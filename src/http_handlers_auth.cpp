#include "http.h"
#include "auth.h"
#include "setup.h"
#include "totp.h"

#include <cstdlib>
#include <ctime>
#include <sstream>

namespace cuddle {
namespace {

std::string setup_status_json() {
    const auto config = current_first_run_config();
    const auto dependencies = first_run_dependency_status(config);
    std::ostringstream out;
    out << "{"
        << "\"config\":{"
        << "\"port\":\"" << json_escape(config.port) << "\","
        << "\"secure_cookies\":" << (config.secure_cookies ? "true" : "false") << ","
        << "\"deploy_systemd_unit_dir\":\"" << json_escape(config.deploy_systemd_unit_dir) << "\","
        << "\"deploy_allowed_roots\":\"" << json_escape(config.deploy_allowed_roots) << "\","
        << "\"systemctl_bin\":\"" << json_escape(config.systemctl_bin) << "\","
        << "\"certbot_bin\":\"" << json_escape(config.certbot_bin) << "\","
        << "\"python3_bin\":\"" << json_escape(config.python3_bin) << "\","
        << "\"npm_bin\":\"" << json_escape(config.npm_bin) << "\","
        << "\"node_bin\":\"" << json_escape(config.node_bin) << "\","
        << "\"go_bin\":\"" << json_escape(config.go_bin) << "\","
        << "\"curl_bin\":\"" << json_escape(config.curl_bin) << "\","
        << "\"cloudflare_zone_id\":\"" << json_escape(config.cloudflare_zone_id) << "\","
        << "\"cloudflare_api_token\":\"\","
        << "\"nginx_available_dir\":\"" << json_escape(config.nginx_available_dir) << "\","
        << "\"nginx_enabled_dir\":\"" << json_escape(config.nginx_enabled_dir) << "\","
        << "\"nginx_bin\":\"" << json_escape(config.nginx_bin) << "\","
        << "\"nginx_reload_service\":\"" << json_escape(config.nginx_reload_service) << "\","
        << "\"system_allowed_roots\":\"" << json_escape(config.system_allowed_roots) << "\","
        << "\"terminal_shell\":\"" << json_escape(config.terminal_shell) << "\","
        << "\"terminal_run_as_user\":\"" << json_escape(config.terminal_run_as_user) << "\","
        << "\"terminal_run_as_group\":\"" << json_escape(config.terminal_run_as_group) << "\","
        << "\"terminal_workdir\":\"" << json_escape(config.terminal_workdir) << "\","
        << "\"terminal_max_sessions_per_user\":\"" << json_escape(config.terminal_max_sessions_per_user) << "\","
        << "\"terminal_idle_timeout_seconds\":\"" << json_escape(config.terminal_idle_timeout_seconds) << "\","
        << "\"terminal_max_session_seconds\":\"" << json_escape(config.terminal_max_session_seconds) << "\","
        << "\"codex_bin\":\"" << json_escape(config.codex_bin) << "\","
        << "\"codex_workdir\":\"" << json_escape(config.codex_workdir) << "\","
        << "\"codex_model\":\"" << json_escape(config.codex_model) << "\","
        << "\"codex_timeout_seconds\":\"" << json_escape(config.codex_timeout_seconds) << "\""
        << "},"
        << "\"dependencies\":[";
    bool first = true;
    for (const auto& dep : dependencies) {
        if (!first) out << ",";
        first = false;
        out << "{"
            << "\"name\":\""    << json_escape(dep.name)    << "\","
            << "\"path\":\""    << json_escape(dep.path)    << "\","
            << "\"present\":"   << (dep.present  ? "true" : "false") << ","
            << "\"required\":"  << (dep.required ? "true" : "false") << ","
            << "\"details\":\"" << json_escape(dep.details) << "\""
            << "}";
    }
    out << "]}";
    return out.str();
}

} // anonymous namespace

HttpResponse handle_setup_status(const RequestContext& ctx, const std::string&) {
    if (ctx.users.has_users()) {
        return json_response(403, "{\"error\":\"onboarding already completed\"}");
    }
    return json_response(200, setup_status_json());
}

HttpResponse handle_onboarding_post(const RequestContext& ctx, const std::string&) {
    if (ctx.users.has_users()) {
        return json_response(409, "{\"error\":\"onboarding already completed\"}");
    }
    auto form = parse_form(ctx.request.body);
    std::string setup_error;
    auto config = first_run_config_from_form(form, &setup_error);
    if (!config) {
        return json_response(400, "{\"error\":\"" + json_escape(setup_error.empty() ? "invalid setup configuration" : setup_error) + "\"}");
    }
    if (!write_first_run_env_file(*config, ".env", &setup_error)) {
        return json_response(500, "{\"error\":\"" + json_escape(setup_error.empty() ? "unable to write .env" : setup_error) + "\"}");
    }
    apply_first_run_config(*config);
    if (!ctx.users.create_superuser(form["username"], form["password"])) {
        return json_response(400, "{\"error\":\"invalid username or password\"}");
    }
    auto token = ctx.sessions.create(form["username"], true);
    HttpResponse response = json_response(200, "{\"ok\":true,\"next\":\"/2fa/setup\"}");
    const std::string secure = std::getenv("CUDDLEPANEL_SECURE_COOKIES") ? "; Secure" : "";
    response.headers["Set-Cookie"] = "cp_session=" + token + "; Path=/; HttpOnly; SameSite=Strict" + secure;
    return response;
}

HttpResponse handle_login_post(const RequestContext& ctx, const std::string&) {
    auto form = parse_form(ctx.request.body);
    auto user = ctx.users.authenticate(form["username"], form["password"]);
    if (!user) {
        return json_response(401, "{\"error\":\"invalid credentials\"}");
    }
    if (user->totp_secret.empty()) {
        ctx.users.set_totp_secret(user->username, generate_totp_secret());
        user = ctx.users.find_user(user->username);
    }
    const bool pending_setup = !user->totp_confirmed;
    auto token = ctx.sessions.create(user->username, pending_setup);
    HttpResponse response = json_response(200,
        pending_setup ? "{\"ok\":true,\"next\":\"/2fa/setup\"}"
                      : "{\"ok\":true,\"next\":\"/dashboard\"}");
    const std::string secure = std::getenv("CUDDLEPANEL_SECURE_COOKIES") ? "; Secure" : "";
    response.headers["Set-Cookie"] = "cp_session=" + token + "; Path=/; HttpOnly; SameSite=Strict" + secure;
    return response;
}

HttpResponse handle_logout_post(const RequestContext& ctx, const std::string&) {
    ctx.sessions.erase(ctx.session_token);
    HttpResponse response = json_response(200, "{\"ok\":true}");
    response.headers["Set-Cookie"] = "cp_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict";
    return response;
}

HttpResponse handle_totp_setup_info(const RequestContext& ctx, const std::string&) {
    if (auto err = ctx.require_auth()) return *err;
    if (!ctx.sessions.pending_totp_setup(ctx.session_token)) {
        return json_response(403, "{\"error\":\"2fa setup not required\"}");
    }
    auto user = ctx.users.find_user(*ctx.username);
    if (!user || user->totp_secret.empty()) {
        return json_response(400, "{\"error\":\"missing totp secret\"}");
    }
    const auto uri = build_otpauth_uri("cuddlePanel", user->username, user->totp_secret);
    std::ostringstream out;
    out << "{\"username\":\"" << json_escape(user->username)
        << "\",\"secret\":\""  << json_escape(user->totp_secret)
        << "\",\"uri\":\""     << json_escape(uri) << "\"}";
    return json_response(200, out.str());
}

HttpResponse handle_totp_confirm(const RequestContext& ctx, const std::string&) {
    if (auto err = ctx.require_auth()) return *err;
    if (!ctx.sessions.pending_totp_setup(ctx.session_token)) {
        return json_response(403, "{\"error\":\"2fa setup not required\"}");
    }
    auto user = ctx.users.find_user(*ctx.username);
    if (!user || user->totp_secret.empty()) {
        return json_response(400, "{\"error\":\"missing totp secret\"}");
    }
    auto form = parse_form(ctx.request.body);
    if (!verify_totp_code(user->totp_secret, form["code"], std::time(nullptr))) {
        return json_response(400, "{\"error\":\"invalid otp code\"}");
    }
    if (!ctx.users.confirm_totp(*ctx.username)) {
        return json_response(500, "{\"error\":\"unable to confirm otp\"}");
    }
    ctx.sessions.mark_totp_setup_complete(ctx.session_token);
    return json_response(200, "{\"ok\":true,\"next\":\"/dashboard\"}");
}

HttpResponse handle_terminal_totp_verify(const RequestContext& ctx, const std::string&) {
    if (auto err = ctx.require_auth()) return *err;
    if (ctx.sessions.pending_totp_setup(ctx.session_token)) {
        return json_response(403, "{\"error\":\"complete 2fa setup first\"}");
    }
    auto user = ctx.users.find_user(*ctx.username);
    if (!user || user->totp_secret.empty() || !user->totp_confirmed) {
        return json_response(403, "{\"error\":\"2fa not configured\"}");
    }
    auto form = parse_form(ctx.request.body);
    if (!verify_totp_code(user->totp_secret, form["code"], std::time(nullptr))) {
        return json_response(400, "{\"error\":\"invalid otp code\"}");
    }
    ctx.sessions.mark_terminal_totp_verified(ctx.session_token, std::time(nullptr));
    return json_response(200, "{\"ok\":true}");
}

} // namespace cuddle
