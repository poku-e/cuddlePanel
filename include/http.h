#pragma once

#include "deploy_runner.h"
#include "codex_chat.h"
#include "nginx_store.h"
#include "service_store.h"
#include "setup.h"
#include "system_admin.h"
#include "terminal_manager.h"
#include "user_store.h"

#include <functional>
#include <ctime>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cuddle {

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "text/html; charset=utf-8";
    std::map<std::string, std::string> headers;
    std::string body;
};

// HTTP response factories – usable from any handler file.
HttpResponse json_response(int status, const std::string& body);
HttpResponse redirect_response(const std::string& location);
HttpResponse page_response(const std::string& file);

// Encoding / parsing utilities shared across handler files.
std::string json_escape(const std::string& value);
std::string html_escape(const std::string& value);
std::string url_decode(const std::string& value);
std::map<std::string, std::string> parse_form(const std::string& body);

struct SessionState {
    std::string username;
    bool pending_totp_setup = false;
    std::time_t terminal_totp_verified_at = 0;
};

class SessionStore {
public:
    std::string create(const std::string& username, bool pending_totp_setup = false);
    std::optional<std::string> username_for(const std::string& token) const;
    std::optional<SessionState> session_for(const std::string& token) const;
    bool pending_totp_setup(const std::string& token) const;
    void mark_totp_setup_complete(const std::string& token);
    void mark_terminal_totp_verified(const std::string& token, std::time_t verified_at);
    bool terminal_totp_recently_verified(const std::string& token,
                                         std::time_t now,
                                         std::time_t max_age_seconds) const;
    void erase(const std::string& token);

private:
    mutable std::mutex mutex_;
    std::map<std::string, SessionState> sessions_;
};

// Resolved per-request context passed to every route handler.
// Eliminates repeated session lookups and auth/permission boilerplate.
struct RequestContext {
    const HttpRequest&             request;
    std::string                    session_token;
    std::optional<std::string>     username;
    UserStore&                     users;
    ServiceStore&                  services;
    NginxStore&                    nginx;
    SystemAdmin&                   system_admin;
    TerminalManager&               terminal;
    CodexProjectStore&             codex_projects;
    CodexConversationManager&      codex_conversations;
    SessionStore&                  sessions;

    // Returns a 401 response when no authenticated user, or nullopt if OK.
    std::optional<HttpResponse> require_auth() const;

    // Returns an error response when the user lacks the given permission,
    // or nullopt if OK.  Also enforces authentication.
    std::optional<HttpResponse> require_permission(const std::string& page,
                                                   PermissionLevel level) const;
};

// Handler signature: (resolved context, url-decoded path id-segment) → response.
using RouteHandler = std::function<HttpResponse(const RequestContext&, const std::string&)>;

// A registered route entry.  The router tests them in registration order.
//   method – HTTP method to match; empty = any method.
//   prefix – matched against the start of the request path.
//   suffix – if non-empty, matched against the end of the segment after prefix;
//            the id between prefix and suffix is url-decoded and passed to the handler.
//   exact  – if true, the full path must equal prefix (no id extraction).
struct Route {
    std::string  method;
    std::string  prefix;
    std::string  suffix;
    bool         exact   = false;
    RouteHandler handler;
};

class App {
public:
    App(UserStore& users,
        ServiceStore& services,
        NginxStore& nginx,
        SystemAdmin& system_admin,
        TerminalManager& terminal,
        CodexProjectStore& codex_projects,
        CodexConversationManager& codex_conversations,
        SessionStore& sessions);

    HttpResponse handle(const HttpRequest& request);

private:
    UserStore&                  users_;
    ServiceStore&               services_;
    NginxStore&                 nginx_;
    SystemAdmin&                system_admin_;
    TerminalManager&            terminal_;
    CodexProjectStore&          codex_projects_;
    CodexConversationManager&   codex_conversations_;
    SessionStore&               sessions_;

    std::vector<Route> routes_;
    void build_routes();
};

bool run_server(App& app, int port);

}
