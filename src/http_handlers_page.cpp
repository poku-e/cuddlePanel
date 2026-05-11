#include "http.h"
#include "codex_runner.h"
#include "template.h"

#include <ctime>
#include <sstream>

namespace cuddle {
namespace {

std::string page_shell(const std::string& title, const std::string& body) {
    return "<section class=\"cp-panel\"><h1>" + html_escape(title) + "</h1>" + body + "</section>";
}

std::string template_panel(const std::string& title,
                            const std::string& path,
                            const std::map<std::string, std::string>& values = {}) {
    return page_shell(title, render_template(path, values));
}

} // anonymous namespace

HttpResponse handle_api_page(const RequestContext& ctx, const std::string& page_name) {
    if (auto err = ctx.require_permission(page_name, PermissionLevel::View)) return *err;

    HttpResponse response;
    response.content_type = "text/html; charset=utf-8";

    if (page_name == "dashboard") {
        response.body = template_panel("Dashboard", "templates/pages/dashboard.html");
    } else if (page_name == "users") {
        const bool can_manage = ctx.users.has_permission(*ctx.username, "users", PermissionLevel::Manage);
        response.body = template_panel("Users", "templates/pages/users.html", {
            {"can_manage_users", can_manage ? "1" : "0"},
            {"disabled_attr",    can_manage ? "" : " disabled"},
            {"view_only_note",   can_manage ? "" : "<p class=\"small text-secondary mt-3 mb-0\">You have view access to this page, but user changes require `users:manage`.</p>"}
        });
    } else if (page_name == "services") {
        const bool can_manage = ctx.users.has_permission(*ctx.username, "services", PermissionLevel::Manage);
        response.body = template_panel("Services", "templates/pages/services.html", {
            {"can_manage_services", can_manage ? "1" : "0"},
            {"disabled_attr",       can_manage ? "" : " disabled"},
            {"view_only_note",      can_manage ? "" : "<p class=\"small text-secondary mt-3 mb-0\">You have view access to this page, but service changes require `services:manage`.</p>"}
        });
    } else if (page_name == "system") {
        const bool can_manage = ctx.users.has_permission(*ctx.username, "system", PermissionLevel::Manage);
        std::ostringstream roots_note;
        const auto roots = ctx.system_admin.allowed_path_roots();
        for (size_t i = 0; i < roots.size(); ++i) {
            if (i > 0) roots_note << ", ";
            roots_note << "<code>" << html_escape(roots[i]) << "</code>";
        }
        response.body = template_panel("System Administration", "templates/pages/system.html", {
            {"can_manage_system", can_manage ? "1" : "0"},
            {"disabled_attr",     can_manage ? "" : " disabled"},
            {"view_only_note",    can_manage ? "" : "<p class=\"small text-secondary mt-3 mb-0\">You have view access to this page, but account and path changes require `system:manage`.</p>"},
            {"roots_note",        roots_note.str()}
        });
    } else if (page_name == "nginx") {
        const bool can_manage = ctx.users.has_permission(*ctx.username, "nginx", PermissionLevel::Manage);
        response.body = template_panel("Nginx Sites", "templates/pages/nginx.html", {
            {"can_manage_nginx", can_manage ? "1" : "0"},
            {"disabled_attr",    can_manage ? "" : " disabled"},
            {"view_only_note",   can_manage ? "" : "<p class=\"small text-secondary mt-3 mb-0\">You have view access to this page, but nginx changes require `nginx:manage`.</p>"}
        });
    } else if (page_name == "terminal") {
        if (!ctx.sessions.terminal_totp_recently_verified(ctx.session_token, std::time(nullptr), 30 * 60)) {
            response.body = template_panel("Terminal Verification", "templates/pages/terminal_gate.html");
        } else {
            const auto policy = terminal_runtime_policy();
            response.body = template_panel("Terminal", "templates/pages/terminal.html", {
                {"terminal_user",         html_escape(policy.run_as_user)},
                {"terminal_workdir",      html_escape(policy.working_directory)},
                {"terminal_idle_timeout", std::to_string(policy.idle_timeout_seconds)},
                {"terminal_max_runtime",  std::to_string(policy.max_session_seconds)}
            });
        }
    } else if (page_name == "codex") {
        const bool can_manage = ctx.users.has_permission(*ctx.username, "codex", PermissionLevel::Manage);
        const auto codex = codex_runtime_config();
        response.body = template_panel("Codex", "templates/pages/codex.html", {
            {"can_manage_codex",      can_manage ? "1" : "0"},
            {"disabled_attr",         can_manage ? "" : " disabled"},
            {"view_only_note",        can_manage ? "" : "<p class=\"small text-secondary mt-3 mb-0\">You have view access to this page, but running Codex requires `codex:manage`.</p>"},
            {"codex_binary",          html_escape(codex.binary_path)},
            {"codex_workdir",         html_escape(codex.working_directory)},
            {"codex_model",           html_escape(codex.model.empty() ? "default" : codex.model)},
            {"codex_timeout_seconds", std::to_string(codex.timeout_seconds)},
            {"maintenance_workdir",   html_escape(codex_maintenance_workdir())}
        });
    } else if (page_name == "deploy") {
        const bool can_manage = ctx.users.has_permission(*ctx.username, "deploy", PermissionLevel::Manage);
        response.body = template_panel("Deploy Site", "templates/pages/deploy.html", {
            {"can_manage_deploy", can_manage ? "1" : "0"},
            {"disabled_attr",     can_manage ? "" : " disabled"},
            {"view_only_note",    can_manage ? "" : "<p class=\"small text-secondary mt-3 mb-0\">You have view access to this page, but deploy execution requires `deploy:manage`.</p>"}
        });
    } else {
        return json_response(404, "{\"error\":\"unknown page\"}");
    }
    return response;
}

HttpResponse handle_system_user_page(const RequestContext& ctx, const std::string& username) {
    if (auto err = ctx.require_permission("system", PermissionLevel::View)) return *err;
    if (!valid_system_username(username)) {
        return json_response(404, "{\"error\":\"unknown user\"}");
    }
    const auto user = ctx.system_admin.find_user(username);
    if (!user) {
        return json_response(404, "{\"error\":\"unknown user\"}");
    }

    std::ostringstream roots_note;
    const auto roots = ctx.system_admin.allowed_path_roots();
    for (size_t i = 0; i < roots.size(); ++i) {
        if (i > 0) roots_note << ", ";
        roots_note << "<code>" << html_escape(roots[i]) << "</code>";
    }

    const bool can_manage = ctx.users.has_permission(*ctx.username, "system", PermissionLevel::Manage);
    HttpResponse response;
    response.content_type = "text/html; charset=utf-8";
    response.body = template_panel("User Management", "templates/pages/system_user.html", {
        {"can_manage_system", can_manage ? "1" : "0"},
        {"disabled_attr",     can_manage ? "" : " disabled"},
        {"view_only_note",    can_manage ? "" : "<p class=\"small mt-3 mb-0\">You have view access to this page, but account changes require `system:manage`.</p>"},
        {"roots_note",        roots_note.str()},
        {"selected_username", html_escape(user->username)}
    });
    return response;
}

} // namespace cuddle
