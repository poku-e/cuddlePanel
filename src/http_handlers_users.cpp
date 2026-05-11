#include "http.h"

#include <sstream>

namespace cuddle {
namespace {

std::string users_json(const std::vector<User>& users) {
    std::ostringstream out;
    out << "{\"users\":[";
    bool first = true;
    for (const auto& user : users) {
        if (!first) out << ",";
        first = false;
        out << "{\"username\":\"" << json_escape(user.username)
            << "\",\"role\":\""   << json_escape(user.role)
            << "\",\"permissions\":{";
        bool first_perm = true;
        for (const auto& page : known_pages()) {
            auto found = user.permissions.find(page);
            if (found == user.permissions.end()) continue;
            if (!first_perm) out << ",";
            first_perm = false;
            out << "\"" << json_escape(page) << "\":\"" << json_escape(permission_to_string(found->second)) << "\"";
        }
        out << "}}";
    }
    out << "]}";
    return out.str();
}

std::map<std::string, PermissionLevel> parse_permission_map(const std::string& raw) {
    std::map<std::string, PermissionLevel> permissions;
    std::stringstream stream(raw);
    std::string item;
    while (std::getline(stream, item, ',')) {
        auto pos = item.find(':');
        if (pos == std::string::npos) continue;
        auto page  = item.substr(0, pos);
        auto level = permission_from_string(item.substr(pos + 1));
        if (known_pages().count(page) > 0 && level != PermissionLevel::None) {
            permissions[page] = level;
        }
    }
    return permissions;
}

} // anonymous namespace

HttpResponse handle_users(const RequestContext& ctx, const std::string&) {
    if (ctx.request.method == "GET") {
        if (auto err = ctx.require_permission("users", PermissionLevel::View)) return *err;
        return json_response(200, users_json(ctx.users.users()));
    }
    if (ctx.request.method == "POST") {
        if (auto err = ctx.require_permission("users", PermissionLevel::Manage)) return *err;
        auto form = parse_form(ctx.request.body);
        const auto permissions = parse_permission_map(form["permissions"]);
        if (!ctx.users.create_user(form["username"], form["password"], form["role"], permissions)) {
            return json_response(400, "{\"error\":\"unable to create user\"}");
        }
        return json_response(200, "{\"ok\":true}");
    }
    return json_response(404, "{\"error\":\"not found\"}");
}

HttpResponse handle_user_permissions(const RequestContext& ctx, const std::string& username) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("users", PermissionLevel::Manage)) return *err;
    auto form = parse_form(ctx.request.body);
    const auto permissions = parse_permission_map(form["permissions"]);
    if (!ctx.users.update_permissions(username, permissions)) {
        return json_response(400, "{\"error\":\"unable to update permissions\"}");
    }
    return json_response(200, "{\"ok\":true}");
}

HttpResponse handle_delete_user(const RequestContext& ctx, const std::string& username) {
    if (ctx.request.method != "DELETE") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("users", PermissionLevel::Manage)) return *err;
    if (ctx.username && *ctx.username == username) {
        return json_response(400, "{\"error\":\"cannot delete current session user\"}");
    }
    if (!ctx.users.delete_user(username)) {
        return json_response(400, "{\"error\":\"unable to delete user\"}");
    }
    return json_response(200, "{\"ok\":true}");
}

} // namespace cuddle
