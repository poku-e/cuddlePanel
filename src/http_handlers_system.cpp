#include "http.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace cuddle {
namespace {

struct SystemAuditEvent {
    std::string timestamp;
    std::string actor;
    std::string target;
    std::string action;
    std::string detail;
};

std::mutex system_audit_mutex;

std::string system_audit_log_path() {
    const char* configured = std::getenv("CUDDLEPANEL_SYSTEM_AUDIT_LOG");
    return configured && *configured ? configured : "data/system_account_audit.log";
}

std::string timestamp_utc() {
    const auto now = std::time(nullptr);
    std::tm utc_time{};
#if defined(_WIN32)
    gmtime_s(&utc_time, &now);
#else
    gmtime_r(&now, &utc_time);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc_time);
    return buffer;
}

std::string sanitize_audit_field(std::string value) {
    for (char& ch : value) {
        if (ch == '\t' || ch == '\n' || ch == '\r') {
            ch = ' ';
        }
    }
    return value;
}

void append_system_audit_event(const std::string& actor,
                               const std::string& target,
                               const std::string& action,
                               const std::string& detail) {
    std::lock_guard<std::mutex> lock(system_audit_mutex);
    const auto path = std::filesystem::path(system_audit_log_path());
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    std::ofstream file(path, std::ios::app);
    if (!file.good()) {
        return;
    }
    file << sanitize_audit_field(timestamp_utc()) << '\t'
         << sanitize_audit_field(actor) << '\t'
         << sanitize_audit_field(target) << '\t'
         << sanitize_audit_field(action) << '\t'
         << sanitize_audit_field(detail) << '\n';
}

std::vector<SystemAuditEvent> load_system_audit_events(const std::string& target) {
    std::lock_guard<std::mutex> lock(system_audit_mutex);
    std::ifstream file(system_audit_log_path());
    std::vector<SystemAuditEvent> events;
    std::string line;
    while (std::getline(file, line)) {
        std::stringstream stream(line);
        std::string timestamp;
        std::string actor;
        std::string target_name;
        std::string action;
        std::string detail;
        if (!std::getline(stream, timestamp, '\t') ||
            !std::getline(stream, actor, '\t') ||
            !std::getline(stream, target_name, '\t') ||
            !std::getline(stream, action, '\t') ||
            !std::getline(stream, detail)) {
            continue;
        }
        if (target_name != target) {
            continue;
        }
        events.push_back({timestamp, actor, target_name, action, detail});
    }
    std::reverse(events.begin(), events.end());
    return events;
}

std::string system_users_json(const SystemAdmin& system_admin) {
    std::ostringstream out;
    out << "{\"users\":[";
    bool first = true;
    for (const auto& user : system_admin.users()) {
        if (!first) out << ",";
        first = false;
        out << "{\"username\":\"" << json_escape(user.username)
            << "\",\"uid\":" << user.uid
            << ",\"gid\":" << user.gid
            << ",\"comment\":\"" << json_escape(user.comment)
            << "\",\"home\":\"" << json_escape(user.home)
            << "\",\"shell\":\"" << json_escape(user.shell)
            << "\",\"primary_group\":\"" << json_escape(user.primary_group)
            << "\",\"secondary_groups\":[";
        bool first_group = true;
        for (const auto& group : user.secondary_groups) {
            if (!first_group) out << ",";
            first_group = false;
            out << "\"" << json_escape(group) << "\"";
        }
        out << "]"
            << ",\"system_account\":" << (user.system_account ? "true" : "false")
            << ",\"login_user\":" << (user.login_user ? "true" : "false")
            << ",\"in_sudo\":" << (user.in_sudo ? "true" : "false")
            << ",\"locked\":" << (user.locked ? "true" : "false")
            << ",\"password_change_required\":" << (user.password_change_required ? "true" : "false")
            << ",\"expires_on\":\"" << json_escape(user.expires_on)
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

std::vector<std::string> parse_group_list(const std::string& raw) {
    std::vector<std::string> groups;
    std::stringstream stream(raw);
    std::string item;
    while (std::getline(stream, item, ',')) {
        while (!item.empty() && std::isspace(static_cast<unsigned char>(item.front()))) {
            item.erase(item.begin());
        }
        while (!item.empty() && std::isspace(static_cast<unsigned char>(item.back()))) {
            item.pop_back();
        }
        if (!item.empty()) {
            groups.push_back(item);
        }
    }
    return groups;
}

std::string system_user_json(const SystemAccountRecord& user, const SystemAdmin& system_admin) {
    std::ostringstream out;
    out << "{\"user\":{"
        << "\"username\":\"" << json_escape(user.username)
        << "\",\"uid\":" << user.uid
        << ",\"gid\":" << user.gid
        << ",\"comment\":\"" << json_escape(user.comment)
        << "\",\"home\":\"" << json_escape(user.home)
        << "\",\"shell\":\"" << json_escape(user.shell)
        << "\",\"primary_group\":\"" << json_escape(user.primary_group)
        << "\",\"secondary_groups\":[";
    bool first_group = true;
    for (const auto& group : user.secondary_groups) {
        if (!first_group) out << ",";
        first_group = false;
        out << "\"" << json_escape(group) << "\"";
    }
    out << "]"
        << ",\"system_account\":" << (user.system_account ? "true" : "false")
        << ",\"login_user\":" << (user.login_user ? "true" : "false")
        << ",\"in_sudo\":" << (user.in_sudo ? "true" : "false")
        << ",\"locked\":" << (user.locked ? "true" : "false")
        << ",\"password_change_required\":" << (user.password_change_required ? "true" : "false")
        << ",\"expires_on\":\"" << json_escape(user.expires_on)
        << "\"},\"allowedRoots\":[";
    bool first_root = true;
    for (const auto& root : system_admin.allowed_path_roots()) {
        if (!first_root) out << ",";
        first_root = false;
        out << "\"" << json_escape(root) << "\"";
    }
    out << "]}";
    return out.str();
}

std::string system_audit_json(const std::vector<SystemAuditEvent>& events) {
    std::ostringstream out;
    out << "{\"events\":[";
    bool first = true;
    for (const auto& event : events) {
        if (!first) out << ",";
        first = false;
        out << "{\"timestamp\":\"" << json_escape(event.timestamp)
            << "\",\"actor\":\"" << json_escape(event.actor)
            << "\",\"target\":\"" << json_escape(event.target)
            << "\",\"action\":\"" << json_escape(event.action)
            << "\",\"detail\":\"" << json_escape(event.detail)
            << "\"}";
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
        if (result.ok) {
            append_system_audit_event(*ctx.username,
                                      form["username"],
                                      "create",
                                      system_account ? "created system account" : "created login account");
        }
        std::ostringstream out;
        out << "{\"ok\":" << (result.ok ? "true" : "false")
            << ",\"output\":\"" << json_escape(result.output) << "\"}";
        return json_response(result.ok ? 200 : 400, out.str());
    }
    return json_response(404, "{\"error\":\"not found\"}");
}

HttpResponse handle_system_user_detail(const RequestContext& ctx, const std::string& username) {
    if (ctx.request.method != "GET") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("system", PermissionLevel::View)) return *err;
    const auto user = ctx.system_admin.find_user(username);
    if (!user) {
        return json_response(404, "{\"error\":\"unknown user\"}");
    }
    return json_response(200, system_user_json(*user, ctx.system_admin));
}

HttpResponse handle_system_user_update(const RequestContext& ctx, const std::string& username) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("system", PermissionLevel::Manage)) return *err;
    auto form = parse_form(ctx.request.body);
    const auto result = ctx.system_admin.update_user(
        username,
        form["shell"],
        form["home"],
        form["move_home"] == "on",
        form["comment"],
        form["primary_group"],
        parse_group_list(form["secondary_groups"]));
    if (result.ok) {
        std::ostringstream detail;
        detail << "shell=" << form["shell"]
               << ", home=" << form["home"]
               << ", primary_group=" << form["primary_group"];
        append_system_audit_event(*ctx.username, username, "profile_saved", detail.str());
    }
    std::ostringstream out;
    out << "{\"ok\":" << (result.ok ? "true" : "false")
        << ",\"output\":\"" << json_escape(result.output) << "\"}";
    return json_response(result.ok ? 200 : 400, out.str());
}

HttpResponse handle_system_user_security(const RequestContext& ctx, const std::string& username) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("system", PermissionLevel::Manage)) return *err;
    auto form = parse_form(ctx.request.body);
    const auto result = ctx.system_admin.update_user_security(
        username,
        form["password"],
        !form["password"].empty(),
        form["force_password_change"] == "on",
        form["clear_expiration"] == "on",
        form["expires_on"]);
    if (result.ok) {
        std::ostringstream detail;
        detail << (form["password"].empty() ? "password unchanged" : "password reset")
               << ", force_change=" << (form["force_password_change"] == "on" ? "yes" : "no")
               << ", expiration=" << (form["clear_expiration"] == "on" ? "cleared" : form["expires_on"]);
        append_system_audit_event(*ctx.username, username, "security_saved", detail.str());
    }
    std::ostringstream out;
    out << "{\"ok\":" << (result.ok ? "true" : "false")
        << ",\"output\":\"" << json_escape(result.output) << "\"}";
    return json_response(result.ok ? 200 : 400, out.str());
}

HttpResponse handle_system_user_action(const RequestContext& ctx, const std::string& username) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("system", PermissionLevel::Manage)) return *err;
    auto form = parse_form(ctx.request.body);
    const bool delete_home = form["delete_home"] == "on";
    const auto result = ctx.system_admin.run_user_action(username, form["action"], delete_home);
    if (result.ok) {
        append_system_audit_event(*ctx.username,
                                  username,
                                  form["action"],
                                  delete_home ? "delete_home=yes" : "delete_home=no");
    }
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
        out << "{\"ok\":" << (result.ok ? "true" : "false")
            << ",\"username\":\"" << json_escape(username) << "\""
            << ",\"content\":\"" << json_escape(content) << "\""
            << ",\"output\":\"" << json_escape(result.output) << "\"}";
        return json_response(result.ok ? 200 : 400, out.str());
    }
    if (ctx.request.method == "POST") {
        auto form = parse_form(ctx.request.body);
        const auto result = ctx.system_admin.write_authorized_keys(username, form["content"]);
        if (result.ok) {
            append_system_audit_event(*ctx.username, username, "authorized_keys_saved", "updated SSH authorized_keys");
        }
        std::ostringstream out;
        out << "{\"ok\":" << (result.ok ? "true" : "false")
            << ",\"output\":\"" << json_escape(result.output) << "\"}";
        return json_response(result.ok ? 200 : 400, out.str());
    }
    return json_response(404, "{\"error\":\"not found\"}");
}

HttpResponse handle_system_user_audit(const RequestContext& ctx, const std::string& username) {
    if (ctx.request.method != "GET") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("system", PermissionLevel::View)) return *err;
    if (!ctx.system_admin.find_user(username)) {
        const auto events = load_system_audit_events(username);
        if (events.empty()) {
            return json_response(404, "{\"error\":\"unknown user\"}");
        }
    }
    return json_response(200, system_audit_json(load_system_audit_events(username)));
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
    if (result.ok && valid_system_username(form["audit_user"])) {
        std::ostringstream detail;
        detail << "path=" << form["path"]
               << ", recursive=" << (recursive ? "yes" : "no");
        if (form["action"] == "chown") {
            detail << ", owner=" << form["owner"] << ", group=" << form["group"];
        } else {
            detail << ", mode=" << form["mode"];
        }
        append_system_audit_event(*ctx.username, form["audit_user"], form["action"], detail.str());
    }
    std::ostringstream out;
    out << "{\"ok\":" << (result.ok ? "true" : "false")
        << ",\"output\":\"" << json_escape(result.output) << "\"}";
    return json_response(result.ok ? 200 : 400, out.str());
}

} // namespace cuddle
