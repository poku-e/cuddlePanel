#include "http.h"

#include "auth.h"
#include "deploy_runner.h"
#include "nginx_store.h"
#include "service_store.h"
#include "template.h"
#include "totp.h"

#include <arpa/inet.h>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <netinet/in.h>
#include <signal.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace cuddle {
namespace {

std::string status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 409: return "Conflict";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

std::string cookie_value(const HttpRequest& request, const std::string& name) {
    auto found = request.headers.find("cookie");
    if (found == request.headers.end()) {
        return {};
    }
    std::stringstream stream(found->second);
    std::string item;
    while (std::getline(stream, item, ';')) {
        while (!item.empty() && item.front() == ' ') {
            item.erase(item.begin());
        }
        auto pos = item.find('=');
        if (pos != std::string::npos && item.substr(0, pos) == name) {
            return item.substr(pos + 1);
        }
    }
    return {};
}

std::string session_token(const HttpRequest& request) {
    return cookie_value(request, "cp_session");
}

std::string page_shell(const std::string& title, const std::string& body) {
    return "<section class=\"cp-panel\"><h1>" + html_escape(title) + "</h1>" + body + "</section>";
}

std::string template_panel(const std::string& title,
                           const std::string& path,
                           const std::map<std::string, std::string>& values = {}) {
    return page_shell(title, render_template(path, values));
}

std::map<std::string, PermissionLevel> parse_permission_map(const std::string& raw) {
    std::map<std::string, PermissionLevel> permissions;
    std::stringstream stream(raw);
    std::string item;
    while (std::getline(stream, item, ',')) {
        auto pos = item.find(':');
        if (pos == std::string::npos) {
            continue;
        }
        auto page = item.substr(0, pos);
        auto level = permission_from_string(item.substr(pos + 1));
        if (known_pages().count(page) > 0 && level != PermissionLevel::None) {
            permissions[page] = level;
        }
    }
    return permissions;
}

std::string users_json(const std::vector<User>& users) {
    std::ostringstream out;
    out << "{\"users\":[";
    bool first_user = true;
    for (const auto& user : users) {
        if (!first_user) {
            out << ",";
        }
        first_user = false;
        out << "{\"username\":\"" << json_escape(user.username)
            << "\",\"role\":\"" << json_escape(user.role)
            << "\",\"permissions\":{";
        bool first_permission = true;
        for (const auto& page : known_pages()) {
            auto found = user.permissions.find(page);
            if (found == user.permissions.end()) {
                continue;
            }
            if (!first_permission) {
                out << ",";
            }
            first_permission = false;
            out << "\"" << json_escape(page) << "\":\"" << json_escape(permission_to_string(found->second)) << "\"";
        }
        out << "}}";
    }
    out << "]}";
    return out.str();
}

std::string services_json(const std::vector<ServiceEntry>& services) {
    std::ostringstream out;
    out << "{\"services\":[";
    bool first_service = true;
    for (const auto& service : services) {
        if (!first_service) {
            out << ",";
        }
        first_service = false;
        const auto status = query_service_status(service.unit);
        out << "{\"name\":\"" << json_escape(service.name)
            << "\",\"unit\":\"" << json_escape(service.unit)
            << "\",\"description\":\"" << json_escape(service.description)
            << "\",\"state\":\"" << json_escape(status.state)
            << "\",\"detail\":\"" << json_escape(status.detail)
            << "\"}";
    }
    out << "]}";
    return out.str();
}

std::string nginx_sites_json(const NginxStore& store) {
    std::ostringstream out;
    out << "{\"sites\":[";
    bool first_site = true;
    for (const auto& site : store.sites()) {
        auto record = store.read_site(site.name);
        if (!record) {
            continue;
        }
        if (!first_site) {
            out << ",";
        }
        first_site = false;
        out << "{\"name\":\"" << json_escape(record->name)
            << "\",\"filename\":\"" << json_escape(record->filename)
            << "\",\"description\":\"" << json_escape(record->description)
            << "\",\"content\":\"" << json_escape(record->content)
            << "\",\"enabled\":" << (record->enabled ? "true" : "false")
            << "}";
    }
    out << "],\"availableDir\":\"" << json_escape(store.available_dir())
        << "\",\"enabledDir\":\"" << json_escape(store.enabled_dir()) << "\"}";
    return out.str();
}

std::string system_users_json(const SystemAdmin& system_admin) {
    std::ostringstream out;
    out << "{\"users\":[";
    bool first_user = true;
    for (const auto& user : system_admin.users()) {
        if (!first_user) {
            out << ",";
        }
        first_user = false;
        out << "{\"username\":\"" << json_escape(user.username)
            << "\",\"uid\":" << user.uid
            << ",\"gid\":" << user.gid
            << ",\"home\":\"" << json_escape(user.home)
            << "\",\"shell\":\"" << json_escape(user.shell)
            << "\",\"system_account\":" << (user.system_account ? "true" : "false")
            << ",\"login_user\":" << (user.login_user ? "true" : "false")
            << ",\"in_sudo\":" << (user.in_sudo ? "true" : "false")
            << ",\"locked\":" << (user.locked ? "true" : "false")
            << "}";
    }
    out << "],\"allowedRoots\":[";
    bool first_root = true;
    for (const auto& root : system_admin.allowed_path_roots()) {
        if (!first_root) {
            out << ",";
        }
        first_root = false;
        out << "\"" << json_escape(root) << "\"";
    }
    out << "]}";
    return out.str();
}

std::string read_request(int client) {
    std::string request;
    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos) {
        ssize_t read_count = recv(client, buffer, sizeof(buffer), 0);
        if (read_count <= 0) {
            return request;
        }
        request.append(buffer, static_cast<size_t>(read_count));
        if (request.size() > 1024 * 1024) {
            return {};
        }
    }

    auto header_end = request.find("\r\n\r\n");
    auto length_pos = request.find("Content-Length:");
    size_t content_length = 0;
    if (length_pos != std::string::npos && length_pos < header_end) {
        content_length = static_cast<size_t>(std::stoul(request.substr(length_pos + 15)));
    }
    while (request.size() < header_end + 4 + content_length) {
        ssize_t read_count = recv(client, buffer, sizeof(buffer), 0);
        if (read_count <= 0) {
            break;
        }
        request.append(buffer, static_cast<size_t>(read_count));
    }
    return request;
}

HttpRequest parse_request(const std::string& raw) {
    HttpRequest request;
    auto header_end = raw.find("\r\n\r\n");
    std::stringstream head(raw.substr(0, header_end));
    head >> request.method >> request.path;

    std::string line;
    std::getline(head, line);
    while (std::getline(head, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        auto pos = line.find(':');
        if (pos == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, pos);
        for (char& c : key) {
            c = static_cast<char>(std::tolower(c));
        }
        std::string value = line.substr(pos + 1);
        while (!value.empty() && value.front() == ' ') {
            value.erase(value.begin());
        }
        request.headers[key] = value;
    }
    if (header_end != std::string::npos) {
        request.body = raw.substr(header_end + 4);
    }
    return request;
}

std::string serialize_response(const HttpResponse& response) {
    std::ostringstream out;
    out << "HTTP/1.1 " << response.status << ' ' << status_text(response.status) << "\r\n";
    out << "Content-Type: " << response.content_type << "\r\n";
    out << "Content-Length: " << response.body.size() << "\r\n";
    out << "X-Content-Type-Options: nosniff\r\n";
    out << "Referrer-Policy: no-referrer\r\n";
    out << "Connection: close\r\n";
    for (const auto& [key, value] : response.headers) {
        out << key << ": " << value << "\r\n";
    }
    out << "\r\n" << response.body;
    return out.str();
}

}

std::string SessionStore::create(const std::string& username, bool pending_totp_setup) {
    auto token = random_token();
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[token] = SessionState{username, pending_totp_setup, 0};
    return token;
}

std::optional<std::string> SessionStore::username_for(const std::string& token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = sessions_.find(token);
    if (found == sessions_.end()) {
        return std::nullopt;
    }
    return found->second.username;
}

std::optional<SessionState> SessionStore::session_for(const std::string& token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = sessions_.find(token);
    if (found == sessions_.end()) {
        return std::nullopt;
    }
    return found->second;
}

bool SessionStore::pending_totp_setup(const std::string& token) const {
    auto session = session_for(token);
    return session && session->pending_totp_setup;
}

void SessionStore::mark_totp_setup_complete(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = sessions_.find(token);
    if (found != sessions_.end()) {
        found->second.pending_totp_setup = false;
    }
}

void SessionStore::mark_terminal_totp_verified(const std::string& token, std::time_t verified_at) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = sessions_.find(token);
    if (found != sessions_.end()) {
        found->second.terminal_totp_verified_at = verified_at;
    }
}

bool SessionStore::terminal_totp_recently_verified(const std::string& token,
                                                   std::time_t now,
                                                   std::time_t max_age_seconds) const {
    auto session = session_for(token);
    return session &&
           session->terminal_totp_verified_at > 0 &&
           (now - session->terminal_totp_verified_at) <= max_age_seconds;
}

void SessionStore::erase(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(token);
}

App::App(UserStore& users,
         ServiceStore& services,
         NginxStore& nginx,
         SystemAdmin& system_admin,
         TerminalManager& terminal,
         SessionStore& sessions)
    : users_(users),
      services_(services),
      nginx_(nginx),
      system_admin_(system_admin),
      terminal_(terminal),
      sessions_(sessions) {}

HttpResponse App::handle(const HttpRequest& request) {
    if (request.method == "GET" && request.path == "/") {
        return redirect(users_.has_users() ? "/login" : "/onboarding");
    }
    if (request.method == "GET" && request.path == "/onboarding") {
        return users_.has_users() ? redirect("/login") : page("templates/onboarding.html");
    }
    if (request.method == "GET" && request.path == "/login") {
        return users_.has_users() ? page("templates/login.html") : redirect("/onboarding");
    }
    if (request.method == "GET" && request.path == "/2fa/setup") {
        if (!current_user(request)) {
            return redirect("/login");
        }
        if (!sessions_.pending_totp_setup(session_token(request))) {
            return redirect("/dashboard");
        }
        return page("templates/totp_setup.html");
    }
    if (request.method == "GET" && request.path == "/dashboard") {
        if (!current_user(request)) {
            return redirect("/login");
        }
        if (sessions_.pending_totp_setup(session_token(request))) {
            return redirect("/2fa/setup");
        }
        return page("templates/dashboard.html");
    }
    if (request.method == "GET" && request.path.rfind("/partials/", 0) == 0) {
        return page("templates" + request.path + ".html");
    }
    if (request.method == "GET" && request.path.rfind("/static/", 0) == 0) {
        auto path = static_asset_path(request.path);
        auto body = read_file(path);
        if (!body) {
            return json(404, "{\"error\":\"not found\"}");
        }
        HttpResponse response;
        response.content_type = request.path.find(".css") != std::string::npos ? "text/css" : "application/javascript";
        response.body = *body;
        return response;
    }
    if (request.method == "POST" && request.path == "/api/onboarding") {
        if (users_.has_users()) {
            return json(409, "{\"error\":\"onboarding already completed\"}");
        }
        auto form = parse_form(request.body);
        if (!users_.create_superuser(form["username"], form["password"])) {
            return json(400, "{\"error\":\"invalid username or password\"}");
        }
        auto token = sessions_.create(form["username"], true);
        HttpResponse response = json(200, "{\"ok\":true,\"next\":\"/2fa/setup\"}");
        std::string secure = std::getenv("CUDDLEPANEL_SECURE_COOKIES") ? "; Secure" : "";
        response.headers["Set-Cookie"] = "cp_session=" + token + "; Path=/; HttpOnly; SameSite=Strict" + secure;
        return response;
    }
    if (request.method == "POST" && request.path == "/api/login") {
        auto form = parse_form(request.body);
        auto user = users_.authenticate(form["username"], form["password"]);
        if (!user) {
            return json(401, "{\"error\":\"invalid credentials\"}");
        }
        if (user->totp_secret.empty()) {
            users_.set_totp_secret(user->username, generate_totp_secret());
            user = users_.find_user(user->username);
        }
        const bool pending_setup = !user->totp_confirmed;
        auto token = sessions_.create(user->username, pending_setup);
        HttpResponse response = json(200, pending_setup ? "{\"ok\":true,\"next\":\"/2fa/setup\"}" : "{\"ok\":true,\"next\":\"/dashboard\"}");
        std::string secure = std::getenv("CUDDLEPANEL_SECURE_COOKIES") ? "; Secure" : "";
        response.headers["Set-Cookie"] = "cp_session=" + token + "; Path=/; HttpOnly; SameSite=Strict" + secure;
        return response;
    }
    if (request.method == "POST" && request.path == "/api/logout") {
        sessions_.erase(cookie_value(request, "cp_session"));
        HttpResponse response = json(200, "{\"ok\":true}");
        response.headers["Set-Cookie"] = "cp_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict";
        return response;
    }

    const auto token = session_token(request);
    if (!token.empty() && sessions_.pending_totp_setup(token)) {
        const bool allowed_during_setup =
            (request.method == "GET" && request.path == "/2fa/setup") ||
            (request.method == "GET" && request.path == "/api/2fa/setup-info") ||
            (request.method == "POST" && request.path == "/api/2fa/confirm") ||
            (request.method == "POST" && request.path == "/api/logout") ||
            (request.method == "GET" && request.path.rfind("/static/", 0) == 0);
        if (!allowed_during_setup) {
            return json(403, "{\"error\":\"complete 2fa setup first\"}");
        }
    }

    if (request.method == "GET" && request.path.rfind("/api/page/", 0) == 0) {
        return api_page(request, request.path.substr(10));
    }
    if (request.path == "/api/users") {
        return api_users(request);
    }
    if (request.path == "/api/services") {
        return api_services(request);
    }
    if (request.path == "/api/system/users") {
        return api_system_users(request);
    }
    if (request.path == "/api/system/path-action") {
        return api_system_path_action(request);
    }
    if (request.path == "/api/nginx/sites") {
        return api_nginx_sites(request);
    }
    if (request.method == "GET" && request.path == "/api/2fa/setup-info") {
        return api_totp_setup_info(request);
    }
    if (request.method == "POST" && request.path == "/api/2fa/confirm") {
        return api_totp_confirm(request);
    }
    if (request.method == "POST" && request.path == "/api/2fa/verify-terminal") {
        return api_terminal_totp_verify(request);
    }
    if (request.method == "POST" && request.path == "/api/terminal/session") {
        return api_terminal_create(request);
    }
    if (request.path == "/api/deploy/run") {
        return api_deploy_run(request);
    }
    if (request.path.rfind("/api/users/", 0) == 0) {
        const std::string base = "/api/users/";
        const std::string remainder = request.path.substr(base.size());
        const auto permissions_suffix = std::string("/permissions");
        if (remainder.size() > permissions_suffix.size() &&
            remainder.rfind(permissions_suffix) == remainder.size() - permissions_suffix.size()) {
            return api_user_permissions(request, url_decode(remainder.substr(0, remainder.size() - permissions_suffix.size())));
        }
        return api_delete_user(request, url_decode(remainder));
    }
    if (request.path.rfind("/api/services/", 0) == 0) {
        const std::string base = "/api/services/";
        const std::string remainder = request.path.substr(base.size());
        const auto action_suffix = std::string("/action");
        if (remainder.size() > action_suffix.size() &&
            remainder.rfind(action_suffix) == remainder.size() - action_suffix.size()) {
            return api_service_action(request, url_decode(remainder.substr(0, remainder.size() - action_suffix.size())));
        }
        return api_update_service(request, url_decode(remainder));
    }
    if (request.path.rfind("/api/system/users/", 0) == 0) {
        const std::string base = "/api/system/users/";
        const std::string remainder = request.path.substr(base.size());
        const auto keys_suffix = std::string("/authorized-keys");
        const auto action_suffix = std::string("/action");
        if (remainder.size() > keys_suffix.size() &&
            remainder.rfind(keys_suffix) == remainder.size() - keys_suffix.size()) {
            return api_system_authorized_keys(request, url_decode(remainder.substr(0, remainder.size() - keys_suffix.size())));
        }
        if (remainder.size() > action_suffix.size() &&
            remainder.rfind(action_suffix) == remainder.size() - action_suffix.size()) {
            return api_system_user_action(request, url_decode(remainder.substr(0, remainder.size() - action_suffix.size())));
        }
    }
    if (request.path.rfind("/api/nginx/sites/", 0) == 0) {
        const std::string base = "/api/nginx/sites/";
        const std::string remainder = request.path.substr(base.size());
        const auto action_suffix = std::string("/action");
        if (remainder.size() > action_suffix.size() &&
            remainder.rfind(action_suffix) == remainder.size() - action_suffix.size()) {
            return api_nginx_action(request, url_decode(remainder.substr(0, remainder.size() - action_suffix.size())));
        }
        return api_update_nginx_site(request, url_decode(remainder));
    }
    if (request.path.rfind("/api/terminal/session/", 0) == 0) {
        const std::string base = "/api/terminal/session/";
        const std::string remainder = request.path.substr(base.size());
        const auto read_suffix = std::string("/read");
        const auto write_suffix = std::string("/write");
        const auto resize_suffix = std::string("/resize");
        const auto close_suffix = std::string("/close");
        if (remainder.size() > read_suffix.size() &&
            remainder.rfind(read_suffix) == remainder.size() - read_suffix.size()) {
            return api_terminal_read(request, url_decode(remainder.substr(0, remainder.size() - read_suffix.size())));
        }
        if (remainder.size() > write_suffix.size() &&
            remainder.rfind(write_suffix) == remainder.size() - write_suffix.size()) {
            return api_terminal_write(request, url_decode(remainder.substr(0, remainder.size() - write_suffix.size())));
        }
        if (remainder.size() > resize_suffix.size() &&
            remainder.rfind(resize_suffix) == remainder.size() - resize_suffix.size()) {
            return api_terminal_resize(request, url_decode(remainder.substr(0, remainder.size() - resize_suffix.size())));
        }
        if (remainder.size() > close_suffix.size() &&
            remainder.rfind(close_suffix) == remainder.size() - close_suffix.size()) {
            return api_terminal_close(request, url_decode(remainder.substr(0, remainder.size() - close_suffix.size())));
        }
    }
    return json(404, "{\"error\":\"not found\"}");
}

HttpResponse App::page(const std::string& file) const {
    HttpResponse response;
    response.body = render_template(file);
    return response;
}

HttpResponse App::json(int status, const std::string& body) const {
    HttpResponse response;
    response.status = status;
    response.content_type = "application/json; charset=utf-8";
    response.body = body;
    return response;
}

HttpResponse App::redirect(const std::string& location) const {
    HttpResponse response;
    response.status = 302;
    response.headers["Location"] = location;
    return response;
}

std::optional<std::string> App::current_user(const HttpRequest& request) const {
    auto token = cookie_value(request, "cp_session");
    if (token.empty()) {
        return std::nullopt;
    }
    return sessions_.username_for(token);
}

HttpResponse App::api_page(const HttpRequest& request, const std::string& page_name) const {
    auto username = current_user(request);
    if (!username) {
        return json(401, "{\"error\":\"login required\"}");
    }
    if (!users_.has_permission(*username, page_name, PermissionLevel::View)) {
        return json(403, "{\"error\":\"permission denied\"}");
    }

    HttpResponse response;
    response.content_type = "text/html; charset=utf-8";
    if (page_name == "dashboard") {
        response.body = template_panel("Dashboard", "templates/pages/dashboard.html");
    } else if (page_name == "users") {
        const bool can_manage_users = users_.has_permission(*username, "users", PermissionLevel::Manage);
        response.body = template_panel("Users",
                                       "templates/pages/users.html",
                                       {
                                           {"can_manage_users", can_manage_users ? "1" : "0"},
                                           {"disabled_attr", can_manage_users ? "" : " disabled"},
                                           {"view_only_note", can_manage_users
            ? ""
                                                               : "<p class=\"small text-secondary mt-3 mb-0\">You have view access to this page, but user changes require `users:manage`.</p>"}
                                       });
    } else if (page_name == "services") {
        const bool can_manage_services = users_.has_permission(*username, "services", PermissionLevel::Manage);
        response.body = template_panel("Services",
                                       "templates/pages/services.html",
                                       {
                                           {"can_manage_services", can_manage_services ? "1" : "0"},
                                           {"disabled_attr", can_manage_services ? "" : " disabled"},
                                           {"view_only_note", can_manage_services
            ? ""
                                                                  : "<p class=\"small text-secondary mt-3 mb-0\">You have view access to this page, but service changes require `services:manage`.</p>"}
                                       });
    } else if (page_name == "system") {
        const bool can_manage_system = users_.has_permission(*username, "system", PermissionLevel::Manage);
        std::ostringstream roots_note;
        const auto roots = system_admin_.allowed_path_roots();
        for (size_t i = 0; i < roots.size(); ++i) {
            if (i > 0) {
                roots_note << ", ";
            }
            roots_note << "<code>" << html_escape(roots[i]) << "</code>";
        }
        response.body = template_panel("System Administration",
                                       "templates/pages/system.html",
                                       {
                                           {"can_manage_system", can_manage_system ? "1" : "0"},
                                           {"disabled_attr", can_manage_system ? "" : " disabled"},
                                           {"view_only_note", can_manage_system
                                                                  ? ""
                                                                  : "<p class=\"small text-secondary mt-3 mb-0\">You have view access to this page, but account and path changes require `system:manage`.</p>"},
                                           {"roots_note", roots_note.str()}
                                       });
    } else if (page_name == "nginx") {
        const bool can_manage_nginx = users_.has_permission(*username, "nginx", PermissionLevel::Manage);
        response.body = template_panel("Nginx Sites",
                                       "templates/pages/nginx.html",
                                       {
                                           {"can_manage_nginx", can_manage_nginx ? "1" : "0"},
                                           {"disabled_attr", can_manage_nginx ? "" : " disabled"},
                                           {"view_only_note", can_manage_nginx
                                                               ? ""
                                                               : "<p class=\"small text-secondary mt-3 mb-0\">You have view access to this page, but nginx changes require `nginx:manage`.</p>"}
                                       });
    } else if (page_name == "terminal") {
        if (!sessions_.terminal_totp_recently_verified(session_token(request), std::time(nullptr), 30 * 60)) {
            response.body = template_panel("Terminal Verification", "templates/pages/terminal_gate.html");
        } else {
            const auto policy = terminal_runtime_policy();
            response.body = template_panel("Terminal",
                                           "templates/pages/terminal.html",
                                           {
                                               {"terminal_user", html_escape(policy.run_as_user)},
                                               {"terminal_workdir", html_escape(policy.working_directory)},
                                               {"terminal_idle_timeout", std::to_string(policy.idle_timeout_seconds)},
                                               {"terminal_max_runtime", std::to_string(policy.max_session_seconds)}
                                           });
        }
    } else if (page_name == "codex") {
        response.body = template_panel("Codex", "templates/pages/codex.html");
    } else if (page_name == "deploy") {
        const bool can_manage_deploy = users_.has_permission(*username, "deploy", PermissionLevel::Manage);
        response.body = template_panel("Deploy Site",
                                       "templates/pages/deploy.html",
                                       {
                                           {"can_manage_deploy", can_manage_deploy ? "1" : "0"},
                                           {"disabled_attr", can_manage_deploy ? "" : " disabled"},
                                           {"view_only_note", can_manage_deploy
                                                                ? ""
                                                                : "<p class=\"small text-secondary mt-3 mb-0\">You have view access to this page, but deploy execution requires `deploy:manage`.</p>"},
                                           {"deploy_script_path", html_escape(deploy_script_path())}
                                       });
    } else {
        return json(404, "{\"error\":\"unknown page\"}");
    }
    return response;
}

HttpResponse App::api_users(const HttpRequest& request) const {
    auto username = current_user(request);
    if (!username) {
        return json(401, "{\"error\":\"login required\"}");
    }

    if (request.method == "GET") {
        if (!users_.has_permission(*username, "users", PermissionLevel::View)) {
            return json(403, "{\"error\":\"permission denied\"}");
        }
        return json(200, users_json(users_.users()));
    }

    if (request.method == "POST") {
        if (!users_.has_permission(*username, "users", PermissionLevel::Manage)) {
            return json(403, "{\"error\":\"permission denied\"}");
        }
        auto form = parse_form(request.body);
        const auto permissions = parse_permission_map(form["permissions"]);
        if (!users_.create_user(form["username"], form["password"], form["role"], permissions)) {
            return json(400, "{\"error\":\"unable to create user\"}");
        }
        return json(200, "{\"ok\":true}");
    }

    return json(404, "{\"error\":\"not found\"}");
}

HttpResponse App::api_user_permissions(const HttpRequest& request, const std::string& username) const {
    auto current = current_user(request);
    if (!current) {
        return json(401, "{\"error\":\"login required\"}");
    }
    if (request.method != "POST") {
        return json(404, "{\"error\":\"not found\"}");
    }
    if (!users_.has_permission(*current, "users", PermissionLevel::Manage)) {
        return json(403, "{\"error\":\"permission denied\"}");
    }

    auto form = parse_form(request.body);
    const auto permissions = parse_permission_map(form["permissions"]);
    if (!users_.update_permissions(username, permissions)) {
        return json(400, "{\"error\":\"unable to update permissions\"}");
    }
    return json(200, "{\"ok\":true}");
}

HttpResponse App::api_delete_user(const HttpRequest& request, const std::string& username) const {
    auto current = current_user(request);
    if (!current) {
        return json(401, "{\"error\":\"login required\"}");
    }
    if (request.method != "DELETE") {
        return json(404, "{\"error\":\"not found\"}");
    }
    if (!users_.has_permission(*current, "users", PermissionLevel::Manage)) {
        return json(403, "{\"error\":\"permission denied\"}");
    }
    if (*current == username) {
        return json(400, "{\"error\":\"cannot delete current session user\"}");
    }
    if (!users_.delete_user(username)) {
        return json(400, "{\"error\":\"unable to delete user\"}");
    }
    return json(200, "{\"ok\":true}");
}

HttpResponse App::api_services(const HttpRequest& request) const {
    auto username = current_user(request);
    if (!username) {
        return json(401, "{\"error\":\"login required\"}");
    }

    if (request.method == "GET") {
        if (!users_.has_permission(*username, "services", PermissionLevel::View)) {
            return json(403, "{\"error\":\"permission denied\"}");
        }
        return json(200, services_json(services_.services()));
    }

    if (request.method == "POST") {
        if (!users_.has_permission(*username, "services", PermissionLevel::Manage)) {
            return json(403, "{\"error\":\"permission denied\"}");
        }
        auto form = parse_form(request.body);
        if (!services_.create_service(form["name"], form["unit"], form["description"])) {
            return json(400, "{\"error\":\"unable to create service\"}");
        }
        return json(200, "{\"ok\":true}");
    }

    return json(404, "{\"error\":\"not found\"}");
}

HttpResponse App::api_system_users(const HttpRequest& request) const {
    auto username = current_user(request);
    if (!username) {
        return json(401, "{\"error\":\"login required\"}");
    }

    if (request.method == "GET") {
        if (!users_.has_permission(*username, "system", PermissionLevel::View)) {
            return json(403, "{\"error\":\"permission denied\"}");
        }
        return json(200, system_users_json(system_admin_));
    }

    if (request.method == "POST") {
        if (!users_.has_permission(*username, "system", PermissionLevel::Manage)) {
            return json(403, "{\"error\":\"permission denied\"}");
        }
        auto form = parse_form(request.body);
        const bool system_account = form["system_account"] == "on";
        const auto result = system_admin_.create_user(form["username"], form["shell"], form["home"], system_account);
        std::ostringstream out;
        out << "{\"ok\":" << (result.ok ? "true" : "false")
            << ",\"output\":\"" << json_escape(result.output) << "\"}";
        return json(result.ok ? 200 : 400, out.str());
    }

    return json(404, "{\"error\":\"not found\"}");
}

HttpResponse App::api_system_user_action(const HttpRequest& request, const std::string& username) const {
    auto current = current_user(request);
    if (!current) {
        return json(401, "{\"error\":\"login required\"}");
    }
    if (request.method != "POST") {
        return json(404, "{\"error\":\"not found\"}");
    }
    if (!users_.has_permission(*current, "system", PermissionLevel::Manage)) {
        return json(403, "{\"error\":\"permission denied\"}");
    }

    auto form = parse_form(request.body);
    const auto result = system_admin_.run_user_action(username, form["action"]);
    std::ostringstream out;
    out << "{\"ok\":" << (result.ok ? "true" : "false")
        << ",\"output\":\"" << json_escape(result.output) << "\"}";
    return json(result.ok ? 200 : 400, out.str());
}

HttpResponse App::api_system_authorized_keys(const HttpRequest& request, const std::string& username) const {
    auto current = current_user(request);
    if (!current) {
        return json(401, "{\"error\":\"login required\"}");
    }
    if (!users_.has_permission(*current, "system", PermissionLevel::Manage)) {
        return json(403, "{\"error\":\"permission denied\"}");
    }

    if (request.method == "GET") {
        std::string content;
        const auto result = system_admin_.read_authorized_keys(username, &content);
        std::ostringstream out;
        out << "{\"ok\":" << (result.ok ? "true" : "false")
            << ",\"username\":\"" << json_escape(username) << "\""
            << ",\"content\":\"" << json_escape(content) << "\""
            << ",\"output\":\"" << json_escape(result.output) << "\"}";
        return json(result.ok ? 200 : 400, out.str());
    }

    if (request.method == "POST") {
        auto form = parse_form(request.body);
        const auto result = system_admin_.write_authorized_keys(username, form["content"]);
        std::ostringstream out;
        out << "{\"ok\":" << (result.ok ? "true" : "false")
            << ",\"output\":\"" << json_escape(result.output) << "\"}";
        return json(result.ok ? 200 : 400, out.str());
    }

    return json(404, "{\"error\":\"not found\"}");
}

HttpResponse App::api_system_path_action(const HttpRequest& request) const {
    auto current = current_user(request);
    if (!current) {
        return json(401, "{\"error\":\"login required\"}");
    }
    if (request.method != "POST") {
        return json(404, "{\"error\":\"not found\"}");
    }
    if (!users_.has_permission(*current, "system", PermissionLevel::Manage)) {
        return json(403, "{\"error\":\"permission denied\"}");
    }

    auto form = parse_form(request.body);
    const bool recursive = form["recursive"] == "on";
    const auto result = system_admin_.run_path_action(form["action"],
                                                      form["path"],
                                                      form["owner"],
                                                      form["group"],
                                                      form["mode"],
                                                      recursive);
    std::ostringstream out;
    out << "{\"ok\":" << (result.ok ? "true" : "false")
        << ",\"output\":\"" << json_escape(result.output) << "\"}";
    return json(result.ok ? 200 : 400, out.str());
}

HttpResponse App::api_update_service(const HttpRequest& request, const std::string& name) const {
    auto username = current_user(request);
    if (!username) {
        return json(401, "{\"error\":\"login required\"}");
    }
    if (request.method != "POST") {
        return json(404, "{\"error\":\"not found\"}");
    }
    if (!users_.has_permission(*username, "services", PermissionLevel::Manage)) {
        return json(403, "{\"error\":\"permission denied\"}");
    }

    auto form = parse_form(request.body);
    const std::string new_name = form["name"].empty() ? name : form["name"];
    if (!services_.update_service(name, new_name, form["unit"], form["description"])) {
        return json(400, "{\"error\":\"unable to update service\"}");
    }
    return json(200, "{\"ok\":true}");
}

HttpResponse App::api_service_action(const HttpRequest& request, const std::string& name) const {
    auto username = current_user(request);
    if (!username) {
        return json(401, "{\"error\":\"login required\"}");
    }
    if (request.method != "POST") {
        return json(404, "{\"error\":\"not found\"}");
    }
    if (!users_.has_permission(*username, "services", PermissionLevel::Manage)) {
        return json(403, "{\"error\":\"permission denied\"}");
    }

    const auto service = services_.find_service(name);
    if (!service) {
        return json(404, "{\"error\":\"service not found\"}");
    }

    auto form = parse_form(request.body);
    const auto result = run_service_action(service->unit, form["action"]);
    std::ostringstream out;
    out << "{\"ok\":" << (result.ok ? "true" : "false")
        << ",\"output\":\"" << json_escape(result.output) << "\"}";
    return json(result.ok ? 200 : 400, out.str());
}

HttpResponse App::api_nginx_sites(const HttpRequest& request) const {
    auto username = current_user(request);
    if (!username) {
        return json(401, "{\"error\":\"login required\"}");
    }

    if (request.method == "GET") {
        if (!users_.has_permission(*username, "nginx", PermissionLevel::View)) {
            return json(403, "{\"error\":\"permission denied\"}");
        }
        return json(200, nginx_sites_json(nginx_));
    }

    if (request.method == "POST") {
        if (!users_.has_permission(*username, "nginx", PermissionLevel::Manage)) {
            return json(403, "{\"error\":\"permission denied\"}");
        }
        auto form = parse_form(request.body);
        if (!nginx_.create_site(form["name"], form["filename"], form["description"], form["content"])) {
            return json(400, "{\"error\":\"unable to create nginx site\"}");
        }
        return json(200, "{\"ok\":true}");
    }

    return json(404, "{\"error\":\"not found\"}");
}

HttpResponse App::api_update_nginx_site(const HttpRequest& request, const std::string& name) const {
    auto username = current_user(request);
    if (!username) {
        return json(401, "{\"error\":\"login required\"}");
    }
    if (request.method != "POST") {
        return json(404, "{\"error\":\"not found\"}");
    }
    if (!users_.has_permission(*username, "nginx", PermissionLevel::Manage)) {
        return json(403, "{\"error\":\"permission denied\"}");
    }

    auto form = parse_form(request.body);
    const std::string new_name = form["name"].empty() ? name : form["name"];
    if (!nginx_.update_site(name, new_name, form["filename"], form["description"], form["content"])) {
        return json(400, "{\"error\":\"unable to update nginx site\"}");
    }
    return json(200, "{\"ok\":true}");
}

HttpResponse App::api_nginx_action(const HttpRequest& request, const std::string& name) const {
    auto username = current_user(request);
    if (!username) {
        return json(401, "{\"error\":\"login required\"}");
    }
    if (request.method != "POST") {
        return json(404, "{\"error\":\"not found\"}");
    }
    if (!users_.has_permission(*username, "nginx", PermissionLevel::Manage)) {
        return json(403, "{\"error\":\"permission denied\"}");
    }

    auto form = parse_form(request.body);
    const std::string action = form["action"];
    NginxActionResult result;
    if (action == "enable") {
        result.ok = nginx_.set_enabled(name, true);
        result.output = result.ok ? "site enabled" : "unable to enable site";
    } else if (action == "disable") {
        result.ok = nginx_.set_enabled(name, false);
        result.output = result.ok ? "site disabled" : "unable to disable site";
    } else if (action == "test") {
        result = nginx_test_config();
    } else if (action == "reload") {
        result = nginx_reload();
    } else {
        return json(400, "{\"error\":\"invalid nginx action\"}");
    }

    std::ostringstream out;
    out << "{\"ok\":" << (result.ok ? "true" : "false")
        << ",\"output\":\"" << json_escape(result.output) << "\"}";
    return json(result.ok ? 200 : 400, out.str());
}

HttpResponse App::api_deploy_run(const HttpRequest& request) const {
    auto username = current_user(request);
    if (!username) {
        return json(401, "{\"error\":\"login required\"}");
    }
    if (request.method != "POST") {
        return json(404, "{\"error\":\"not found\"}");
    }
    if (!users_.has_permission(*username, "deploy", PermissionLevel::Manage)) {
        return json(403, "{\"error\":\"permission denied\"}");
    }

    auto form = parse_form(request.body);
    auto deploy_request = deploy_request_from_form(form);
    if (!deploy_request) {
        return json(400, "{\"error\":\"invalid deploy request\"}");
    }
    const auto result = run_deploy_site(*deploy_request);

    std::ostringstream out;
    out << "{\"ok\":" << (result.ok ? "true" : "false")
        << ",\"output\":\"" << json_escape(result.output) << "\"}";
    return json(result.ok ? 200 : 400, out.str());
}

HttpResponse App::api_totp_setup_info(const HttpRequest& request) const {
    auto username = current_user(request);
    if (!username) {
        return json(401, "{\"error\":\"login required\"}");
    }
    const auto token = session_token(request);
    if (!sessions_.pending_totp_setup(token)) {
        return json(403, "{\"error\":\"2fa setup not required\"}");
    }
    auto user = users_.find_user(*username);
    if (!user || user->totp_secret.empty()) {
        return json(400, "{\"error\":\"missing totp secret\"}");
    }

    const auto uri = build_otpauth_uri("cuddlePanel", user->username, user->totp_secret);
    std::ostringstream out;
    out << "{\"username\":\"" << json_escape(user->username)
        << "\",\"secret\":\"" << json_escape(user->totp_secret)
        << "\",\"uri\":\"" << json_escape(uri) << "\"}";
    return json(200, out.str());
}

HttpResponse App::api_totp_confirm(const HttpRequest& request) const {
    auto username = current_user(request);
    if (!username) {
        return json(401, "{\"error\":\"login required\"}");
    }
    const auto token = session_token(request);
    if (!sessions_.pending_totp_setup(token)) {
        return json(403, "{\"error\":\"2fa setup not required\"}");
    }

    auto user = users_.find_user(*username);
    if (!user || user->totp_secret.empty()) {
        return json(400, "{\"error\":\"missing totp secret\"}");
    }

    auto form = parse_form(request.body);
    if (!verify_totp_code(user->totp_secret, form["code"], std::time(nullptr))) {
        return json(400, "{\"error\":\"invalid otp code\"}");
    }
    if (!users_.confirm_totp(*username)) {
        return json(500, "{\"error\":\"unable to confirm otp\"}");
    }
    sessions_.mark_totp_setup_complete(token);
    return json(200, "{\"ok\":true,\"next\":\"/dashboard\"}");
}

HttpResponse App::api_terminal_totp_verify(const HttpRequest& request) const {
    auto username = current_user(request);
    if (!username) {
        return json(401, "{\"error\":\"login required\"}");
    }
    if (sessions_.pending_totp_setup(session_token(request))) {
        return json(403, "{\"error\":\"complete 2fa setup first\"}");
    }
    auto user = users_.find_user(*username);
    if (!user || user->totp_secret.empty() || !user->totp_confirmed) {
        return json(403, "{\"error\":\"2fa not configured\"}");
    }
    auto form = parse_form(request.body);
    if (!verify_totp_code(user->totp_secret, form["code"], std::time(nullptr))) {
        return json(400, "{\"error\":\"invalid otp code\"}");
    }
    sessions_.mark_terminal_totp_verified(session_token(request), std::time(nullptr));
    return json(200, "{\"ok\":true}");
}

HttpResponse App::api_terminal_create(const HttpRequest& request) const {
    auto username = current_user(request);
    if (!username) {
        return json(401, "{\"error\":\"login required\"}");
    }
    if (request.method != "POST") {
        return json(404, "{\"error\":\"not found\"}");
    }
    if (!users_.has_permission(*username, "terminal", PermissionLevel::Manage)) {
        return json(403, "{\"error\":\"permission denied\"}");
    }
    if (!sessions_.terminal_totp_recently_verified(session_token(request), std::time(nullptr), 30 * 60)) {
        return json(403, "{\"error\":\"terminal otp verification required\"}");
    }
    auto form = parse_form(request.body);
    const std::uint16_t rows = static_cast<std::uint16_t>(std::stoi(form["rows"].empty() ? "24" : form["rows"]));
    const std::uint16_t cols = static_cast<std::uint16_t>(std::stoi(form["cols"].empty() ? "80" : form["cols"]));
    auto session_id = terminal_.create_session(*username, rows, cols);
    if (!session_id) {
        return json(500, "{\"error\":\"unable to create terminal session\"}");
    }
    return json(200, "{\"ok\":true,\"session_id\":\"" + json_escape(*session_id) + "\"}");
}

HttpResponse App::api_terminal_read(const HttpRequest& request, const std::string& session_id) const {
    auto username = current_user(request);
    if (!username) {
        return json(401, "{\"error\":\"login required\"}");
    }
    if (request.method != "POST") {
        return json(404, "{\"error\":\"not found\"}");
    }
    if (!users_.has_permission(*username, "terminal", PermissionLevel::Manage)) {
        return json(403, "{\"error\":\"permission denied\"}");
    }
    if (!sessions_.terminal_totp_recently_verified(session_token(request), std::time(nullptr), 30 * 60)) {
        return json(403, "{\"error\":\"terminal otp verification required\"}");
    }
    auto form = parse_form(request.body);
    const std::uint64_t cursor = form["cursor"].empty() ? 0ULL : static_cast<std::uint64_t>(std::stoull(form["cursor"]));
    auto snapshot = terminal_.read_session(session_id, *username, cursor);
    if (!snapshot) {
        return json(404, "{\"error\":\"terminal session not found\"}");
    }
    std::ostringstream out;
    out << "{\"ok\":true"
        << ",\"data\":\"" << json_escape(snapshot->data) << "\""
        << ",\"cursor\":" << snapshot->cursor
        << ",\"closed\":" << (snapshot->closed ? "true" : "false")
        << ",\"truncated\":" << (snapshot->truncated ? "true" : "false")
        << ",\"exit_code\":" << snapshot->exit_code
        << "}";
    return json(200, out.str());
}

HttpResponse App::api_terminal_write(const HttpRequest& request, const std::string& session_id) const {
    auto username = current_user(request);
    if (!username) {
        return json(401, "{\"error\":\"login required\"}");
    }
    if (request.method != "POST") {
        return json(404, "{\"error\":\"not found\"}");
    }
    if (!users_.has_permission(*username, "terminal", PermissionLevel::Manage)) {
        return json(403, "{\"error\":\"permission denied\"}");
    }
    if (!sessions_.terminal_totp_recently_verified(session_token(request), std::time(nullptr), 30 * 60)) {
        return json(403, "{\"error\":\"terminal otp verification required\"}");
    }
    auto form = parse_form(request.body);
    if (form["data"].size() > 8192) {
        return json(400, "{\"error\":\"terminal input too large\"}");
    }
    if (!terminal_.write_session(session_id, *username, form["data"])) {
        return json(400, "{\"error\":\"unable to write to terminal session\"}");
    }
    return json(200, "{\"ok\":true}");
}

HttpResponse App::api_terminal_resize(const HttpRequest& request, const std::string& session_id) const {
    auto username = current_user(request);
    if (!username) {
        return json(401, "{\"error\":\"login required\"}");
    }
    if (request.method != "POST") {
        return json(404, "{\"error\":\"not found\"}");
    }
    if (!users_.has_permission(*username, "terminal", PermissionLevel::Manage)) {
        return json(403, "{\"error\":\"permission denied\"}");
    }
    if (!sessions_.terminal_totp_recently_verified(session_token(request), std::time(nullptr), 30 * 60)) {
        return json(403, "{\"error\":\"terminal otp verification required\"}");
    }
    auto form = parse_form(request.body);
    const std::uint16_t rows = static_cast<std::uint16_t>(std::stoi(form["rows"].empty() ? "24" : form["rows"]));
    const std::uint16_t cols = static_cast<std::uint16_t>(std::stoi(form["cols"].empty() ? "80" : form["cols"]));
    if (!terminal_.resize_session(session_id, *username, rows, cols)) {
        return json(400, "{\"error\":\"unable to resize terminal session\"}");
    }
    return json(200, "{\"ok\":true}");
}

HttpResponse App::api_terminal_close(const HttpRequest& request, const std::string& session_id) const {
    auto username = current_user(request);
    if (!username) {
        return json(401, "{\"error\":\"login required\"}");
    }
    if (request.method != "POST") {
        return json(404, "{\"error\":\"not found\"}");
    }
    if (!users_.has_permission(*username, "terminal", PermissionLevel::Manage)) {
        return json(403, "{\"error\":\"permission denied\"}");
    }
    if (!sessions_.terminal_totp_recently_verified(session_token(request), std::time(nullptr), 30 * 60)) {
        return json(403, "{\"error\":\"terminal otp verification required\"}");
    }
    if (!terminal_.close_session(session_id, *username)) {
        return json(400, "{\"error\":\"unable to close terminal session\"}");
    }
    return json(200, "{\"ok\":true}");
}

bool run_server(App& app, int port) {
    signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        return false;
    }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || listen(server_fd, 32) < 0) {
        close(server_fd);
        return false;
    }

    std::cout << "cuddlePanel listening on http://127.0.0.1:" << port << std::endl;
    while (true) {
        int client = accept(server_fd, nullptr, nullptr);
        if (client < 0) {
            continue;
        }
        auto raw = read_request(client);
        auto request = parse_request(raw);
        auto response = serialize_response(app.handle(request));
        send(client, response.data(), response.size(), 0);
        close(client);
    }
}

std::map<std::string, std::string> parse_form(const std::string& body) {
    std::map<std::string, std::string> form;
    std::stringstream stream(body);
    std::string item;
    while (std::getline(stream, item, '&')) {
        auto pos = item.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        form[url_decode(item.substr(0, pos))] = url_decode(item.substr(pos + 1));
    }
    return form;
}

std::string url_decode(const std::string& value) {
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+' ) {
            out += ' ';
        } else if (value[i] == '%' && i + 2 < value.size()) {
            char hex[3] = {value[i + 1], value[i + 2], '\0'};
            out += static_cast<char>(std::strtol(hex, nullptr, 16));
            i += 2;
        } else {
            out += value[i];
        }
    }
    return out;
}

std::string html_escape(const std::string& value) {
    std::string out;
    for (char c : value) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string json_escape(const std::string& value) {
    std::string out;
    for (char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

}
