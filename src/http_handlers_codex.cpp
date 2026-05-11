#include "http.h"
#include "codex_runner.h"

#include <cstdint>
#include <cstdlib>
#include <sstream>

namespace cuddle {
namespace {

std::string codex_result_json(const CodexResult& result) {
    std::ostringstream out;
    out << "{"
        << "\"ok\":"              << (result.ok       ? "true" : "false") << ","
        << "\"timed_out\":"       << (result.timed_out ? "true" : "false") << ","
        << "\"output\":\""        << json_escape(result.output)        << "\","
        << "\"agent_message\":\"" << json_escape(result.agent_message) << "\","
        << "\"change_summary\":\"" << json_escape(result.change_summary) << "\","
        << "\"working_directory\":\"" << json_escape(result.working_directory) << "\","
        << "\"changed_files\":[";
    for (size_t i = 0; i < result.changed_files.size(); ++i) {
        if (i > 0) out << ",";
        out << "\"" << json_escape(result.changed_files[i]) << "\"";
    }
    out << "]}";
    return out.str();
}

std::string codex_projects_json(const std::vector<CodexProject>& projects) {
    std::ostringstream out;
    out << "{\"projects\":[";
    for (size_t i = 0; i < projects.size(); ++i) {
        if (i > 0) out << ",";
        out << "{"
            << "\"id\":\""   << json_escape(projects[i].id)   << "\","
            << "\"name\":\"" << json_escape(projects[i].name) << "\","
            << "\"root\":\"" << json_escape(projects[i].root) << "\""
            << "}";
    }
    out << "]}";
    return out.str();
}

std::string codex_conversations_json(const std::vector<CodexConversationRecord>& conversations) {
    std::ostringstream out;
    out << "{\"conversations\":[";
    for (size_t i = 0; i < conversations.size(); ++i) {
        const auto& c = conversations[i];
        if (i > 0) out << ",";
        out << "{"
            << "\"id\":\""                << json_escape(c.id)                << "\","
            << "\"title\":\""             << json_escape(c.title)             << "\","
            << "\"project_id\":\""        << json_escape(c.project_id)        << "\","
            << "\"project_name\":\""      << json_escape(c.project_name)      << "\","
            << "\"working_directory\":\"" << json_escape(c.working_directory) << "\","
            << "\"codex_session_id\":\""  << json_escape(c.codex_session_id)  << "\","
            << "\"maintenance_mode\":"    << (c.maintenance_mode ? "true" : "false") << ","
            << "\"closed\":"              << (c.closed            ? "true" : "false") << ","
            << "\"created_at\":"          << c.created_at << ","
            << "\"updated_at\":"          << c.updated_at
            << "}";
    }
    out << "]}";
    return out.str();
}

std::string codex_conversation_snapshot_json(const CodexConversationSnapshot& snapshot) {
    std::ostringstream out;
    out << "{"
        << "\"ok\":"        << (snapshot.ok        ? "true" : "false") << ","
        << "\"closed\":"    << (snapshot.closed    ? "true" : "false") << ","
        << "\"truncated\":" << (snapshot.truncated ? "true" : "false") << ","
        << "\"data\":\""    << json_escape(snapshot.data) << "\","
        << "\"cursor\":"    << snapshot.cursor     << ","
        << "\"exit_code\":" << snapshot.exit_code  << ","
        << "\"title\":\""   << json_escape(snapshot.title) << "\""
        << "}";
    return out.str();
}

std::string codex_audit_history_json(const std::vector<CodexAuditEvent>& events) {
    std::ostringstream out;
    out << "{\"events\":[";
    for (size_t i = 0; i < events.size(); ++i) {
        if (i > 0) out << ",";
        out << "{"
            << "\"timestamp\":" << events[i].timestamp << ","
            << "\"kind\":\""    << json_escape(events[i].kind)   << "\","
            << "\"detail\":\""  << json_escape(events[i].detail) << "\""
            << "}";
    }
    out << "]}";
    return out.str();
}

} // anonymous namespace

HttpResponse handle_codex_projects(const RequestContext& ctx, const std::string&) {
    if (ctx.request.method == "GET") {
        if (auto err = ctx.require_permission("codex", PermissionLevel::View)) return *err;
        return json_response(200, codex_projects_json(ctx.codex_projects.projects()));
    }
    if (ctx.request.method == "POST") {
        if (auto err = ctx.require_permission("codex", PermissionLevel::Manage)) return *err;
        auto form = parse_form(ctx.request.body);
        std::string error;
        auto project = ctx.codex_projects.create_project(form["name"], form["root"], &error);
        if (!project) {
            return json_response(400, "{\"error\":\"" + json_escape(error.empty() ? "unable to create project" : error) + "\"}");
        }
        return json_response(200, "{\"ok\":true}");
    }
    return json_response(404, "{\"error\":\"not found\"}");
}

HttpResponse handle_codex_conversations(const RequestContext& ctx, const std::string&) {
    if (ctx.request.method == "GET") {
        if (auto err = ctx.require_permission("codex", PermissionLevel::View)) return *err;
        return json_response(200, codex_conversations_json(ctx.codex_conversations.conversations_for(*ctx.username)));
    }
    if (ctx.request.method == "POST") {
        if (auto err = ctx.require_permission("codex", PermissionLevel::Manage)) return *err;
        auto form = parse_form(ctx.request.body);
        std::string error;
        auto conversation = ctx.codex_conversations.create_conversation(*ctx.username, form["title"], form["project_id"], &error);
        if (!conversation) {
            return json_response(400, "{\"error\":\"" + json_escape(error.empty() ? "unable to create conversation" : error) + "\"}");
        }
        return json_response(200, "{\"ok\":true,\"conversation_id\":\"" + json_escape(conversation->id) + "\"}");
    }
    return json_response(404, "{\"error\":\"not found\"}");
}

HttpResponse handle_codex_conversation_read(const RequestContext& ctx, const std::string& conversation_id) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("codex", PermissionLevel::View)) return *err;
    auto form = parse_form(ctx.request.body);
    const std::uint64_t cursor = static_cast<std::uint64_t>(std::strtoull(form["cursor"].c_str(), nullptr, 10));
    auto snapshot = ctx.codex_conversations.read_conversation(conversation_id, *ctx.username, cursor);
    if (!snapshot) {
        return json_response(404, "{\"error\":\"conversation not found\"}");
    }
    return json_response(200, codex_conversation_snapshot_json(*snapshot));
}

HttpResponse handle_codex_conversation_transcript(const RequestContext& ctx, const std::string& conversation_id) {
    if (ctx.request.method != "GET") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("codex", PermissionLevel::View)) return *err;
    auto transcript = ctx.codex_conversations.transcript_for(conversation_id, *ctx.username);
    if (!transcript) {
        return json_response(404, "{\"error\":\"conversation not found\"}");
    }
    return json_response(200, "{\"ok\":true,\"transcript\":\"" + json_escape(*transcript) + "\"}");
}

HttpResponse handle_codex_conversation_history(const RequestContext& ctx, const std::string& conversation_id) {
    if (ctx.request.method != "GET") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("codex", PermissionLevel::View)) return *err;
    auto events = ctx.codex_conversations.audit_history_for(conversation_id, *ctx.username);
    if (!events) {
        return json_response(404, "{\"error\":\"conversation not found\"}");
    }
    return json_response(200, codex_audit_history_json(*events));
}

HttpResponse handle_codex_conversation_send(const RequestContext& ctx, const std::string& conversation_id) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("codex", PermissionLevel::Manage)) return *err;
    auto form = parse_form(ctx.request.body);
    if (!ctx.codex_conversations.send_message(conversation_id, *ctx.username, form["message"])) {
        return json_response(400, "{\"error\":\"unable to send message\"}");
    }
    return json_response(200, "{\"ok\":true}");
}

HttpResponse handle_codex_conversation_close(const RequestContext& ctx, const std::string& conversation_id) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("codex", PermissionLevel::Manage)) return *err;
    if (!ctx.codex_conversations.close_conversation(conversation_id, *ctx.username)) {
        return json_response(400, "{\"error\":\"unable to close conversation\"}");
    }
    return json_response(200, "{\"ok\":true}");
}

HttpResponse handle_codex_run(const RequestContext& ctx, const std::string&) {
    if (auto err = ctx.require_permission("codex", PermissionLevel::Manage)) return *err;
    auto form = parse_form(ctx.request.body);
    std::string error;
    auto codex_request = codex_request_from_form(form, &error);
    if (!codex_request) {
        return json_response(400, "{\"error\":\"" + json_escape(error.empty() ? "invalid codex prompt" : error) + "\"}");
    }
    const auto result = run_codex_prompt(*codex_request);
    if (result.ok) {
        return json_response(200, codex_result_json(result));
    }
    return json_response(result.timed_out ? 408 : 400,
        "{\"error\":\"" + json_escape(result.output.empty() ? "Codex run failed" : result.output) +
        "\",\"details\":" + codex_result_json(result) + "}");
}

} // namespace cuddle
