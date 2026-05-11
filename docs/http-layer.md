# HTTP Layer Architecture

Purpose: document the internal design of cuddlePanel's hand-rolled HTTP server so that future agents can add routes, handlers, or security fixes without re-deriving the structure from scratch.

Access scope: internal C++ only; no external HTTP framework is used. All socket, request parsing, routing, and response serialisation lives in `src/http.cpp` and `src/http_handlers_*.cpp`.

---

## File layout

| File | Responsibility |
|---|---|
| `include/http.h` | All shared HTTP types, `SessionStore`, `RequestContext`, `Route`/`RouteHandler`, `App` class declaration, free-function declarations |
| `src/http.cpp` | Socket server loop, request parsing, response serialisation, `SessionStore` implementation, `RequestContext` helpers, forward declarations for all handlers, `App::build_routes()`, `App::handle()` |
| `src/http_handlers_auth.cpp` | Login, logout, onboarding, TOTP setup/confirm/verify |
| `src/http_handlers_page.cpp` | `/api/page/<name>` — renders dashboard page body HTML |
| `src/http_handlers_users.cpp` | Panel user management |
| `src/http_handlers_services.cpp` | Systemd service management |
| `src/http_handlers_nginx.cpp` | Nginx site management |
| `src/http_handlers_system.cpp` | Host account and file ownership management |
| `src/http_handlers_terminal.cpp` | PTY session management |
| `src/http_handlers_codex.cpp` | Codex project and conversation management |
| `src/http_handlers_deploy.cpp` | Native deploy engine trigger |

All handler files are compiled into the `cuddle_core` static library (`CMakeLists.txt`).

---

## Route table

Routes are registered as a `std::vector<Route>` in `App::build_routes()` and tested in registration order by `App::handle()`.

```cpp
struct Route {
    std::string  method;   // HTTP method to match; empty = any method
    std::string  prefix;   // matched against the start of request.path
    std::string  suffix;   // if non-empty, matched against end of tail after prefix
    bool         exact;    // if true, full path must equal prefix exactly
    RouteHandler handler;  // std::function<HttpResponse(const RequestContext&, const std::string&)>
};
```

Dispatch logic (in `App::handle()`):

1. If `route.method` is set and does not match `request.method`, skip.
2. If `route.exact == true`, match only when `request.path == route.prefix`; pass `""` as the id.
3. If `route.exact == false`, check that `request.path` starts with `route.prefix`.
4. Extract `tail = path.substr(prefix.size())`.
   - If `route.suffix` is set: tail must end with suffix; the segment between prefix and suffix is url-decoded and passed as the id.
   - If no suffix: tail must be non-empty; the entire tail is url-decoded and passed as the id.

Registration examples:

```cpp
// Exact match, POST-only, no id
routes_.push_back({"POST", "/api/login", "", true, handle_login_post});

// Prefix+suffix match: extracts username from /api/users/<username>/permissions
routes_.push_back({"", "/api/users/", "/permissions", false, handle_user_permissions});

// Prefix-only match: extracts session id from /api/terminal/session/<id>/read
routes_.push_back({"", "/api/terminal/session/", "/read", false, handle_terminal_read});
```

---

## RequestContext

Constructed once per request in `App::handle()` before route dispatch. Passed by const reference to every handler.

```cpp
struct RequestContext {
    const HttpRequest&         request;         // raw HTTP request
    std::string                session_token;   // raw cookie value (may be empty)
    std::optional<std::string> username;        // set if session is valid
    UserStore&                 users;
    ServiceStore&              services;
    NginxStore&                nginx;
    SystemAdmin&               system_admin;
    TerminalManager&           terminal;
    CodexProjectStore&         codex_projects;
    CodexConversationManager&  codex_conversations;
    SessionStore&              sessions;
};
```

Helpers:

- `ctx.require_auth()` — returns `401 json_response` if `username` is not set, else `nullopt`.
- `ctx.require_permission(page, level)` — calls `require_auth()` then checks `users.has_permission()`; returns `403` if denied, else `nullopt`.

Typical handler pattern:

```cpp
HttpResponse handle_example(const RequestContext& ctx, const std::string& id) {
    if (auto err = ctx.require_permission("example", PermissionLevel::Manage)) return *err;
    // ... use ctx.users, ctx.services, etc.
    return json_response(200, "{\"ok\":true}");
}
```

---

## Pre-route guards in App::handle()

Before the route table is consulted, `App::handle()` performs these checks in order:

1. **Null-byte / path traversal rejection** — any path containing `\0` or `..` is rejected with 400 immediately.
2. **Page navigation shortcuts** — `GET /`, `/onboarding`, `/login`, `/2fa/setup`, `/dashboard` are handled inline before a session is needed for routing logic (but still enforce appropriate redirect rules).
3. **Partial templates** — `GET /partials/*` serves `templates/<path>.html`; path is safe because `..` is already blocked.
4. **Static assets** — `GET /static/*` is served through `static_asset_path()` which also rejects `..`; MIME type is resolved by file extension.
5. **TOTP-setup fence** — after building the `RequestContext`, if the session has a pending TOTP setup all routes except `/api/2fa/setup-info`, `/api/2fa/confirm`, and `/api/logout` return 403.

---

## Adding a new route

1. Write the handler as a free function in the appropriate `src/http_handlers_<domain>.cpp`:
   ```cpp
   namespace cuddle {
   HttpResponse handle_my_thing(const RequestContext& ctx, const std::string& id) {
       if (auto err = ctx.require_permission("mything", PermissionLevel::Manage)) return *err;
       // ...
       return json_response(200, "{\"ok\":true}");
   }
   } // namespace cuddle
   ```
2. Add a forward declaration in the forward-declarations block of `src/http.cpp`.
3. Register the route in `App::build_routes()`.
4. If this is a new domain file, add it to `add_library(cuddle_core ...)` in `CMakeLists.txt`.

---

## Response factories and utilities

All declared in `include/http.h`, defined in `src/http.cpp`, usable from every handler file:

| Symbol | Purpose |
|---|---|
| `json_response(status, body)` | Sets `application/json; charset=utf-8` |
| `redirect_response(location)` | 302 with `Location` header |
| `page_response(file)` | Renders a template file as `text/html` |
| `json_escape(s)` | JSON string escaping, including `\u00xx` for control bytes |
| `html_escape(s)` | HTML entity encoding for `&`, `<`, `>`, `"`, `'` |
| `url_decode(s)` | `%XX` / `+` decoding |
| `parse_form(body)` | `application/x-www-form-urlencoded` → `map<string,string>` |

All responses automatically carry `X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY`, `Referrer-Policy: no-referrer`, `Cache-Control: no-store`, and `Connection: close`.

---

## SessionStore

In-memory, mutex-guarded. Sessions are not persisted across restarts (all active sessions are lost on process restart). The store lives in `src/http.cpp`.

Session state per token:
- `username` — the authenticated user
- `pending_totp_setup` — true until the user has confirmed their first TOTP code
- `terminal_totp_verified_at` — timestamp of the last successful terminal OTP; checked against a 30-minute window

---

## Gotchas

- **Never route-dispatch on prefix alone when an exact match is intended.** Use `exact=true` so that `/api/login` can't match `/api/login-extra`.
- **Handler files must stay inside `namespace cuddle`.** The forward declarations in `http.cpp` are in that namespace; a mismatched namespace produces a linker error for undefined symbols.
- **Handler signature must be exactly** `HttpResponse f(const RequestContext&, const std::string&)`. The id parameter is always present even when the route uses `exact=true` (it will be `""`).
- **Suffix routes require non-empty tail content between prefix and suffix.** A request to `/api/users//permissions` (empty username) will fall through and return 404. Validate the id value inside the handler if needed.
- **`ctx.username` is `std::optional`.** Always call `require_auth()` or `require_permission()` before dereferencing it with `*ctx.username`.
- **Static assets get `Cache-Control: no-store` too.** For large vendored assets this is intentional (no sensitive data should be cached). If a CDN is added later, apply a `Cache-Control: public, max-age=...` header specifically for those responses instead of changing the default.
- **Request body size is capped at 1 MiB** in `read_request()`. Endpoints that need larger uploads (e.g. nginx config writes) must stay within that limit or the request will be dropped before the handler is reached.
