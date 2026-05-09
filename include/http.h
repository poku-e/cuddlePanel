#pragma once

#include "deploy_runner.h"
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

class App {
public:
    App(UserStore& users,
        ServiceStore& services,
        NginxStore& nginx,
        SystemAdmin& system_admin,
        TerminalManager& terminal,
        SessionStore& sessions);
    HttpResponse handle(const HttpRequest& request);

private:
    UserStore& users_;
    ServiceStore& services_;
    NginxStore& nginx_;
    SystemAdmin& system_admin_;
    TerminalManager& terminal_;
    SessionStore& sessions_;

    HttpResponse page(const std::string& file) const;
    HttpResponse json(int status, const std::string& body) const;
    HttpResponse redirect(const std::string& location) const;
    std::optional<std::string> current_user(const HttpRequest& request) const;
    HttpResponse api_page(const HttpRequest& request, const std::string& page_name) const;
    HttpResponse api_setup_status(const HttpRequest& request) const;
    HttpResponse api_users(const HttpRequest& request) const;
    HttpResponse api_user_permissions(const HttpRequest& request, const std::string& username) const;
    HttpResponse api_delete_user(const HttpRequest& request, const std::string& username) const;
    HttpResponse api_services(const HttpRequest& request) const;
    HttpResponse api_update_service(const HttpRequest& request, const std::string& name) const;
    HttpResponse api_service_action(const HttpRequest& request, const std::string& name) const;
    HttpResponse api_system_users(const HttpRequest& request) const;
    HttpResponse api_system_user_action(const HttpRequest& request, const std::string& username) const;
    HttpResponse api_system_authorized_keys(const HttpRequest& request, const std::string& username) const;
    HttpResponse api_system_path_action(const HttpRequest& request) const;
    HttpResponse api_nginx_sites(const HttpRequest& request) const;
    HttpResponse api_update_nginx_site(const HttpRequest& request, const std::string& name) const;
    HttpResponse api_nginx_action(const HttpRequest& request, const std::string& name) const;
    HttpResponse api_deploy_run(const HttpRequest& request) const;
    HttpResponse api_totp_setup_info(const HttpRequest& request) const;
    HttpResponse api_totp_confirm(const HttpRequest& request) const;
    HttpResponse api_terminal_totp_verify(const HttpRequest& request) const;
    HttpResponse api_terminal_create(const HttpRequest& request) const;
    HttpResponse api_terminal_read(const HttpRequest& request, const std::string& session_id) const;
    HttpResponse api_terminal_write(const HttpRequest& request, const std::string& session_id) const;
    HttpResponse api_terminal_resize(const HttpRequest& request, const std::string& session_id) const;
    HttpResponse api_terminal_close(const HttpRequest& request, const std::string& session_id) const;
};

bool run_server(App& app, int port);
std::map<std::string, std::string> parse_form(const std::string& body);
std::string url_decode(const std::string& value);
std::string html_escape(const std::string& value);
std::string json_escape(const std::string& value);

}
