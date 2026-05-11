#include "http.h"

#include <sstream>

namespace cuddle {
namespace {

std::string system_users_json(const SystemAdmin& system_admin) {
    std::ostringstream out;
    out << "{\"users\":[";
    bool first = true;
    for (const auto& user : system_admin.users()) {
        if (!first) out << ",";
        first = false;
        out << "{\"username\":\"" << json_escape(user.username)
            << "\",\"uid\":"      << user.uid
            << ",\"gid\":"        << user.gid
            << ",\"home\":\""     << json_escape(user.home)
            << "\",\"shell\":\""  << json_escape(user.shell)
            << "\",\"system_account\":" << (user.system_account ? "true" : "false")
            << ",\"login_user\":"  << (user.login_user  ? "true" : "false")
            << ",\"in_sudo\":"     << (user.in_sudo      ? "true" : "false")
            << ",\"locked\":"      << (user.locked       ? "true" : "false")
            << "}";
    }
    out << "],\"allowedRoots\":[";
    bool first_root = true;
    for (const auto& root : system_admin.allowed_path_roots()) {
        if (!first_root) out << ",";
        first_root = false;
        out << "\"" << json_escape(root) << "\"";
    }
    out << "]}";
    return out.str();
}

} // anonymous namespace

HttpResponse handle_system_users(const RequestContext& ctx, const std::string&) {
    if (ctx.request.method == "GET") {
        if (auto err = ctx.require_permission("system", PermissionLevel::View)) return *err;
        return json_response(200, system_users_json(ctx.system_admin));
    }
    if (ctx.request.method == "POST") {
        if (auto err = ctx.require_permission("system", PermissionLevel::Manage)) return *err;
        auto form = parse_form(ctx.request.body);
        const bool system_account = form["system_account"] == "on";
        const auto result = ctx.system_admin.create_user(form["username"], form["shell"], form["home"], system_account);
        std::ostringstream out;
        out << "{\"ok\":" << (result.ok ? "true" : "false")
            << ",\"output\":\"" << json_escape(result.output) << "\"}";
        return json_response(result.ok ? 200 : 400, out.str());
    }
    return json_response(404, "{\"error\":\"not found\"}");
}

HttpResponse handle_system_user_action(const RequestContext& ctx, const std::string& username) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("system", PermissionLevel::Manage)) return *err;
    auto form = parse_form(ctx.request.body);
    const auto result = ctx.system_admin.run_user_action(username, form["action"]);
    std::ostringstream out;
    out << "{\"ok\":" << (result.ok ? "true" : "false")
        << ",\"output\":\"" << json_escape(result.output) << "\"}";
    return json_response(result.ok ? 200 : 400, out.str());
}

HttpResponse handle_system_authorized_keys(const RequestContext& ctx, const std::string& username) {
    if (auto err = ctx.require_permission("system", PermissionLevel::Manage)) return *err;
    if (ctx.request.method == "GET") {
        std::string content;
        const auto result = ctx.system_admin.read_authorized_keys(username, &content);
        std::ostringstream out;
        out << "{\"ok\":"        << (result.ok ? "true" : "false")
            << ",\"username\":\"" << json_escape(username) << "\""
            << ",\"content\":\""  << json_escape(content) << "\""
            << ",\"output\":\""   << json_escape(result.output) << "\"}";
        return json_response(result.ok ? 200 : 400, out.str());
    }
    if (ctx.request.method == "POST") {
        auto form = parse_form(ctx.request.body);
        const auto result = ctx.system_admin.write_authorized_keys(username, form["content"]);
        std::ostringstream out;
        out << "{\"ok\":" << (result.ok ? "true" : "false")
            << ",\"output\":\"" << json_escape(result.output) << "\"}";
        return json_response(result.ok ? 200 : 400, out.str());
    }
    return json_response(404, "{\"error\":\"not found\"}");
}

HttpResponse handle_system_path_action(const RequestContext& ctx, const std::string&) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("system", PermissionLevel::Manage)) return *err;
    auto form = parse_form(ctx.request.body);
    const bool recursive = form["recursive"] == "on";
    const auto result = ctx.system_admin.run_path_action(
        form["action"], form["path"], form["owner"], form["group"], form["mode"], recursive);
    std::ostringstream out;
    out << "{\"ok\":" << (result.ok ? "true" : "false")
        << ",\"output\":\"" << json_escape(result.output) << "\"}";
    return json_response(result.ok ? 200 : 400, out.str());
}

} // namespace cuddle
