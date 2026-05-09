#include "user_store.h"

#include "auth.h"
#include "totp.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

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

std::string serialize_permissions(const std::map<std::string, PermissionLevel>& permissions) {
    std::string out;
    for (const auto& [page, level] : permissions) {
        if (!out.empty()) {
            out += ",";
        }
        out += page + ":" + permission_to_string(level);
    }
    return out;
}

std::map<std::string, PermissionLevel> parse_permissions(const std::string& value) {
    std::map<std::string, PermissionLevel> permissions;
    for (const auto& item : split(value, ',')) {
        auto pos = item.find(':');
        if (pos == std::string::npos) {
            continue;
        }
        std::string page = item.substr(0, pos);
        auto level = permission_from_string(item.substr(pos + 1));
        if (known_pages().count(page) > 0 && level != PermissionLevel::None) {
            permissions[page] = level;
        }
    }
    return permissions;
}

bool valid_permissions(const std::map<std::string, PermissionLevel>& permissions) {
    const auto pages = known_pages();
    for (const auto& [page, level] : permissions) {
        if (pages.count(page) == 0) {
            return false;
        }
        if (level == PermissionLevel::None) {
            return false;
        }
    }
    return true;
}

}

UserStore::UserStore(std::string path) : path_(std::move(path)) {}

bool UserStore::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    users_.clear();

    std::ifstream file(path_);
    if (!file.good()) {
        return true;
    }

    std::string line;
    while (std::getline(file, line)) {
        auto parts = split(line, '\t');
        if ((parts.size() != 4 && parts.size() != 6) || !valid_username(parts[0])) {
            continue;
        }
        User user;
        user.username = parts[0];
        user.password_hash = parts[1];
        user.role = parts[2];
        user.permissions = parse_permissions(parts[3]);
        if (parts.size() == 6) {
            user.totp_secret = parts[4];
            user.totp_confirmed = parts[5] == "1";
        }
        users_.push_back(user);
    }
    return true;
}

bool UserStore::save() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path());

    std::ofstream file(path_, std::ios::trunc);
    if (!file.good()) {
        return false;
    }
    for (const auto& user : users_) {
        file << user.username << '\t'
             << user.password_hash << '\t'
             << user.role << '\t'
             << serialize_permissions(user.permissions) << '\t'
             << user.totp_secret << '\t'
             << (user.totp_confirmed ? "1" : "0") << '\n';
    }
    file.close();
    chmod(path_.c_str(), S_IRUSR | S_IWUSR);
    return true;
}

bool UserStore::has_users() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !users_.empty();
}

bool UserStore::create_superuser(const std::string& username, const std::string& password) {
    if (!valid_username(username) || !valid_password(password)) {
        return false;
    }

    auto hash = hash_password(password);
    if (!hash) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!users_.empty()) {
            return false;
        }

        User user;
        user.username = username;
        user.password_hash = *hash;
        user.role = "superuser";
        for (const auto& page : known_pages()) {
            user.permissions[page] = PermissionLevel::Manage;
        }
        user.totp_secret = generate_totp_secret();
        user.totp_confirmed = false;
        users_.push_back(user);
    }
    return save();
}

bool UserStore::create_user(const std::string& username,
                            const std::string& password,
                            const std::string& role,
                            const std::map<std::string, PermissionLevel>& permissions) {
    if (!valid_username(username) || !valid_password(password)) {
        return false;
    }
    if (role == "superuser") {
        return false;
    }
    if (allowed_roles().count(role) == 0 || !valid_permissions(permissions)) {
        return false;
    }

    auto hash = hash_password(password);
    if (!hash) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& user : users_) {
            if (user.username == username) {
                return false;
            }
        }
        users_.push_back(User{username, *hash, role, permissions, "", false});
    }
    return save();
}

bool UserStore::delete_user(const std::string& username) {
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = users_.begin(); it != users_.end(); ++it) {
            if (it->username != username) {
                continue;
            }
            if (it->role == "superuser") {
                return false;
            }
            users_.erase(it);
            removed = true;
            break;
        }
    }
    return removed && save();
}

bool UserStore::update_permissions(const std::string& username,
                                   const std::map<std::string, PermissionLevel>& permissions) {
    if (!valid_permissions(permissions)) {
        return false;
    }

    bool updated = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& user : users_) {
            if (user.username != username) {
                continue;
            }
            if (user.role == "superuser") {
                return false;
            }
            user.permissions = permissions;
            updated = true;
            break;
        }
    }
    return updated && save();
}

bool UserStore::set_totp_secret(const std::string& username, const std::string& secret) {
    bool updated = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& user : users_) {
            if (user.username != username) {
                continue;
            }
            user.totp_secret = secret;
            user.totp_confirmed = false;
            updated = true;
            break;
        }
    }
    return updated && save();
}

bool UserStore::confirm_totp(const std::string& username) {
    bool updated = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& user : users_) {
            if (user.username != username) {
                continue;
            }
            if (user.totp_secret.empty()) {
                return false;
            }
            user.totp_confirmed = true;
            updated = true;
            break;
        }
    }
    return updated && save();
}

std::optional<User> UserStore::authenticate(const std::string& username, const std::string& password) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& user : users_) {
        if (user.username == username && verify_password(password, user.password_hash)) {
            return user;
        }
    }
    return std::nullopt;
}

std::optional<User> UserStore::find_user(const std::string& username) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& user : users_) {
        if (user.username == username) {
            return user;
        }
    }
    return std::nullopt;
}

std::vector<User> UserStore::users() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return users_;
}

bool UserStore::has_permission(const std::string& username, const std::string& page, PermissionLevel level) const {
    if (known_pages().count(page) == 0) {
        return false;
    }

    auto user = find_user(username);
    if (!user) {
        return false;
    }
    if (user->role == "superuser") {
        return true;
    }

    auto found = user->permissions.find(page);
    if (found == user->permissions.end()) {
        return false;
    }
    return static_cast<int>(found->second) >= static_cast<int>(level);
}

std::set<std::string> known_pages() {
    return {"codex", "dashboard", "deploy", "nginx", "services", "system", "terminal", "users"};
}

std::set<std::string> allowed_roles() {
    return {"admin", "operator", "superuser"};
}

std::string permission_to_string(PermissionLevel level) {
    switch (level) {
        case PermissionLevel::View:
            return "view";
        case PermissionLevel::Manage:
            return "manage";
        case PermissionLevel::None:
        default:
            return "none";
    }
}

PermissionLevel permission_from_string(const std::string& value) {
    if (value == "view") {
        return PermissionLevel::View;
    }
    if (value == "manage") {
        return PermissionLevel::Manage;
    }
    return PermissionLevel::None;
}

}
