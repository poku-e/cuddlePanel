#include "http.h"

#include "auth.h"
#include "log.h"
#include "template.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cctype>
#include <ctime>
#include <exception>
#include <cstdlib>
#include <cstring>
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
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 500: return "Internal Server Error";
        default:  return "OK";
    }
}

std::optional<size_t> parse_content_length_value(const std::string& raw_value) {
    std::string trimmed = raw_value;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
        trimmed.erase(trimmed.begin());
    }
    while (!trimmed.empty() && (trimmed.back() == '\r' || trimmed.back() == ' ' || trimmed.back() == '\t')) {
        trimmed.pop_back();
    }
    if (trimmed.empty()) { return size_t{0}; }
    for (unsigned char c : trimmed) {
        if (!std::isdigit(c)) { return std::nullopt; }
    }
    try {
        return static_cast<size_t>(std::stoull(trimmed));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string cookie_value(const HttpRequest& request, const std::string& name) {
    auto found = request.headers.find("cookie");
    if (found == request.headers.end()) { return {}; }
    std::stringstream stream(found->second);
    std::string item;
    while (std::getline(stream, item, ';')) {
        while (!item.empty() && item.front() == ' ') { item.erase(item.begin()); }
        auto pos = item.find('=');
        if (pos != std::string::npos && item.substr(0, pos) == name) {
            return item.substr(pos + 1);
        }
    }
    return {};
}

std::string read_request(int client) {
    std::string request;
    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos) {
        ssize_t read_count = recv(client, buffer, sizeof(buffer), 0);
        if (read_count <= 0) {
            if (read_count < 0) { log_errno(LogLevel::Warning, "failed to read request header", errno); }
            return request;
        }
        request.append(buffer, static_cast<size_t>(read_count));
        if (request.size() > 1024 * 1024) {
            log_message(LogLevel::Warning, "dropping request larger than 1 MiB while reading headers");
            return {};
        }
    }

    auto header_end = request.find("\r\n\r\n");
    auto length_pos = request.find("Content-Length:");
    size_t content_length = 0;
    if (length_pos != std::string::npos && length_pos < header_end) {
        const auto line_end = request.find("\r\n", length_pos);
        const auto header_value = request.substr(length_pos + 15, line_end == std::string::npos ? std::string::npos : line_end - (length_pos + 15));
        const auto parsed_length = parse_content_length_value(header_value);
        if (!parsed_length) {
            log_message(LogLevel::Warning, "received request with invalid Content-Length header");
            return {};
        }
        content_length = *parsed_length;
        if (content_length > 1024 * 1024) {
            log_message(LogLevel::Warning, "dropping request body larger than 1 MiB");
            return {};
        }
    }
    while (request.size() < header_end + 4 + content_length) {
        ssize_t read_count = recv(client, buffer, sizeof(buffer), 0);
        if (read_count <= 0) {
            if (read_count < 0) { log_errno(LogLevel::Warning, "failed to read request body", errno); }
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
    const std::string raw_target = request.path;
    const auto query_pos = raw_target.find('?');
    if (query_pos != std::string::npos && query_pos + 1 < raw_target.size()) {
        request.query_string = raw_target.substr(query_pos + 1);
    }
    request.path = normalize_request_path(raw_target);

    std::string line;
    std::getline(head, line);
    while (std::getline(head, line)) {
        if (!line.empty() && line.back() == '\r') { line.pop_back(); }
        auto pos = line.find(':');
        if (pos == std::string::npos) { continue; }
        std::string key = line.substr(0, pos);
        for (char& c : key) { c = static_cast<char>(std::tolower(c)); }
        std::string value = line.substr(pos + 1);
        while (!value.empty() && value.front() == ' ') { value.erase(value.begin()); }
        request.headers[key] = value;
    }
    if (header_end != std::string::npos) { request.body = raw.substr(header_end + 4); }
    return request;
}

std::string serialize_response(const HttpResponse& response) {
    std::ostringstream out;
    out << "HTTP/1.1 " << response.status << ' ' << status_text(response.status) << "\r\n";
    out << "Content-Type: "   << response.content_type << "\r\n";
    out << "Content-Length: " << response.body.size()  << "\r\n";
    out << "X-Content-Type-Options: nosniff\r\n";
    out << "X-Frame-Options: DENY\r\n";
    out << "Referrer-Policy: no-referrer\r\n";
    out << "Cache-Control: no-store\r\n";
    out << "Connection: close\r\n";
    for (const auto& [key, value] : response.headers) { out << key << ": " << value << "\r\n"; }
    out << "\r\n" << response.body;
    return out.str();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Response factories
// ---------------------------------------------------------------------------

HttpResponse json_response(int status, const std::string& body) {
    HttpResponse r;
    r.status = status;
    r.content_type = "application/json; charset=utf-8";
    r.body = body;
    return r;
}

HttpResponse redirect_response(const std::string& location) {
    HttpResponse r;
    r.status = 302;
    r.headers["Location"] = location;
    return r;
}

HttpResponse page_response(const std::string& file) {
    HttpResponse r;
    r.body = render_template(file);
    return r;
}

// ---------------------------------------------------------------------------
// Encoding / parsing utilities
// ---------------------------------------------------------------------------

std::string json_escape(const std::string& value) {
    std::string out;
    static const char hex_digits[] = "0123456789abcdef";
    for (unsigned char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += hex_digits[(c >> 4) & 0x0f];
                    out += hex_digits[c & 0x0f];
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

std::string html_escape(const std::string& value) {
    std::string out;
    for (char c : value) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;        break;
        }
    }
    return out;
}

std::string url_decode(const std::string& value) {
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
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

std::string normalize_request_path(const std::string& raw_path) {
    const auto query_pos = raw_path.find('?');
    if (query_pos == std::string::npos) {
        return raw_path;
    }
    return raw_path.substr(0, query_pos);
}

std::map<std::string, std::string> parse_form(const std::string& body) {
    std::map<std::string, std::string> form;
    std::stringstream stream(body);
    std::string item;
    while (std::getline(stream, item, '&')) {
        auto pos = item.find('=');
        if (pos == std::string::npos) { continue; }
        form[url_decode(item.substr(0, pos))] = url_decode(item.substr(pos + 1));
    }
    return form;
}

// ---------------------------------------------------------------------------
// SessionStore
// ---------------------------------------------------------------------------

std::string SessionStore::create(const std::string& username, bool pending_totp_setup) {
    auto token = random_token();
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[token] = SessionState{username, pending_totp_setup, 0};
    return token;
}

std::optional<std::string> SessionStore::username_for(const std::string& token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = sessions_.find(token);
    if (found == sessions_.end()) { return std::nullopt; }
    return found->second.username;
}

std::optional<SessionState> SessionStore::session_for(const std::string& token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = sessions_.find(token);
    if (found == sessions_.end()) { return std::nullopt; }
    return found->second;
}

bool SessionStore::pending_totp_setup(const std::string& token) const {
    auto session = session_for(token);
    return session && session->pending_totp_setup;
}

void SessionStore::mark_totp_setup_complete(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = sessions_.find(token);
    if (found != sessions_.end()) { found->second.pending_totp_setup = false; }
}

void SessionStore::mark_terminal_totp_verified(const std::string& token, std::time_t verified_at) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = sessions_.find(token);
    if (found != sessions_.end()) { found->second.terminal_totp_verified_at = verified_at; }
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

// ---------------------------------------------------------------------------
// RequestContext helpers
// ---------------------------------------------------------------------------

std::optional<HttpResponse> RequestContext::require_auth() const {
    if (!username) {
        return json_response(401, "{\"error\":\"login required\"}");
    }
    return std::nullopt;
}

std::optional<HttpResponse> RequestContext::require_permission(const std::string& page,
                                                               PermissionLevel level) const {
    if (auto err = require_auth()) { return err; }
    if (!users.has_permission(*username, page, level)) {
        return json_response(403, "{\"error\":\"permission denied\"}");
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Forward declarations for domain handler functions
// (defined in src/http_handlers_*.cpp)
// ---------------------------------------------------------------------------

HttpResponse handle_setup_status(const RequestContext&, const std::string&);
HttpResponse handle_onboarding_post(const RequestContext&, const std::string&);
HttpResponse handle_login_post(const RequestContext&, const std::string&);
HttpResponse handle_logout_post(const RequestContext&, const std::string&);
HttpResponse handle_totp_setup_info(const RequestContext&, const std::string&);
HttpResponse handle_totp_confirm(const RequestContext&, const std::string&);
HttpResponse handle_terminal_totp_verify(const RequestContext&, const std::string&);

HttpResponse handle_api_page(const RequestContext&, const std::string&);
HttpResponse handle_system_user_page(const RequestContext&, const std::string&);
HttpResponse handle_service_page(const RequestContext&, const std::string&);
HttpResponse handle_dashboard_health(const RequestContext&, const std::string&);
HttpResponse handle_dashboard_nginx_autofix(const RequestContext&, const std::string&);

HttpResponse handle_users(const RequestContext&, const std::string&);
HttpResponse handle_user_permissions(const RequestContext&, const std::string&);
HttpResponse handle_delete_user(const RequestContext&, const std::string&);

HttpResponse handle_services(const RequestContext&, const std::string&);
HttpResponse handle_service_detail(const RequestContext&, const std::string&);
HttpResponse handle_service_unit_file(const RequestContext&, const std::string&);
HttpResponse handle_update_service(const RequestContext&, const std::string&);
HttpResponse handle_service_action(const RequestContext&, const std::string&);

HttpResponse handle_system_users(const RequestContext&, const std::string&);
HttpResponse handle_system_user_detail(const RequestContext&, const std::string&);
HttpResponse handle_system_user_update(const RequestContext&, const std::string&);
HttpResponse handle_system_user_security(const RequestContext&, const std::string&);
HttpResponse handle_system_user_action(const RequestContext&, const std::string&);
HttpResponse handle_system_authorized_keys(const RequestContext&, const std::string&);
HttpResponse handle_system_user_audit(const RequestContext&, const std::string&);
HttpResponse handle_system_user_logfiles(const RequestContext&, const std::string&);
HttpResponse handle_system_files_browse(const RequestContext&, const std::string&);
HttpResponse handle_system_files_action(const RequestContext&, const std::string&);
HttpResponse handle_system_path_action(const RequestContext&, const std::string&);

HttpResponse handle_nginx_sites(const RequestContext&, const std::string&);
HttpResponse handle_update_nginx_site(const RequestContext&, const std::string&);
HttpResponse handle_nginx_action(const RequestContext&, const std::string&);

HttpResponse handle_fail2ban_jails(const RequestContext&, const std::string&);
HttpResponse handle_fail2ban_jail_detail(const RequestContext&, const std::string&);
HttpResponse handle_fail2ban_jail_action(const RequestContext&, const std::string&);
HttpResponse handle_fail2ban_ban(const RequestContext&, const std::string&);
HttpResponse handle_fail2ban_unban(const RequestContext&, const std::string&);
HttpResponse handle_fail2ban_whitelist(const RequestContext&, const std::string&);
HttpResponse handle_fail2ban_action(const RequestContext&, const std::string&);
HttpResponse handle_fail2ban_logs(const RequestContext&, const std::string&);

HttpResponse handle_codex_projects(const RequestContext&, const std::string&);
HttpResponse handle_codex_conversations(const RequestContext&, const std::string&);
HttpResponse handle_codex_conversation_read(const RequestContext&, const std::string&);
HttpResponse handle_codex_conversation_transcript(const RequestContext&, const std::string&);
HttpResponse handle_codex_conversation_history(const RequestContext&, const std::string&);
HttpResponse handle_codex_conversation_send(const RequestContext&, const std::string&);
HttpResponse handle_codex_conversation_close(const RequestContext&, const std::string&);
HttpResponse handle_codex_run(const RequestContext&, const std::string&);

HttpResponse handle_deploy_run(const RequestContext&, const std::string&);

HttpResponse handle_terminal_create(const RequestContext&, const std::string&);
HttpResponse handle_terminal_read(const RequestContext&, const std::string&);
HttpResponse handle_terminal_write(const RequestContext&, const std::string&);
HttpResponse handle_terminal_resize(const RequestContext&, const std::string&);
HttpResponse handle_terminal_close(const RequestContext&, const std::string&);

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------

App::App(UserStore& users,
         ServiceStore& services,
         NginxStore& nginx,
         Fail2banStore& fail2ban,
         SystemAdmin& system_admin,
         TerminalManager& terminal,
         CodexProjectStore& codex_projects,
         CodexConversationManager& codex_conversations,
         SessionStore& sessions)
    : users_(users),
      services_(services),
      nginx_(nginx),
            fail2ban_(fail2ban),
      system_admin_(system_admin),
      terminal_(terminal),
      codex_projects_(codex_projects),
      codex_conversations_(codex_conversations),
      sessions_(sessions)
{
    build_routes();
}

void App::build_routes() {
    // auth & setup
    routes_.push_back({"POST", "/api/onboarding",           "",                 true,  handle_onboarding_post});
    routes_.push_back({"POST", "/api/login",                "",                 true,  handle_login_post});
    routes_.push_back({"POST", "/api/logout",               "",                 true,  handle_logout_post});
    routes_.push_back({"GET",  "/api/setup/status",         "",                 true,  handle_setup_status});
    routes_.push_back({"GET",  "/api/2fa/setup-info",       "",                 true,  handle_totp_setup_info});
    routes_.push_back({"POST", "/api/2fa/confirm",          "",                 true,  handle_totp_confirm});
    routes_.push_back({"POST", "/api/2fa/verify-terminal",  "",                 true,  handle_terminal_totp_verify});
    // pages
    routes_.push_back({"GET",  "/api/page/system-user/",     "",                 false, handle_system_user_page});
    routes_.push_back({"GET",  "/api/page/service/",         "",                 false, handle_service_page});
    routes_.push_back({"GET",  "/api/page/",                "",                 false, handle_api_page});
    routes_.push_back({"GET",  "/api/dashboard/health",     "",                 true,  handle_dashboard_health});
    routes_.push_back({"POST", "/api/dashboard/health/nginx/auto-fix", "",       true,  handle_dashboard_nginx_autofix});
    // users
    routes_.push_back({"",     "/api/users",                "",                 true,  handle_users});
    routes_.push_back({"",     "/api/users/",               "/permissions",     false, handle_user_permissions});
    routes_.push_back({"",     "/api/users/",               "",                 false, handle_delete_user});
    // services
    routes_.push_back({"",     "/api/services",             "",                 true,  handle_services});
    routes_.push_back({"GET",  "/api/services/",            "/page",            false, handle_service_page});
    routes_.push_back({"",     "/api/services/",            "/unit-file",       false, handle_service_unit_file});
    routes_.push_back({"GET",  "/api/services/",            "",                 false, handle_service_detail});
    routes_.push_back({"",     "/api/services/",            "/action",          false, handle_service_action});
    routes_.push_back({"",     "/api/services/",            "",                 false, handle_update_service});
    // system
    routes_.push_back({"",     "/api/system/users",         "",                 true,  handle_system_users});
    routes_.push_back({"",     "/api/system/path-action",   "",                 true,  handle_system_path_action});
    routes_.push_back({"POST", "/api/system/files/browse",  "",                 true,  handle_system_files_browse});
    routes_.push_back({"POST", "/api/system/files/action",  "",                 true,  handle_system_files_action});
    routes_.push_back({"GET",  "/api/system/users/",        "/page",            false, handle_system_user_page});
    routes_.push_back({"GET",  "/api/system/users/",        "/audit",           false, handle_system_user_audit});
    routes_.push_back({"GET",  "/api/system/users/",        "/logfiles",        false, handle_system_user_logfiles});
    routes_.push_back({"",     "/api/system/users/",        "/authorized-keys", false, handle_system_authorized_keys});
    routes_.push_back({"",     "/api/system/users/",        "/edit",            false, handle_system_user_update});
    routes_.push_back({"",     "/api/system/users/",        "/security",        false, handle_system_user_security});
    routes_.push_back({"",     "/api/system/users/",        "/action",          false, handle_system_user_action});
    routes_.push_back({"GET",  "/api/system/users/",        "",                 false, handle_system_user_detail});
    // nginx
    routes_.push_back({"",     "/api/nginx/sites",          "",                 true,  handle_nginx_sites});
    routes_.push_back({"",     "/api/nginx/sites/",         "/action",          false, handle_nginx_action});
    routes_.push_back({"",     "/api/nginx/sites/",         "",                 false, handle_update_nginx_site});
    // fail2ban
    routes_.push_back({"GET",  "/api/fail2ban/jails",       "",                 true,  handle_fail2ban_jails});
    routes_.push_back({"POST", "/api/fail2ban/jails/",      "/action",          false, handle_fail2ban_jail_action});
    routes_.push_back({"POST", "/api/fail2ban/jails/",      "/ban",             false, handle_fail2ban_ban});
    routes_.push_back({"POST", "/api/fail2ban/jails/",      "/unban",           false, handle_fail2ban_unban});
    routes_.push_back({"POST", "/api/fail2ban/jails/",      "/whitelist",       false, handle_fail2ban_whitelist});
    routes_.push_back({"GET",  "/api/fail2ban/jails/",      "",                 false, handle_fail2ban_jail_detail});
    routes_.push_back({"POST", "/api/fail2ban/action",      "",                 true,  handle_fail2ban_action});
    routes_.push_back({"GET",  "/api/fail2ban/logs",        "",                 true,  handle_fail2ban_logs});
    // codex
    routes_.push_back({"",     "/api/codex/projects",       "",                 true,  handle_codex_projects});
    routes_.push_back({"",     "/api/codex/conversations",  "",                 true,  handle_codex_conversations});
    routes_.push_back({"POST", "/api/codex/run",            "",                 true,  handle_codex_run});
    routes_.push_back({"",     "/api/codex/conversations/", "/read",            false, handle_codex_conversation_read});
    routes_.push_back({"",     "/api/codex/conversations/", "/transcript",      false, handle_codex_conversation_transcript});
    routes_.push_back({"",     "/api/codex/conversations/", "/history",         false, handle_codex_conversation_history});
    routes_.push_back({"",     "/api/codex/conversations/", "/send",            false, handle_codex_conversation_send});
    routes_.push_back({"",     "/api/codex/conversations/", "/close",           false, handle_codex_conversation_close});
    // deploy
    routes_.push_back({"POST", "/api/deploy/run",           "",                 true,  handle_deploy_run});
    // terminal
    routes_.push_back({"POST", "/api/terminal/session",     "",                 true,  handle_terminal_create});
    routes_.push_back({"",     "/api/terminal/session/",    "/read",            false, handle_terminal_read});
    routes_.push_back({"",     "/api/terminal/session/",    "/write",           false, handle_terminal_write});
    routes_.push_back({"",     "/api/terminal/session/",    "/resize",          false, handle_terminal_resize});
    routes_.push_back({"",     "/api/terminal/session/",    "/close",           false, handle_terminal_close});
}

HttpResponse App::handle(const HttpRequest& request) {
    // Reject any path containing null bytes or literal ".." components
    // to prevent null-byte injection and directory traversal attacks.
    if (request.path.find('\0') != std::string::npos ||
        request.path.find("..") != std::string::npos) {
        return json_response(400, "{\"error\":\"bad request\"}");
    }

    // Pre-route: state-dependent page navigation (no session required).
    if (request.method == "GET" && request.path == "/") {
        return redirect_response(users_.has_users() ? "/login" : "/onboarding");
    }
    if (request.method == "GET" && request.path == "/onboarding") {
        return users_.has_users() ? redirect_response("/login") : page_response("templates/onboarding.html");
    }
    if (request.method == "GET" && request.path == "/login") {
        if (!request.query_string.empty()) {
            return redirect_response("/login");
        }
        return users_.has_users() ? page_response("templates/login.html") : redirect_response("/onboarding");
    }

    const std::string tok = cookie_value(request, "cp_session");

    if (request.method == "GET" && request.path == "/2fa/setup") {
        if (!sessions_.username_for(tok)) return redirect_response("/login");
        if (!sessions_.pending_totp_setup(tok)) return redirect_response("/dashboard");
        return page_response("templates/totp_setup.html");
    }
    if (request.method == "GET" && request.path == "/dashboard") {
        if (!sessions_.username_for(tok)) return redirect_response("/login");
        if (sessions_.pending_totp_setup(tok)) return redirect_response("/2fa/setup");
        return page_response("templates/dashboard.html");
    }
    if (request.method == "GET" && request.path.rfind("/partials/", 0) == 0) {
        // path.find("..") already rejected above; no further traversal possible.
        return page_response("templates" + request.path + ".html");
    }
    if (request.method == "GET" && request.path.rfind("/static/", 0) == 0) {
        const auto asset = static_asset_path(request.path);
        const auto body  = read_file(asset);
        if (!body) { return json_response(404, "{\"error\":\"not found\"}"); }
        HttpResponse response;
        const auto dot = request.path.rfind('.');
        const std::string ext = (dot != std::string::npos) ? request.path.substr(dot) : "";
        if      (ext == ".css")   response.content_type = "text/css; charset=utf-8";
        else if (ext == ".js")    response.content_type = "application/javascript; charset=utf-8";
        else if (ext == ".png")   response.content_type = "image/png";
        else if (ext == ".ico")   response.content_type = "image/x-icon";
        else if (ext == ".svg")   response.content_type = "image/svg+xml";
        else if (ext == ".woff2") response.content_type = "font/woff2";
        else if (ext == ".woff")  response.content_type = "font/woff";
        else                      response.content_type = "application/octet-stream";
        response.body = *body;
        return response;
    }

    // Build per-request context (session resolved once for all handlers).
    RequestContext ctx{
        request,
        tok,
        sessions_.username_for(tok),
        users_,
        services_,
        nginx_,
        fail2ban_,
        system_admin_,
        terminal_,
        codex_projects_,
        codex_conversations_,
        sessions_
    };

    // TOTP-setup fence: most endpoints are blocked until 2FA is configured.
    if (!tok.empty() && sessions_.pending_totp_setup(tok)) {
        const bool allowed =
            (request.method == "GET"  && request.path == "/api/2fa/setup-info") ||
            (request.method == "POST" && request.path == "/api/2fa/confirm")    ||
            (request.method == "POST" && request.path == "/api/logout");
        if (!allowed) {
            return json_response(403, "{\"error\":\"complete 2fa setup first\"}");
        }
    }

    // Route dispatch.
    for (const auto& route : routes_) {
        if (!route.method.empty() && route.method != request.method) { continue; }
        if (route.exact) {
            if (request.path != route.prefix) continue;
            return route.handler(ctx, "");
        }
        if (request.path.rfind(route.prefix, 0) != 0) continue;
        const std::string tail = request.path.substr(route.prefix.size());
        if (!route.suffix.empty()) {
            if (tail.size() <= route.suffix.size()) continue;
            if (tail.rfind(route.suffix) != tail.size() - route.suffix.size()) continue;
            return route.handler(ctx, url_decode(tail.substr(0, tail.size() - route.suffix.size())));
        }
        if (tail.empty()) continue;
        return route.handler(ctx, url_decode(tail));
    }

    return json_response(404, "{\"error\":\"not found\"}");
}

// ---------------------------------------------------------------------------
// HTTP server loop
// ---------------------------------------------------------------------------

bool run_server(App& app, int port) {
    signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        log_errno(LogLevel::Error, "unable to create server socket", errno);
        return false;
    }
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_errno(LogLevel::Warning, "unable to set SO_REUSEADDR", errno);
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        log_errno(LogLevel::Error, "unable to bind HTTP server to port " + std::to_string(port), errno);
        close(server_fd);
        return false;
    }
    if (listen(server_fd, 32) < 0) {
        log_errno(LogLevel::Error, "unable to listen on port " + std::to_string(port), errno);
        close(server_fd);
        return false;
    }

    log_message(LogLevel::Info, "cuddlePanel listening on http://127.0.0.1:" + std::to_string(port));
    while (true) {
        int client = accept(server_fd, nullptr, nullptr);
        if (client < 0) {
            log_errno(LogLevel::Warning, "failed to accept client connection", errno);
            continue;
        }
        try {
            auto raw = read_request(client);
            if (raw.empty()) { close(client); continue; }
            auto request  = parse_request(raw);
            auto response = serialize_response(app.handle(request));
            if (send(client, response.data(), response.size(), 0) < 0) {
                log_errno(LogLevel::Warning, "failed to send HTTP response", errno);
            }
        } catch (const std::exception& exception) {
            log_message(LogLevel::Error, std::string("unhandled server exception: ") + exception.what());
            const auto response = serialize_response(HttpResponse{500, "application/json; charset=utf-8", {}, "{\"error\":\"internal server error\"}"});
            send(client, response.data(), response.size(), 0);
        } catch (...) {
            log_message(LogLevel::Error, "unhandled non-standard server exception");
            const auto response = serialize_response(HttpResponse{500, "application/json; charset=utf-8", {}, "{\"error\":\"internal server error\"}"});
            send(client, response.data(), response.size(), 0);
        }
        close(client);
    }
}

} // namespace cuddle
