#include "http.h"
#include "codex_runner.h"
#include "template.h"

#include <ctime>
#include <sstream>

namespace cuddle {
namespace {

std::string dashboard_health_item(const std::string& id,
                                  const std::string& severity,
                                  const std::string& title,
                                  const std::string& detail,
                                  bool can_autofix) {
    std::ostringstream out;
    out << "{\"id\":\"" << json_escape(id)
        << "\",\"severity\":\"" << json_escape(severity)
        << "\",\"title\":\"" << json_escape(title)
        << "\",\"detail\":\"" << json_escape(detail)
        << "\",\"canAutoFix\":" << (can_autofix ? "true" : "false")
        << "}";
    return out.str();
}

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

HttpResponse handle_service_page(const RequestContext& ctx, const std::string& unit) {
    if (auto err = ctx.require_permission("services", PermissionLevel::View)) return *err;
    const auto service = discover_service(unit);
    if (!service) {
        return json_response(404, "{\"error\":\"service not found\"}");
    }

    const bool can_manage = ctx.users.has_permission(*ctx.username, "services", PermissionLevel::Manage);
    HttpResponse response;
    response.content_type = "text/html; charset=utf-8";
    response.body = template_panel("Service Management", "templates/pages/service_detail.html", {
        {"can_manage_services", can_manage ? "1" : "0"},
        {"disabled_attr",       can_manage ? "" : " disabled"},
        {"view_only_note",      can_manage ? "" : "<p class=\"small mt-3 mb-0\">You have view access to this page, but service changes require `services:manage`.</p>"},
        {"selected_unit",       html_escape(service->unit)},
        {"selected_name",       html_escape(service->name)}
    });
    return response;
}

HttpResponse handle_dashboard_health(const RequestContext& ctx, const std::string&) {
    if (auto err = ctx.require_permission("dashboard", PermissionLevel::View)) return *err;

    const bool can_manage_nginx = ctx.users.has_permission(*ctx.username, "nginx", PermissionLevel::Manage);

    std::vector<std::string> items;

    const NginxActionResult nginx_check = nginx_test_config();
    if (nginx_check.ok) {
        items.push_back(dashboard_health_item(
            "nginx-config",
            "green",
            "Nginx config is healthy",
            nginx_check.output,
            can_manage_nginx));
    } else {
        items.push_back(dashboard_health_item(
            "nginx-config",
            "red",
            "Nginx config invalid",
            nginx_check.output,
            can_manage_nginx));
    }

    size_t site_count = 0;
    size_t enabled_count = 0;
    for (const auto& site : ctx.nginx.sites()) {
        auto record = ctx.nginx.read_site(site.name);
        if (!record) {
            continue;
        }
        site_count += 1;
        if (record->enabled) {
            enabled_count += 1;
        }
    }

    if (site_count == 0) {
        items.push_back(dashboard_health_item(
            "nginx-sites",
            "yellow",
            "No nginx sites registered",
            "Add a site to avoid deploy-time routing issues.",
            false));
    } else if (enabled_count == 0) {
        items.push_back(dashboard_health_item(
            "nginx-sites",
            "yellow",
            "Nginx has no enabled site",
            "All registered nginx sites are currently disabled.",
            false));
    } else {
        std::ostringstream detail;
        detail << enabled_count << " enabled of " << site_count << " registered site";
        if (site_count != 1) {
            detail << "s";
        }
        detail << ".";
        items.push_back(dashboard_health_item(
            "nginx-sites",
            "green",
            "Nginx site coverage looks good",
            detail.str(),
            false));
    }

    std::ostringstream response_body;
    response_body << "{\"items\":[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            response_body << ",";
        }
        response_body << items[i];
    }
    response_body << "]}";
    return json_response(200, response_body.str());
}

HttpResponse handle_dashboard_nginx_autofix(const RequestContext& ctx, const std::string&) {
    if (auto err = ctx.require_permission("dashboard", PermissionLevel::View)) return *err;
    if (!ctx.users.has_permission(*ctx.username, "nginx", PermissionLevel::Manage)) {
        return json_response(403, "{\"error\":\"permission denied\"}");
    }

    const NginxActionResult test_result = nginx_test_config();
    if (!test_result.ok) {
        std::ostringstream out;
        out << "{\"error\":\"auto-fix could not repair nginx config\",\"output\":\""
            << json_escape(test_result.output)
            << "\"}";
        return json_response(400, out.str());
    }

    const NginxActionResult reload_result = nginx_reload();
    std::ostringstream out;
    out << "{\"ok\":" << (reload_result.ok ? "true" : "false")
        << ",\"output\":\"" << json_escape(reload_result.output) << "\"}";
    return json_response(reload_result.ok ? 200 : 400, out.str());
}

} // namespace cuddle
