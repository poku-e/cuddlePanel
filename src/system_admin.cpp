#include "system_admin.h"

#include "auth.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
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

struct UserHistoryCandidate {
    const char* name;
    const char* label;
};

constexpr std::array<UserHistoryCandidate, 4> kUserHistoryCandidates{{
    {".bash_history", "Bash history"},
    {".zsh_history",  "Zsh history"},
    {".sh_history",   "POSIX shell history"},
    {".ash_history",  "BusyBox shell history"},
}};

constexpr std::uintmax_t kMaxUserLogfileBytes = 128 * 1024;

bool read_tail_text_file(const std::filesystem::path& path,
                         std::string* content_out,
                         bool* truncated_out) {
    if (!content_out || !truncated_out) {
        return false;
    }
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        return false;
    }

    *truncated_out = size > kMaxUserLogfileBytes;
    const auto bytes_to_read = static_cast<std::streamoff>(*truncated_out ? kMaxUserLogfileBytes : size);
    const auto start_offset = static_cast<std::streamoff>(*truncated_out ? size - kMaxUserLogfileBytes : 0);
    file.seekg(start_offset, std::ios::beg);
    std::string content(static_cast<size_t>(bytes_to_read), '\0');
    file.read(content.data(), bytes_to_read);
    content.resize(static_cast<size_t>(file.gcount()));
    if (*truncated_out) {
        const auto first_newline = content.find('\n');
        if (first_newline != std::string::npos && first_newline + 1 < content.size()) {
            content.erase(0, first_newline + 1);
        }
    }
    *content_out = std::move(content);
    return true;
}

std::optional<std::filesystem::path> resolved_user_history_path(const std::filesystem::path& home,
                                                                const std::string& filename) {
    const auto requested = home / filename;
    std::error_code error;
    if (!std::filesystem::exists(requested, error) || error) {
        return std::nullopt;
    }
    const auto resolved = std::filesystem::weakly_canonical(requested, error);
    if (error || resolved.empty() || !starts_with_path(resolved, home)) {
        return std::nullopt;
    }
    if (!std::filesystem::is_regular_file(resolved, error) || error) {
        return std::nullopt;
    }
    return resolved;
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

std::string zip_binary() {
    return env_or_default("CUDDLEPANEL_ZIP_BIN", "/usr/bin/zip");
}

std::string unzip_binary() {
    return env_or_default("CUDDLEPANEL_UNZIP_BIN", "/usr/bin/unzip");
}

SystemActionResult capture_command_in_dir(const std::vector<std::string>& args,
                                          const std::string& working_directory) {
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
        if (chdir(working_directory.c_str()) != 0) {
            _exit(127);
        }

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

std::string octal_mode_string(std::filesystem::perms perms) {
    auto digit = [perms](std::filesystem::perms read_bit,
                         std::filesystem::perms write_bit,
                         std::filesystem::perms exec_bit) {
        int value = 0;
        if ((perms & read_bit) != std::filesystem::perms::none) value += 4;
        if ((perms & write_bit) != std::filesystem::perms::none) value += 2;
        if ((perms & exec_bit) != std::filesystem::perms::none) value += 1;
        return static_cast<char>('0' + value);
    };
    std::string out = "000";
    out[0] = digit(std::filesystem::perms::owner_read, std::filesystem::perms::owner_write, std::filesystem::perms::owner_exec);
    out[1] = digit(std::filesystem::perms::group_read, std::filesystem::perms::group_write, std::filesystem::perms::group_exec);
    out[2] = digit(std::filesystem::perms::others_read, std::filesystem::perms::others_write, std::filesystem::perms::others_exec);
    return out;
}

bool valid_relative_entry_name(const std::string& name) {
    return !name.empty() &&
           name.size() <= 255 &&
           name != "." &&
           name != ".." &&
           name.find('/') == std::string::npos &&
           name.find('\0') == std::string::npos;
}

std::optional<std::filesystem::path> canonical_allowed_root_for(const std::filesystem::path& normalized_path) {
    std::error_code error;
    for (const auto& root_string : system_allowed_roots()) {
        const auto root = std::filesystem::weakly_canonical(root_string, error);
        if (error) {
            error.clear();
            continue;
        }
        if (starts_with_path(normalized_path, root)) {
            return root;
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> normalized_allowed_existing_path(const std::string& path) {
    if (!valid_home_path(path)) {
        return std::nullopt;
    }
    std::error_code error;
    const auto input = std::filesystem::path(path);
    if (!std::filesystem::exists(input, error) || error) {
        return std::nullopt;
    }
    const auto normalized = std::filesystem::weakly_canonical(input, error);
    if (error || normalized.empty()) {
        return std::nullopt;
    }
    if (!canonical_allowed_root_for(normalized)) {
        return std::nullopt;
    }
    return normalized;
}

std::optional<std::filesystem::path> normalized_allowed_child_path(const std::string& parent_path,
                                                                   const std::string& child_name) {
    if (!valid_home_path(parent_path) || !valid_relative_entry_name(child_name)) {
        return std::nullopt;
    }
    const auto parent = normalized_allowed_existing_path(parent_path);
    if (!parent) {
        return std::nullopt;
    }
    std::error_code error;
    if (!std::filesystem::is_directory(*parent, error) || error) {
        return std::nullopt;
    }
    const auto candidate = *parent / child_name;
    if (canonical_allowed_root_for(candidate.lexically_normal())) {
        return candidate.lexically_normal();
    }
    return std::nullopt;
}

bool contains_symlink(const std::filesystem::path& root_path) {
    std::error_code error;
    if (std::filesystem::is_symlink(root_path, error)) {
        return true;
    }
    if (error || !std::filesystem::is_directory(root_path, error) || error) {
        return false;
    }
    for (std::filesystem::recursive_directory_iterator it(root_path,
             std::filesystem::directory_options::skip_permission_denied, error), end;
         it != end;
         it.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (it->is_symlink(error) && !error) {
            return true;
        }
        error.clear();
    }
    return false;
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

SystemActionResult SystemAdmin::read_user_logfiles(const std::string& username,
                                                   std::vector<SystemUserLogFile>* files_out) const {
    if (!files_out) {
        return {false, "missing output buffer"};
    }
    files_out->clear();
    const auto user = find_user(username);
    if (!user) {
        return {false, "system user not found"};
    }
    if (!is_login_user_record(*user)) {
        return {false, "logfiles are only available for login-enabled users"};
    }

    std::error_code error;
    const auto home = std::filesystem::weakly_canonical(std::filesystem::path(user->home), error);
    if (error || home.empty() || !std::filesystem::is_directory(home, error) || error) {
        return {false, "unable to resolve the user home directory"};
    }

    for (const auto& candidate : kUserHistoryCandidates) {
        const auto history_path = resolved_user_history_path(home, candidate.name);
        if (!history_path) {
            continue;
        }
        std::string content;
        bool truncated = false;
        if (!read_tail_text_file(*history_path, &content, &truncated)) {
            continue;
        }
        files_out->push_back(SystemUserLogFile{candidate.name, candidate.label, content, truncated});
    }

    if (files_out->empty()) {
        return {true, "no supported history files were found in the user home directory"};
    }
    return {true, "user logfiles loaded"};
}

SystemActionResult SystemAdmin::browse_files(const std::string& path,
                                             SystemFileBrowserListing* listing_out) const {
    if (!listing_out) {
        return {false, "missing output buffer"};
    }
    listing_out->entries.clear();
    const auto normalized_path = normalized_allowed_existing_path(path);
    if (!normalized_path) {
        return {false, "path is outside allowed roots or does not exist"};
    }

    std::error_code error;
    if (!std::filesystem::is_directory(*normalized_path, error) || error) {
        return {false, "path is not a directory"};
    }

    listing_out->current_path = normalized_path->string();
    listing_out->parent_path.clear();
    const auto allowed_root = canonical_allowed_root_for(*normalized_path);
    if (allowed_root && *normalized_path != *allowed_root) {
        listing_out->parent_path = normalized_path->parent_path().string();
    }

    for (std::filesystem::directory_iterator it(*normalized_path,
             std::filesystem::directory_options::skip_permission_denied, error), end;
         it != end;
         it.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        const auto name = it->path().filename().string();
        if (!valid_relative_entry_name(name)) {
            continue;
        }
        std::error_code status_error;
        const auto status = it->symlink_status(status_error);
        if (status_error) {
            continue;
        }
        const bool is_symlink = status.type() == std::filesystem::file_type::symlink;
        const bool is_directory = status.type() == std::filesystem::file_type::directory;
        const bool is_regular = status.type() == std::filesystem::file_type::regular;
        std::uintmax_t size = 0;
        if (is_regular) {
            size = it->file_size(status_error);
            if (status_error) {
                size = 0;
            }
        }
        listing_out->entries.push_back(SystemFileEntry{
            name,
            it->path().string(),
            is_symlink ? "symlink" : (is_directory ? "directory" : (is_regular ? "file" : "other")),
            octal_mode_string(status.permissions()),
            size,
            is_directory,
            is_symlink
        });
    }

    std::sort(listing_out->entries.begin(), listing_out->entries.end(), [](const SystemFileEntry& left, const SystemFileEntry& right) {
        if (left.directory != right.directory) {
            return left.directory > right.directory;
        }
        if (left.symlink != right.symlink) {
            return left.symlink < right.symlink;
        }
        return left.name < right.name;
    });
    return {true, "file listing loaded"};
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

SystemActionResult SystemAdmin::run_file_action(const std::string& action,
                                                const std::string& path,
                                                const std::string& destination_path,
                                                const std::string& new_name,
                                                const std::string& owner,
                                                const std::string& group,
                                                const std::string& mode,
                                                bool recursive) const {
    const auto normalized_path = normalized_allowed_existing_path(path);
    if (!normalized_path) {
        return {false, "path is outside allowed roots or does not exist"};
    }

    std::error_code error;
    const auto status = std::filesystem::symlink_status(*normalized_path, error);
    if (error) {
        return {false, "unable to inspect target path"};
    }
    const bool is_directory = status.type() == std::filesystem::file_type::directory;
    const bool is_symlink = status.type() == std::filesystem::file_type::symlink;

    if (is_symlink && action != "rename") {
        return {false, "symlink actions are not supported from the panel"};
    }

    if (action == "chown" || action == "chmod") {
        return run_path_action(action, normalized_path->string(), owner, group, mode, recursive);
    }

    if (action == "rename") {
        if (!valid_relative_entry_name(new_name)) {
            return {false, "invalid new name"};
        }
        const auto destination = normalized_allowed_child_path(normalized_path->parent_path().string(), new_name);
        if (!destination) {
            return {false, "destination is outside allowed roots"};
        }
        if (std::filesystem::exists(*destination, error) && !error) {
            return {false, "destination already exists"};
        }
        std::filesystem::rename(*normalized_path, *destination, error);
        if (error) {
            return {false, "unable to rename the selected path"};
        }
        return {true, "path renamed"};
    }

    if (action == "copy") {
        if (is_symlink) {
            return {false, "copying symlinks is not supported"};
        }
        const auto destination_directory = normalized_allowed_existing_path(destination_path);
        if (!destination_directory) {
            return {false, "destination directory is outside allowed roots or does not exist"};
        }
        if (!std::filesystem::is_directory(*destination_directory, error) || error) {
            return {false, "destination is not a directory"};
        }
        if (is_directory && contains_symlink(*normalized_path)) {
            return {false, "copying directories that contain symlinks is not supported"};
        }
        const auto destination = *destination_directory / normalized_path->filename();
        if (std::filesystem::exists(destination, error) && !error) {
            return {false, "destination already exists"};
        }
        if (is_directory) {
            std::filesystem::copy(*normalized_path, destination, std::filesystem::copy_options::recursive, error);
        } else {
            std::filesystem::copy_file(*normalized_path, destination, std::filesystem::copy_options::none, error);
        }
        if (error) {
            return {false, "unable to copy the selected path"};
        }
        return {true, "path copied"};
    }

    if (action == "zip") {
        if (is_symlink) {
            return {false, "zipping symlinks is not supported"};
        }
        if (is_directory && contains_symlink(*normalized_path)) {
            return {false, "zipping directories that contain symlinks is not supported"};
        }
        const auto parent = normalized_path->parent_path();
        const auto archive_name = normalized_path->filename().string() + ".zip";
        const auto archive_path = parent / archive_name;
        if (std::filesystem::exists(archive_path, error) && !error) {
            return {false, "zip archive already exists"};
        }
        return capture_command_in_dir({zip_binary(), "-r", archive_name, normalized_path->filename().string()}, parent.string());
    }

    if (action == "unzip") {
        if (normalized_path->extension() != ".zip") {
            return {false, "only .zip archives can be extracted"};
        }
        const auto destination_directory = normalized_allowed_existing_path(destination_path);
        if (!destination_directory) {
            return {false, "destination directory is outside allowed roots or does not exist"};
        }
        if (!std::filesystem::is_directory(*destination_directory, error) || error) {
            return {false, "destination is not a directory"};
        }
        return capture_command({unzip_binary(), "-o", normalized_path->string(), "-d", destination_directory->string()});
    }

    return {false, "invalid file action"};
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
