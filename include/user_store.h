#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace cuddle {

enum class PermissionLevel {
    None = 0,
    View = 1,
    Manage = 2,
};

struct User {
    std::string username;
    std::string password_hash;
    std::string role;
    std::map<std::string, PermissionLevel> permissions;
    std::string totp_secret;
    bool totp_confirmed = false;
};

class UserStore {
public:
    explicit UserStore(std::string path);

    bool load();
    bool save() const;
    bool has_users() const;
    bool create_superuser(const std::string& username, const std::string& password);
    bool create_user(const std::string& username,
                     const std::string& password,
                     const std::string& role,
                     const std::map<std::string, PermissionLevel>& permissions);
    bool delete_user(const std::string& username);
    bool update_permissions(const std::string& username,
                            const std::map<std::string, PermissionLevel>& permissions);
    bool set_totp_secret(const std::string& username, const std::string& secret);
    bool confirm_totp(const std::string& username);
    std::optional<User> authenticate(const std::string& username, const std::string& password) const;
    std::optional<User> find_user(const std::string& username) const;
    std::vector<User> users() const;
    bool has_permission(const std::string& username, const std::string& page, PermissionLevel level) const;

private:
    std::string path_;
    mutable std::mutex mutex_;
    std::vector<User> users_;
};

std::set<std::string> known_pages();
std::set<std::string> allowed_roles();
std::string permission_to_string(PermissionLevel level);
PermissionLevel permission_from_string(const std::string& value);

}
