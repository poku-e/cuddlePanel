#include "http.h"

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <sodium.h>
#include <sstream>

namespace cuddle {
namespace {

std::optional<std::string> decode_base64_input(const std::string& encoded) {
    if (encoded.empty()) { return std::string(); }
    std::string decoded(encoded.size(), '\0');
    size_t decoded_len = 0;
    if (sodium_base642bin(reinterpret_cast<unsigned char*>(decoded.data()),
                          decoded.size(),
                          encoded.c_str(),
                          encoded.size(),
                          nullptr,
                          &decoded_len,
                          nullptr,
                          sodium_base64_VARIANT_ORIGINAL) != 0) {
        return std::nullopt;
    }
    decoded.resize(decoded_len);
    return decoded;
}

// Shared terminal TOTP check used by all session sub-handlers.
bool terminal_totp_ok(const RequestContext& ctx) {
    return ctx.sessions.terminal_totp_recently_verified(ctx.session_token, std::time(nullptr), 30 * 60);
}

// Parse a terminal dimension (rows/cols) from a form field.
// Returns std::nullopt on non-numeric or out-of-range input.
std::optional<std::uint16_t> parse_dim(const std::string& s, std::uint16_t fallback) {
    if (s.empty()) { return fallback; }
    for (unsigned char c : s) {
        if (!std::isdigit(c)) { return std::nullopt; }
    }
    try {
        long v = std::stol(s);
        if (v < 1 || v > 65535) { return std::nullopt; }
        return static_cast<std::uint16_t>(v);
    } catch (...) { return std::nullopt; }
}

// Parse an unsigned 64-bit cursor offset from a form field.
std::optional<std::uint64_t> parse_cursor(const std::string& s) {
    if (s.empty()) { return std::uint64_t{0}; }
    for (unsigned char c : s) {
        if (!std::isdigit(c)) { return std::nullopt; }
    }
    try {
        return std::stoull(s);
    } catch (...) { return std::nullopt; }
}

} // anonymous namespace

HttpResponse handle_terminal_create(const RequestContext& ctx, const std::string&) {
    if (auto err = ctx.require_permission("terminal", PermissionLevel::Manage)) return *err;
    if (!terminal_totp_ok(ctx)) {
        return json_response(403, "{\"error\":\"terminal otp verification required\"}");
    }
    auto form = parse_form(ctx.request.body);
    const auto rows_opt = parse_dim(form["rows"], 24);
    const auto cols_opt = parse_dim(form["cols"], 80);
    if (!rows_opt || !cols_opt) {
        return json_response(400, "{\"error\":\"invalid terminal dimensions\"}");
    }
    auto session_id = ctx.terminal.create_session(*ctx.username, *rows_opt, *cols_opt);
    if (!session_id) {
        return json_response(500, "{\"error\":\"unable to create terminal session\"}");
    }
    return json_response(200, "{\"ok\":true,\"session_id\":\"" + json_escape(*session_id) + "\"}");
}

HttpResponse handle_terminal_read(const RequestContext& ctx, const std::string& session_id) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("terminal", PermissionLevel::Manage)) return *err;
    if (!terminal_totp_ok(ctx)) {
        return json_response(403, "{\"error\":\"terminal otp verification required\"}");
    }
    auto form = parse_form(ctx.request.body);
    const auto cursor_opt = parse_cursor(form["cursor"]);
    if (!cursor_opt) {
        return json_response(400, "{\"error\":\"invalid cursor\"}");
    }
    auto snapshot = ctx.terminal.read_session(session_id, *ctx.username, *cursor_opt);
    if (!snapshot) {
        return json_response(404, "{\"error\":\"terminal session not found\"}");
    }
    std::ostringstream out;
    out << "{\"ok\":true"
        << ",\"data\":\""      << json_escape(snapshot->data) << "\""
        << ",\"cursor\":"      << snapshot->cursor
        << ",\"closed\":"      << (snapshot->closed   ? "true" : "false")
        << ",\"truncated\":"   << (snapshot->truncated ? "true" : "false")
        << ",\"exit_code\":"   << snapshot->exit_code
        << "}";
    return json_response(200, out.str());
}

HttpResponse handle_terminal_write(const RequestContext& ctx, const std::string& session_id) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("terminal", PermissionLevel::Manage)) return *err;
    if (!terminal_totp_ok(ctx)) {
        return json_response(403, "{\"error\":\"terminal otp verification required\"}");
    }
    auto form = parse_form(ctx.request.body);
    std::string input = form["data"];
    if (!form["data_base64"].empty()) {
        auto decoded = decode_base64_input(form["data_base64"]);
        if (!decoded) {
            return json_response(400, "{\"error\":\"invalid terminal input encoding\"}");
        }
        input = *decoded;
    }
    if (input.size() > 8192) {
        return json_response(400, "{\"error\":\"terminal input too large\"}");
    }
    if (!ctx.terminal.write_session(session_id, *ctx.username, input)) {
        return json_response(400, "{\"error\":\"unable to write to terminal session\"}");
    }
    return json_response(200, "{\"ok\":true}");
}

HttpResponse handle_terminal_resize(const RequestContext& ctx, const std::string& session_id) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("terminal", PermissionLevel::Manage)) return *err;
    if (!terminal_totp_ok(ctx)) {
        return json_response(403, "{\"error\":\"terminal otp verification required\"}");
    }
    auto form = parse_form(ctx.request.body);
    const auto rows_opt = parse_dim(form["rows"], 24);
    const auto cols_opt = parse_dim(form["cols"], 80);
    if (!rows_opt || !cols_opt) {
        return json_response(400, "{\"error\":\"invalid terminal dimensions\"}");
    }
    if (!ctx.terminal.resize_session(session_id, *ctx.username, *rows_opt, *cols_opt)) {
        return json_response(400, "{\"error\":\"unable to resize terminal session\"}");
    }
    return json_response(200, "{\"ok\":true}");
}

HttpResponse handle_terminal_close(const RequestContext& ctx, const std::string& session_id) {
    if (ctx.request.method != "POST") {
        return json_response(404, "{\"error\":\"not found\"}");
    }
    if (auto err = ctx.require_permission("terminal", PermissionLevel::Manage)) return *err;
    if (!terminal_totp_ok(ctx)) {
        return json_response(403, "{\"error\":\"terminal otp verification required\"}");
    }
    if (!ctx.terminal.close_session(session_id, *ctx.username)) {
        return json_response(400, "{\"error\":\"unable to close terminal session\"}");
    }
    return json_response(200, "{\"ok\":true}");
}

} // namespace cuddle
