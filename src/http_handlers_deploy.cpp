#include "http.h"

#include <sstream>

namespace cuddle {

HttpResponse handle_deploy_run(const RequestContext& ctx, const std::string&) {
    if (auto err = ctx.require_permission("deploy", PermissionLevel::Manage)) return *err;
    auto form = parse_form(ctx.request.body);
    auto deploy_request = deploy_request_from_form(form);
    if (!deploy_request) {
        return json_response(400, "{\"error\":\"invalid deploy request\"}");
    }
    const auto result = run_deploy_site(*deploy_request);
    std::ostringstream out;
    out << "{\"ok\":" << (result.ok ? "true" : "false")
        << ",\"output\":\"" << json_escape(result.output) << "\"}";
    return json_response(result.ok ? 200 : 400, out.str());
}

} // namespace cuddle
