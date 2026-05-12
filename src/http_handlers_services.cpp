#include "http.h"

#include <sstream>

namespace cuddle {
namespace {

std::string services_json(const std::vector<DiscoveredService>& services) {
    std::ostringstream out;
    out << "{\"services\":[";
    bool first = true;
    for (const auto& service : services) {
        if (!first) out << ",";
        first = false;
        out << "{\"name\":\""        << json_escape(service.name)
            << "\",\"unit\":\""      << json_escape(service.unit)
            << "\",\"description\":\"" << json_escape(service.description)
            << "\",\"state\":\""     << json_escape(service.active_state)
            << "\",\"subState\":\""  << json_escape(service.sub_state)
            << "\",\"enabledState\":\"" << json_escape(service.unit_file_state)
            << "\",\"loadState\":\"" << json_escape(service.load_state)
            << "\",\"fragmentPath\":\"" << json_escape(service.fragment_path)
            << "\"}";
    }
    out << "]}";
    return out.str();
}

std::string service_detail_json(const DiscoveredService& service) {
    std::ostringstream out;
    out << "{\"service\":{"
        << "\"name\":\"" << json_escape(service.name)
        << "\",\"unit\":\"" << json_escape(service.unit)
        << "\",\"description\":\"" << json_escape(service.description)
        << "\",\"state\":\"" << json_escape(service.active_state)
        << "\",\"subState\":\"" << json_escape(service.sub_state)
        << "\",\"enabledState\":\"" << json_escape(service.unit_file_state)
        << "\",\"loadState\":\"" << json_escape(service.load_state)
        << "\",\"fragmentPath\":\"" << json_escape(service.fragment_path)
        << "\"}}";
    return out.str();
}

} // anonymous namespace

HttpResponse handle_services(const RequestContext& ctx, const std::string&) {
    if (ctx.request.method == "GET") {
        if (auto err = ctx.require_permission("services", PermissionLevel::View)) return *err;
        return json_response(200, services_json(discover_services()));
    }
    if (ctx.request.method == "POST") {
        if (auto err = ctx.require_permission("services", PermissionLevel::Manage)) return *err;
        auto form = parse_form(ctx.request.body);
        if (!ctx.services.create_service(form["name"], form["unit"], form["description"])) {
            return json_response(400, "{\"error\":\"unable to create service\"}");
        }
        return json_response(200, "{\"ok\":true}");
    }
    return json_response(404, "{\"error\":\"not found\"}");
}

HttpResponse handle_service_detail(const RequestContext& ctx, const std::string& unit) {
    if (ctx.request.method != "GET") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("services", PermissionLevel::View)) return *err;
    const auto service = discover_service(unit);
    if (!service) {
        return json_response(404, "{\"error\":\"service not found\"}");
    }
    return json_response(200, service_detail_json(*service));
}

HttpResponse handle_update_service(const RequestContext& ctx, const std::string& name) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("services", PermissionLevel::Manage)) return *err;
    auto form = parse_form(ctx.request.body);
    const std::string new_name = form["name"].empty() ? name : form["name"];
    if (!ctx.services.update_service(name, new_name, form["unit"], form["description"])) {
        return json_response(400, "{\"error\":\"unable to update service\"}");
    }
    return json_response(200, "{\"ok\":true}");
}

HttpResponse handle_service_action(const RequestContext& ctx, const std::string& name) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("services", PermissionLevel::Manage)) return *err;
    const auto service = discover_service(name);
    if (!service) {
        return json_response(404, "{\"error\":\"service not found\"}");
    }
    auto form = parse_form(ctx.request.body);
    const auto result = run_service_action(service->unit, form["action"]);
    std::ostringstream out;
    out << "{\"ok\":" << (result.ok ? "true" : "false")
        << ",\"output\":\"" << json_escape(result.output) << "\"}";
    return json_response(result.ok ? 200 : 400, out.str());
}

} // namespace cuddle
