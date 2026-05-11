#include "system_admin.h"

#include "auth.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <tuple>
#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace cuddle {
namespace {

std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, delimiter)) {
        parts.push_back(item);
    }
    return parts;
}

std::string env_or_default(const char* name, const char* fallback) {
    const char* configured = std::getenv(name);
    return configured && *configured ? configured : fallback;
}

bool valid_account_token(const std::string& value) {
    return valid_username(value);
}

bool starts_with_path(const std::filesystem::path& path, const std::filesystem::path& root) {
    auto path_it = path.begin();
    auto root_it = root.begin();
    for (; root_it != root.end(); ++root_it, ++path_it) {
        if (path_it == path.end() || *path_it != *root_it) {
            return false;
        }
    }
    return true;
}

bool is_login_shell(const std::string& shell) {
    return !shell.empty() &&
           shell != "/usr/sbin/nologin" &&
           shell != "/sbin/nologin" &&
           shell != "/bin/false" &&
           shell != "/usr/bin/false";
}

bool is_login_user_record(const SystemAccountRecord& user) {
    return !user.system_account && user.uid >= 1000 && valid_home_path(user.home) && is_login_shell(user.shell);
}

std::optional<std::filesystem::path> authorized_keys_path_for(const SystemAccountRecord& user) {
    if (!is_login_user_record(user)) {
        return std::nullopt;
    }
    std::error_code error;
    const auto home = std::filesystem::weakly_canonical(std::filesystem::path(user.home), error);
    if (error || home.empty()) {
        return std::nullopt;
    }
    return home / ".ssh" / "authorized_keys";
}

SystemActionResult capture_command(const std::vector<std::string>& args) {
    std::array<char, 256> buffer{};
    std::string output;
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        return {false, "unable to execute command"};
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return {false, "unable to execute command"};
    }

    if (pid == 0) {
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv(argv[0], argv.data());
        _exit(127);
    }

    close(pipe_fds[1]);
    ssize_t read_count = 0;
    while ((read_count = read(pipe_fds[0], buffer.data(), buffer.size())) > 0) {
        output.append(buffer.data(), static_cast<size_t>(read_count));
    }
    close(pipe_fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (output.empty()) {
        output = ok ? "command completed successfully" : "command failed";
    }
    return {ok, output};
}

SystemActionResult capture_command_with_input(const std::vector<std::string>& args, const std::string& input) {
    std::array<char, 256> buffer{};
    std::string output;
    int stdout_pipe[2] = {-1, -1};
    int stdin_pipe[2] = {-1, -1};
    if (pipe(stdout_pipe) != 0 || pipe(stdin_pipe) != 0) {
        if (stdout_pipe[0] >= 0) close(stdout_pipe[0]);
        if (stdout_pipe[1] >= 0) close(stdout_pipe[1]);
        if (stdin_pipe[0] >= 0) close(stdin_pipe[0]);
        if (stdin_pipe[1] >= 0) close(stdin_pipe[1]);
        return {false, "unable to execute command"};
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return {false, "unable to execute command"};
    }

    if (pid == 0) {
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);
        dup2(stdin_pipe[0], STDIN_FILENO);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv(argv[0], argv.data());
        _exit(127);
    }

    close(stdout_pipe[1]);
    close(stdin_pipe[0]);
    if (!input.empty()) {
        const char* data = input.data();
        size_t remaining = input.size();
        while (remaining > 0) {
            const ssize_t written = write(stdin_pipe[1], data, remaining);
            if (written <= 0) {
                break;
            }
            data += written;
            remaining -= static_cast<size_t>(written);
        }
    }
    close(stdin_pipe[1]);

    ssize_t read_count = 0;
    while ((read_count = read(stdout_pipe[0], buffer.data(), buffer.size())) > 0) {
        output.append(buffer.data(), static_cast<size_t>(read_count));
    }
    close(stdout_pipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (output.empty()) {
        output = ok ? "command completed successfully" : "command failed";
    }
    return {ok, output};
}

std::string useradd_binary() {
    return env_or_default("CUDDLEPANEL_USERADD_BIN", "/usr/sbin/useradd");
}

std::string passwd_binary() {
    return env_or_default("CUDDLEPANEL_PASSWD_BIN", "/usr/bin/passwd");
}

std::string usermod_binary() {
    return env_or_default("CUDDLEPANEL_USERMOD_BIN", "/usr/sbin/usermod");
}

std::string gpasswd_binary() {
    return env_or_default("CUDDLEPANEL_GPASSWD_BIN", "/usr/bin/gpasswd");
}

std::string userdel_binary() {
    return env_or_default("CUDDLEPANEL_USERDEL_BIN", "/usr/sbin/userdel");
}

std::string chpasswd_binary() {
    return env_or_default("CUDDLEPANEL_CHPASSWD_BIN", "/usr/sbin/chpasswd");
}

std::string chage_binary() {
    return env_or_default("CUDDLEPANEL_CHAGE_BIN", "/usr/bin/chage");
}

std::string chown_binary() {
    return env_or_default("CUDDLEPANEL_CHOWN_BIN", "/bin/chown");
}

std::string chmod_binary() {
    return env_or_default("CUDDLEPANEL_CHMOD_BIN", "/bin/chmod");
}

std::vector<std::string> read_lines(const std::string& path) {
    std::ifstream file(path);
    std::vector<std::string> lines;
    if (!file.good()) {
        return lines;
    }
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> sudo_members_from_group_file(const std::string& group_path) {
    for (const auto& line : read_lines(group_path)) {
        const auto parts = split(line, ':');
        if (parts.size() < 4 || parts[0] != "sudo") {
            continue;
        }
        std::vector<std::string> members;
        if (!parts[3].empty()) {
            members = split(parts[3], ',');
        }
        return members;
    }
    return {};
}

struct GroupRecord {
    std::string name;
    int gid = -1;
    std::vector<std::string> members;
};

std::vector<GroupRecord> load_groups(const std::string& group_path) {
    std::vector<GroupRecord> groups;
    for (const auto& line : read_lines(group_path)) {
        const auto parts = split(line, ':');
        if (parts.size() < 4 || !valid_group_name(parts[0])) {
            continue;
        }
        try {
            GroupRecord record;
            record.name = parts[0];
            record.gid = std::stoi(parts[2]);
            if (!parts[3].empty()) {
                for (const auto& member : split(parts[3], ',')) {
                    if (valid_system_username(member)) {
                        record.members.push_back(member);
                    }
                }
            }
            groups.push_back(record);
        } catch (...) {
            continue;
        }
    }
    return groups;
}

bool user_locked_in_shadow(const std::string& shadow_path, const std::string& username) {
    for (const auto& line : read_lines(shadow_path)) {
        const auto parts = split(line, ':');
        if (parts.size() < 2 || parts[0] != username) {
            continue;
        }
        const std::string& password_field = parts[1];
        return !password_field.empty() &&
               (password_field[0] == '!' || password_field[0] == '*' || password_field == "LK");
    }
    return false;
}

std::optional<std::string> shadow_days_to_iso_date(const std::string& raw_days) {
    if (raw_days.empty()) {
        return std::nullopt;
    }
    try {
        const long long days = std::stoll(raw_days);
        if (days < 0) {
            return std::nullopt;
        }
        std::time_t timestamp = static_cast<std::time_t>(days * 86400);
        std::tm tm{};
        if (!gmtime_r(&timestamp, &tm)) {
            return std::nullopt;
        }
        char buffer[11];
        if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm) == 0) {
            return std::nullopt;
        }
        return std::string(buffer);
    } catch (...) {
        return std::nullopt;
    }
}

bool user_password_change_required_in_shadow(const std::string& shadow_path, const std::string& username) {
    for (const auto& line : read_lines(shadow_path)) {
        const auto parts = split(line, ':');
        if (parts.size() < 3 || parts[0] != username) {
            continue;
        }
        return parts[2] == "0";
    }
    return false;
}

std::optional<std::string> user_expiration_in_shadow(const std::string& shadow_path, const std::string& username) {
    for (const auto& line : read_lines(shadow_path)) {
        const auto parts = split(line, ':');
        if (parts.size() < 8 || parts[0] != username) {
            continue;
        }
        return shadow_days_to_iso_date(parts[7]);
    }
    return std::nullopt;
}

std::vector<SystemAccountRecord> load_users(const std::string& passwd_path,
                                            const std::string& group_path,
                                            const std::string& shadow_path) {
    const auto groups = load_groups(group_path);
    const auto sudo_members = sudo_members_from_group_file(group_path);
    std::map<int, std::string> group_name_by_gid;
    for (const auto& group : groups) {
        group_name_by_gid[group.gid] = group.name;
    }
    std::vector<SystemAccountRecord> users;
    for (const auto& line : read_lines(passwd_path)) {
        const auto parts = split(line, ':');
        if (parts.size() < 7 || !valid_system_username(parts[0])) {
            continue;
        }
        try {
            const int uid = std::stoi(parts[2]);
            const int gid = std::stoi(parts[3]);
            const bool is_system = uid >= 0 && uid < 1000;
            SystemAccountRecord record{
                parts[0],
                uid,
                gid,
                parts[4],
                parts[5],
                parts[6],
                group_name_by_gid.count(gid) ? group_name_by_gid[gid] : "",
                {},
                user_expiration_in_shadow(shadow_path, parts[0]).value_or(""),
                is_system,
                false,
                std::find(sudo_members.begin(), sudo_members.end(), parts[0]) != sudo_members.end(),
                user_locked_in_shadow(shadow_path, parts[0]),
                user_password_change_required_in_shadow(shadow_path, parts[0])
            };
            for (const auto& group : groups) {
                if (group.name == record.primary_group) {
                    continue;
                }
                if (std::find(group.members.begin(), group.members.end(), record.username) != group.members.end()) {
                    record.secondary_groups.push_back(group.name);
                }
            }
            std::sort(record.secondary_groups.begin(), record.secondary_groups.end());
            record.login_user = is_login_user_record(record);
            users.push_back(record);
        } catch (...) {
            continue;
        }
    }

    std::sort(users.begin(), users.end(), [](const SystemAccountRecord& left, const SystemAccountRecord& right) {
        if (left.uid != right.uid) {
            return left.uid < right.uid;
        }
        return left.username < right.username;
    });
    return users;
}

}

SystemAdmin::SystemAdmin(std::string passwd_path, std::string group_path, std::string shadow_path)
    : passwd_path_(std::move(passwd_path)),
      group_path_(std::move(group_path)),
      shadow_path_(std::move(shadow_path)) {}

std::vector<SystemAccountRecord> SystemAdmin::users() const {
    return load_users(passwd_path_, group_path_, shadow_path_);
}

std::optional<SystemAccountRecord> SystemAdmin::find_user(const std::string& username) const {
    for (const auto& user : users()) {
        if (user.username == username) {
            return user;
        }
    }
    return std::nullopt;
}

SystemActionResult SystemAdmin::create_user(const std::string& username,
                                            const std::string& shell,
                                            const std::string& home,
                                            bool system_account) const {
    std::lock_guard<std::mutex> lock(account_command_mutex_);
    if (!valid_system_username(username)) {
        return {false, "invalid username"};
    }
    if (!valid_shell_path(shell)) {
        return {false, "invalid shell path"};
    }
    if (!valid_home_path(home)) {
        return {false, "invalid home path"};
    }
    if (find_user(username)) {
        return {false, "account already exists"};
    }

    std::vector<std::string> args{useradd_binary()};
    if (system_account) {
        args.insert(args.end(), {"-r", "-M"});
    } else {
        args.push_back("-m");
    }
    args.insert(args.end(), {"-d", home, "-s", shell, username});
    return capture_command(args);
}

SystemActionResult SystemAdmin::update_user(const std::string& username,
                                            const std::string& shell,
                                            const std::string& home,
                                            bool move_home,
                                            const std::string& comment,
                                            const std::string& primary_group,
                                            const std::vector<std::string>& secondary_groups) const {
    std::lock_guard<std::mutex> lock(account_command_mutex_);
    if (!valid_system_username(username)) {
        return {false, "invalid username"};
    }
    if (username == "root") {
        return {false, "refusing to edit root in this phase"};
    }
    const auto existing_user = find_user(username);
    if (!existing_user) {
        return {false, "system user not found"};
    }
    if (!valid_shell_path(shell)) {
        return {false, "invalid shell path"};
    }
    if (!valid_home_path(home)) {
        return {false, "invalid home path"};
    }
    if (!valid_gecos_comment(comment)) {
        return {false, "invalid comment"};
    }
    if (!valid_group_name(primary_group)) {
        return {false, "invalid primary group"};
    }

    std::vector<std::string> normalized_secondary_groups;
    normalized_secondary_groups.reserve(secondary_groups.size());
    for (const auto& group : secondary_groups) {
        if (group.empty()) {
            continue;
        }
        if (!valid_group_name(group)) {
            return {false, "invalid supplementary group"};
        }
        if (group == primary_group) {
            continue;
        }
        normalized_secondary_groups.push_back(group);
    }
    std::sort(normalized_secondary_groups.begin(), normalized_secondary_groups.end());
    normalized_secondary_groups.erase(
        std::unique(normalized_secondary_groups.begin(), normalized_secondary_groups.end()),
        normalized_secondary_groups.end());

    std::vector<std::string> args{usermod_binary()};
    args.insert(args.end(), {"-s", shell});
    if (move_home && home != existing_user->home) {
        args.push_back("-m");
    }
    args.insert(args.end(), {"-d", home});
    args.insert(args.end(), {"-c", comment});
    args.insert(args.end(), {"-g", primary_group});
    args.insert(args.end(), {"-G", normalized_secondary_groups.empty() ? "" : [&normalized_secondary_groups]() {
        std::ostringstream out;
        for (size_t i = 0; i < normalized_secondary_groups.size(); ++i) {
            if (i != 0) {
                out << ",";
            }
            out << normalized_secondary_groups[i];
        }
        return out.str();
    }()});
    args.push_back(username);
    return capture_command(args);
}

SystemActionResult SystemAdmin::update_user_security(const std::string& username,
                                                     const std::string& password,
                                                     bool set_password,
                                                     bool force_password_change,
                                                     bool clear_expiration,
                                                     const std::string& expires_on) const {
    std::lock_guard<std::mutex> lock(account_command_mutex_);
    if (!valid_system_username(username)) {
        return {false, "invalid username"};
    }
    if (username == "root") {
        return {false, "refusing to edit root credentials in this phase"};
    }
    if (!find_user(username)) {
        return {false, "system user not found"};
    }
    if (set_password && !valid_system_password(password)) {
        return {false, "invalid password"};
    }
    if (!clear_expiration && !expires_on.empty() && !valid_iso_date(expires_on)) {
        return {false, "invalid expiration date"};
    }

    if (set_password) {
        const auto password_result = capture_command_with_input(
            {chpasswd_binary()},
            username + ":" + password + "\n");
        if (!password_result.ok) {
            return password_result;
        }
    }

    if (force_password_change) {
        const auto force_result = capture_command({chage_binary(), "-d", "0", username});
        if (!force_result.ok) {
            return force_result;
        }
    }

    if (clear_expiration) {
        const auto clear_result = capture_command({chage_binary(), "-E", "-1", username});
        if (!clear_result.ok) {
            return clear_result;
        }
    } else if (!expires_on.empty()) {
        const auto expiry_result = capture_command({chage_binary(), "-E", expires_on, username});
        if (!expiry_result.ok) {
            return expiry_result;
        }
    }

    return {true, "account security updated"};
}

SystemActionResult SystemAdmin::read_authorized_keys(const std::string& username, std::string* content_out) const {
    if (!content_out) {
        return {false, "missing output buffer"};
    }
    const auto user = find_user(username);
    if (!user) {
        return {false, "system user not found"};
    }
    const auto keys_path = authorized_keys_path_for(*user);
    if (!keys_path) {
        return {false, "authorized_keys is only available for login users"};
    }

    std::ifstream file(*keys_path, std::ios::binary);
    if (!file.good()) {
        content_out->clear();
        return {true, "authorized_keys not present yet"};
    }
    std::ostringstream out;
    out << file.rdbuf();
    *content_out = out.str();
    if (!valid_authorized_keys_content(*content_out)) {
        return {false, "authorized_keys content is invalid or too large"};
    }
    return {true, "authorized_keys loaded"};
}

SystemActionResult SystemAdmin::write_authorized_keys(const std::string& username, const std::string& content) const {
    if (!valid_authorized_keys_content(content)) {
        return {false, "invalid authorized_keys content"};
    }
    const auto user = find_user(username);
    if (!user) {
        return {false, "system user not found"};
    }
    const auto keys_path = authorized_keys_path_for(*user);
    if (!keys_path) {
        return {false, "authorized_keys is only available for login users"};
    }

    std::error_code error;
    const auto ssh_dir = keys_path->parent_path();
    std::filesystem::create_directories(ssh_dir, error);
    if (error) {
        return {false, "unable to prepare .ssh directory"};
    }
    if (chmod(ssh_dir.c_str(), S_IRWXU) != 0) {
        return {false, "unable to secure .ssh directory"};
    }
    if (chown(ssh_dir.c_str(), static_cast<uid_t>(user->uid), static_cast<gid_t>(user->gid)) != 0) {
        return {false, "unable to assign .ssh ownership"};
    }

    std::ofstream file(*keys_path, std::ios::binary | std::ios::trunc);
    if (!file.good()) {
        return {false, "unable to write authorized_keys"};
    }
    file << content;
    file.close();
    if (!file) {
        return {false, "unable to write authorized_keys"};
    }
    if (chmod(keys_path->c_str(), S_IRUSR | S_IWUSR) != 0) {
        return {false, "unable to secure authorized_keys"};
    }
    if (chown(keys_path->c_str(), static_cast<uid_t>(user->uid), static_cast<gid_t>(user->gid)) != 0) {
        return {false, "unable to assign authorized_keys ownership"};
    }
    return {true, "authorized_keys saved"};
}

SystemActionResult SystemAdmin::run_user_action(const std::string& username,
                                                const std::string& action,
                                                bool delete_home) const {
    std::lock_guard<std::mutex> lock(account_command_mutex_);
    if (!valid_system_username(username)) {
        return {false, "invalid username"};
    }
    if (username == "root" && (action == "lock" || action == "revoke-sudo" || action == "delete")) {
        return {false, "refusing to modify root in this way"};
    }
    if (!find_user(username)) {
        return {false, "system user not found"};
    }

    if (action == "lock") {
        return capture_command({passwd_binary(), "-l", username});
    }
    if (action == "unlock") {
        return capture_command({passwd_binary(), "-u", username});
    }
    if (action == "grant-sudo") {
        return capture_command({usermod_binary(), "-aG", "sudo", username});
    }
    if (action == "revoke-sudo") {
        return capture_command({gpasswd_binary(), "-d", username, "sudo"});
    }
    if (action == "delete") {
        std::vector<std::string> args{userdel_binary()};
        if (delete_home) {
            args.push_back("-r");
        }
        args.push_back(username);
        return capture_command(args);
    }
    return {false, "invalid system user action"};
}

SystemActionResult SystemAdmin::run_path_action(const std::string& action,
                                                const std::string& path,
                                                const std::string& owner,
                                                const std::string& group,
                                                const std::string& mode,
                                                bool recursive) const {
    const auto normalized_path = normalize_allowed_system_path(path);
    if (!normalized_path) {
        return {false, "path is outside allowed roots or does not exist"};
    }

    if (action == "chown") {
        if (!valid_owner_name(owner)) {
            return {false, "invalid owner"};
        }
        if (!group.empty() && !valid_group_name(group)) {
            return {false, "invalid group"};
        }
        std::vector<std::string> args{chown_binary()};
        if (recursive) {
            args.push_back("-R");
        }
        args.push_back(group.empty() ? owner : owner + ":" + group);
        args.push_back(*normalized_path);
        return capture_command(args);
    }

    if (action == "chmod") {
        if (!valid_mode_string(mode)) {
            return {false, "invalid mode"};
        }
        std::vector<std::string> args{chmod_binary()};
        if (recursive) {
            args.push_back("-R");
        }
        args.push_back(mode);
        args.push_back(*normalized_path);
        return capture_command(args);
    }

    return {false, "invalid path action"};
}

std::vector<std::string> SystemAdmin::allowed_path_roots() const {
    return system_allowed_roots();
}

std::string passwd_file_path() {
    return env_or_default("CUDDLEPANEL_PASSWD_FILE", "/etc/passwd");
}

std::string group_file_path() {
    return env_or_default("CUDDLEPANEL_GROUP_FILE", "/etc/group");
}

std::string shadow_file_path() {
    return env_or_default("CUDDLEPANEL_SHADOW_FILE", "/etc/shadow");
}

std::vector<std::string> system_allowed_roots() {
    const char* configured = std::getenv("CUDDLEPANEL_SYSTEM_ALLOWED_ROOTS");
    const std::string raw = configured && *configured ? configured : "/home,/srv,/var/www";
    std::vector<std::string> roots;
    for (const auto& part : split(raw, ',')) {
        if (part.empty() || part[0] != '/') {
            continue;
        }
        roots.push_back(part);
    }
    return roots;
}

bool valid_system_username(const std::string& username) {
    return valid_account_token(username);
}

bool valid_shell_path(const std::string& path) {
    return valid_home_path(path);
}

bool valid_home_path(const std::string& path) {
    return !path.empty() &&
           path.size() <= 512 &&
           path.front() == '/' &&
           path.find('\0') == std::string::npos;
}

bool valid_group_name(const std::string& group) {
    return valid_account_token(group);
}

bool valid_owner_name(const std::string& owner) {
    return valid_account_token(owner);
}

bool valid_mode_string(const std::string& mode) {
    if (mode.size() < 3 || mode.size() > 4) {
        return false;
    }
    return std::all_of(mode.begin(), mode.end(), [](unsigned char c) {
        return c >= '0' && c <= '7';
    });
}

bool valid_authorized_keys_content(const std::string& content) {
    if (content.size() > 256 * 1024) {
        return false;
    }
    return content.find('\0') == std::string::npos;
}

bool valid_gecos_comment(const std::string& content) {
    if (content.size() > 256 || content.find('\0') != std::string::npos) {
        return false;
    }
    return std::none_of(content.begin(), content.end(), [](unsigned char c) {
        return c == '\n' || c == '\r';
    });
}

bool valid_system_password(const std::string& password) {
    if (password.size() < 8 || password.size() > 256) {
        return false;
    }
    return std::none_of(password.begin(), password.end(), [](unsigned char c) {
        return c == '\0' || c == '\n' || c == '\r' || c == ':';
    });
}

bool valid_iso_date(const std::string& value) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
        return false;
    }
    for (size_t i = 0; i < value.size(); ++i) {
        if (i == 4 || i == 7) {
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
            return false;
        }
    }
    const int year = std::stoi(value.substr(0, 4));
    const int month = std::stoi(value.substr(5, 2));
    const int day = std::stoi(value.substr(8, 2));
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_isdst = -1;
    return std::mktime(&tm) != -1;
}

std::optional<std::string> normalize_allowed_system_path(const std::string& path) {
    if (!valid_home_path(path)) {
        return std::nullopt;
    }
    std::error_code error;
    const auto input = std::filesystem::path(path);
    if (!std::filesystem::exists(input, error)) {
        return std::nullopt;
    }
    const auto normalized = std::filesystem::weakly_canonical(input, error);
    if (error) {
        return std::nullopt;
    }
    for (const auto& root_string : system_allowed_roots()) {
        const auto root = std::filesystem::weakly_canonical(root_string, error);
        if (error) {
            error.clear();
            continue;
        }
        if (starts_with_path(normalized, root)) {
            return normalized.string();
        }
    }
    return std::nullopt;
}

}
