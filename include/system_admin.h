#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cuddle {

struct SystemAccountRecord {
    std::string username;
    int uid = -1;
    int gid = -1;
    std::string comment;
    std::string home;
    std::string shell;
    std::string primary_group;
    std::vector<std::string> secondary_groups;
    std::string expires_on;
    bool system_account = false;
    bool login_user = false;
    bool in_sudo = false;
    bool locked = false;
    bool password_change_required = false;
};

struct SystemActionResult {
    bool ok = false;
    std::string output;
};

struct SystemUserLogFile {
    std::string name;
    std::string label;
    std::string content;
    bool truncated = false;
};

class SystemAdmin {
public:
    SystemAdmin(std::string passwd_path, std::string group_path, std::string shadow_path);

    std::vector<SystemAccountRecord> users() const;
    std::optional<SystemAccountRecord> find_user(const std::string& username) const;
    SystemActionResult create_user(const std::string& username,
                                   const std::string& shell,
                                   const std::string& home,
                                   bool system_account) const;
    SystemActionResult update_user(const std::string& username,
                                   const std::string& shell,
                                   const std::string& home,
                                   bool move_home,
                                   const std::string& comment,
                                   const std::string& primary_group,
                                   const std::vector<std::string>& secondary_groups) const;
    SystemActionResult update_user_security(const std::string& username,
                                           const std::string& password,
                                           bool set_password,
                                           bool force_password_change,
                                           bool clear_expiration,
                                           const std::string& expires_on) const;
    SystemActionResult read_authorized_keys(const std::string& username, std::string* content_out) const;
    SystemActionResult read_user_logfiles(const std::string& username,
                                          std::vector<SystemUserLogFile>* files_out) const;
    SystemActionResult write_authorized_keys(const std::string& username, const std::string& content) const;
    SystemActionResult run_user_action(const std::string& username,
                                       const std::string& action,
                                       bool delete_home) const;
    SystemActionResult run_path_action(const std::string& action,
                                       const std::string& path,
                                       const std::string& owner,
                                       const std::string& group,
                                       const std::string& mode,
                                       bool recursive) const;
    std::vector<std::string> allowed_path_roots() const;

private:
    mutable std::mutex account_command_mutex_;
    std::string passwd_path_;
    std::string group_path_;
    std::string shadow_path_;
};

std::string passwd_file_path();
std::string group_file_path();
std::string shadow_file_path();
std::vector<std::string> system_allowed_roots();
bool valid_system_username(const std::string& username);
bool valid_shell_path(const std::string& path);
bool valid_home_path(const std::string& path);
bool valid_group_name(const std::string& group);
bool valid_owner_name(const std::string& owner);
bool valid_mode_string(const std::string& mode);
bool valid_authorized_keys_content(const std::string& content);
bool valid_gecos_comment(const std::string& content);
bool valid_system_password(const std::string& password);
bool valid_iso_date(const std::string& value);
std::optional<std::string> normalize_allowed_system_path(const std::string& path);

}
