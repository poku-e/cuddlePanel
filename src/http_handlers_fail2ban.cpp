#include "http.h"

#include <sstream>

namespace cuddle {
namespace {

std::string fail2ban_jails_json(const std::vector<Fail2banJailSummary>& jails) {
    std::ostringstream out;
    out << "{\"jails\":[";
    for (size_t i = 0; i < jails.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << "{\"name\":\"" << json_escape(jails[i].name)
            << "\",\"running\":" << (jails[i].running ? "true" : "false")
            << ",\"currentlyFailed\":" << jails[i].currently_failed
            << ",\"currentlyBanned\":" << jails[i].currently_banned
            << "}";
    }
    out << "]}";
    return out.str();
}

std::string fail2ban_jail_detail_json(const Fail2banJailDetail& detail) {
    std::ostringstream out;
    out << "{\"jail\":{";
    out << "\"name\":\"" << json_escape(detail.summary.name)
        << "\",\"running\":" << (detail.summary.running ? "true" : "false")
        << ",\"currentlyFailed\":" << detail.summary.currently_failed
        << ",\"currentlyBanned\":" << detail.summary.currently_banned
        << ",\"bannedIps\":[";

    for (size_t i = 0; i < detail.banned_ips.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << "\"" << json_escape(detail.banned_ips[i]) << "\"";
    }

    out << "],\"ignoredIps\":[";
    for (size_t i = 0; i < detail.ignored_ips.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << "\"" << json_escape(detail.ignored_ips[i]) << "\"";
    }
    out << "]}}";
    return out.str();
}

std::string fail2ban_logs_json(const std::vector<std::string>& lines) {
    std::ostringstream out;
    out << "{\"lines\":[";
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << "\"" << json_escape(lines[i]) << "\"";
    }
    out << "]}";
    return out.str();
}

HttpResponse action_response(const Fail2banActionResult& result) {
    std::ostringstream out;
    out << "{\"ok\":" << (result.ok ? "true" : "false")
        << ",\"output\":\"" << json_escape(result.output) << "\"}";
    return json_response(result.ok ? 200 : 400, out.str());
}

size_t parse_log_line_limit(const std::string& query_string) {
    const std::string needle = "lines=";
    const auto pos = query_string.find(needle);
    if (pos == std::string::npos) {
        return 200;
    }
    std::string value = query_string.substr(pos + needle.size());
    const auto amp = value.find('&');
    if (amp != std::string::npos) {
        value = value.substr(0, amp);
    }
    if (value.empty()) {
        return 200;
    }
    try {
        const size_t parsed = static_cast<size_t>(std::stoul(value));
        if (parsed == 0) {
            return 200;
        }
        return parsed;
    } catch (...) {
        return 200;
    }
}

} // anonymous namespace

HttpResponse handle_fail2ban_jails(const RequestContext& ctx, const std::string&) {
    if (ctx.request.method != "GET") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("fail2ban", PermissionLevel::View)) return *err;
    return json_response(200, fail2ban_jails_json(ctx.fail2ban.list_jails()));
}

HttpResponse handle_fail2ban_jail_detail(const RequestContext& ctx, const std::string& jail) {
    if (ctx.request.method != "GET") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("fail2ban", PermissionLevel::View)) return *err;
    const auto detail = ctx.fail2ban.jail_detail(jail);
    if (!detail) {
        return json_response(404, "{\"error\":\"jail not found\"}");
    }
    return json_response(200, fail2ban_jail_detail_json(*detail));
}

HttpResponse handle_fail2ban_jail_action(const RequestContext& ctx, const std::string& jail) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("fail2ban", PermissionLevel::Manage)) return *err;
    auto form = parse_form(ctx.request.body);
    return action_response(ctx.fail2ban.jail_action(jail, form["action"]));
}

HttpResponse handle_fail2ban_ban(const RequestContext& ctx, const std::string& jail) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("fail2ban", PermissionLevel::Manage)) return *err;
    auto form = parse_form(ctx.request.body);
    return action_response(ctx.fail2ban.ban_ip(jail, form["ip"]));
}

HttpResponse handle_fail2ban_unban(const RequestContext& ctx, const std::string& jail) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("fail2ban", PermissionLevel::Manage)) return *err;
    auto form = parse_form(ctx.request.body);
    return action_response(ctx.fail2ban.unban_ip(jail, form["ip"]));
}

HttpResponse handle_fail2ban_whitelist(const RequestContext& ctx, const std::string& jail) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("fail2ban", PermissionLevel::Manage)) return *err;
    auto form = parse_form(ctx.request.body);
    return action_response(ctx.fail2ban.whitelist_action(jail, form["action"], form["ip"]));
}

HttpResponse handle_fail2ban_action(const RequestContext& ctx, const std::string&) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("fail2ban", PermissionLevel::Manage)) return *err;
    auto form = parse_form(ctx.request.body);
    return action_response(ctx.fail2ban.global_action(form["action"]));
}

HttpResponse handle_fail2ban_logs(const RequestContext& ctx, const std::string&) {
    if (ctx.request.method != "GET") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("fail2ban", PermissionLevel::View)) return *err;
    const size_t max_lines = parse_log_line_limit(ctx.request.query_string);
    return json_response(200, fail2ban_logs_json(ctx.fail2ban.recent_logs(max_lines)));
}

} // namespace cuddle
