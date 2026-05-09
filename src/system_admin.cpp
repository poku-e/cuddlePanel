#include "system_admin.h"

#include "auth.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
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

std::vector<SystemAccountRecord> load_users(const std::string& passwd_path,
                                            const std::string& group_path,
                                            const std::string& shadow_path) {
    const auto sudo_members = sudo_members_from_group_file(group_path);
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
                parts[5],
                parts[6],
                is_system,
                false,
                std::find(sudo_members.begin(), sudo_members.end(), parts[0]) != sudo_members.end(),
                user_locked_in_shadow(shadow_path, parts[0])
            };
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

SystemActionResult SystemAdmin::run_user_action(const std::string& username, const std::string& action) const {
    if (!valid_system_username(username)) {
        return {false, "invalid username"};
    }
    if (username == "root" && (action == "lock" || action == "revoke-sudo")) {
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
