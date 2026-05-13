#include "http.h"

#include <sstream>

namespace cuddle {
namespace {

std::string nginx_sites_json(const NginxStore& store) {
    std::ostringstream out;
    out << "{\"sites\":[";
    bool first = true;
    for (const auto& site : store.sites()) {
        auto record = store.read_site(site.name);
        if (!record) continue;
        if (!first) out << ",";
        first = false;
        out << "{\"name\":\""        << json_escape(record->name)
            << "\",\"filename\":\""  << json_escape(record->filename)
            << "\",\"description\":\"" << json_escape(record->description)
            << "\",\"content\":\""   << json_escape(record->content)
            << "\",\"enabled\":"     << (record->enabled ? "true" : "false")
            << "}";
    }
    out << "],\"availableDir\":\"" << json_escape(store.available_dir())
        << "\",\"enabledDir\":\""  << json_escape(store.enabled_dir()) << "\"}";
    return out.str();
}

} // anonymous namespace

HttpResponse handle_nginx_sites(const RequestContext& ctx, const std::string&) {
    if (ctx.request.method == "GET") {
        if (auto err = ctx.require_permission("nginx", PermissionLevel::View)) return *err;
        return json_response(200, nginx_sites_json(ctx.nginx));
    }
    if (ctx.request.method == "POST") {
        if (auto err = ctx.require_permission("nginx", PermissionLevel::Manage)) return *err;
        auto form = parse_form(ctx.request.body);
        if (!ctx.nginx.create_site(form["name"], form["filename"], form["description"], form["content"])) {
            return json_response(400, "{\"error\":\"unable to create nginx site\"}");
        }
        return json_response(200, "{\"ok\":true}");
    }
    return json_response(404, "{\"error\":\"not found\"}");
}

HttpResponse handle_update_nginx_site(const RequestContext& ctx, const std::string& name) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("nginx", PermissionLevel::Manage)) return *err;
    auto form = parse_form(ctx.request.body);
    const std::string new_name = form["name"].empty() ? name : form["name"];
    if (!ctx.nginx.update_site(name, new_name, form["filename"], form["description"], form["content"])) {
        return json_response(400, "{\"error\":\"unable to update nginx site\"}");
    }
    return json_response(200, "{\"ok\":true}");
}

HttpResponse handle_nginx_action(const RequestContext& ctx, const std::string& name) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("nginx", PermissionLevel::Manage)) return *err;
    auto form = parse_form(ctx.request.body);
    const std::string action = form["action"];
    NginxActionResult result;
    std::string store_error;
    if (action == "enable") {
        result.ok     = ctx.nginx.set_enabled(name, true, &store_error);
        result.output = result.ok ? "site enabled" : (store_error.empty() ? "unable to enable site" : store_error);
    } else if (action == "disable") {
        result.ok     = ctx.nginx.set_enabled(name, false, &store_error);
        result.output = result.ok ? "site disabled" : (store_error.empty() ? "unable to disable site" : store_error);
    } else if (action == "test") {
        result = nginx_test_config();
    } else if (action == "reload") {
        result = nginx_reload();
    } else {
        return json_response(400, "{\"error\":\"invalid nginx action\"}");
    }
    std::ostringstream out;
    out << "{\"ok\":" << (result.ok ? "true" : "false")
        << ",\"output\":\"" << json_escape(result.output) << "\"}";
    return json_response(result.ok ? 200 : 400, out.str());
}

} // namespace cuddle
