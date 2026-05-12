# Authentication And Admin Foundation

Purpose: define the first cuddlePanel security boundary for all administrator pages and AJAX APIs. The dashboard exists to manage a server, so every feature must pass through server-side authentication and page permission checks before any UI state matters.

Routes and access:
- `GET /`: redirects to `/onboarding` until the first superuser exists, otherwise `/login`.
- `GET /onboarding`: public only while no user exists.
- `GET /api/setup/status`: public only while no user exists.
- `POST /api/onboarding`: creates the first superuser and disables onboarding.
- `GET /login` and `POST /api/login`: public login flow.
- `GET /2fa/setup`: requires a signed-in session that still needs TOTP setup.
- `GET /api/2fa/setup-info`: requires a signed-in session that still needs TOTP setup.
- `POST /api/2fa/confirm`: requires a signed-in session that still needs TOTP setup.
- `POST /api/2fa/verify-terminal`: requires a signed-in session and a configured TOTP secret.
- `POST /api/logout`: requires a valid session.
- `GET /dashboard`: requires a session.
- `GET /api/page/<page>`: requires a session and page permission.
- `GET /api/users`: requires `users:view`.
- `POST /api/users`: requires `users:manage`.
- `POST /api/users/<username>/permissions`: requires `users:manage`.
- `DELETE /api/users/<username>`: requires `users:manage`.
- `GET /api/services`: requires `services:view`.
- `GET /api/services/<unit>`: requires `services:view`.
- `GET /api/services/<unit>/page`: requires `services:view`.
- `GET /api/services/<unit>/unit-file`: requires `services:view`.
- `POST /api/services/<unit>/unit-file`: requires `services:manage`.
- `POST /api/services/<unit>/action`: requires `services:manage`.
- `GET /api/system/users`: requires `system:view`.
- `GET /api/system/users/<username>/logfiles`: requires `system:view`.
- `POST /api/system/files/browse`: requires `system:view`.
- `POST /api/system/users`: requires `system:manage`.
- `POST /api/system/users/<username>/edit`: requires `system:manage`.
- `POST /api/system/users/<username>/security`: requires `system:manage`.
- `POST /api/system/users/<username>/action`: requires `system:manage`.
- `GET /api/system/users/<username>/authorized-keys`: requires `system:manage`.
- `POST /api/system/users/<username>/authorized-keys`: requires `system:manage`.
- `POST /api/system/path-action`: requires `system:manage`.
- `POST /api/system/files/action`: requires `system:manage`.
- `GET /api/nginx/sites`: requires `nginx:view`.
- `POST /api/nginx/sites`: requires `nginx:manage`.
- `POST /api/nginx/sites/<name>`: requires `nginx:manage`.
- `POST /api/nginx/sites/<name>/action`: requires `nginx:manage`.
- `GET /api/codex/projects`: requires `codex:view`.
- `POST /api/codex/projects`: requires `codex:manage`.
- `GET /api/codex/conversations`: requires `codex:view`.
- `POST /api/codex/conversations`: requires `codex:manage`.
- `POST /api/codex/conversations/<id>/read`: requires `codex:view`.
- `GET /api/codex/conversations/<id>/transcript`: requires `codex:view`.
- `GET /api/codex/conversations/<id>/history`: requires `codex:view`.
- `POST /api/codex/conversations/<id>/send`: requires `codex:manage`.
- `POST /api/codex/conversations/<id>/close`: requires `codex:manage`.
- `POST /api/codex/run`: requires `codex:manage`.
- `POST /api/deploy/run`: requires `deploy:manage`.
- `POST /api/terminal/session`: requires `terminal:manage` and a fresh terminal OTP verification.
- `POST /api/terminal/session/<id>/read`: requires `terminal:manage` and a fresh terminal OTP verification.
- `POST /api/terminal/session/<id>/write`: requires `terminal:manage` and a fresh terminal OTP verification.
- `POST /api/terminal/session/<id>/resize`: requires `terminal:manage` and a fresh terminal OTP verification.
- `POST /api/terminal/session/<id>/close`: requires `terminal:manage` and a fresh terminal OTP verification.
- `GET /partials/<name>` and static assets: public template assets without privileged data.

Workflow:
- First boot shows onboarding because `data/users.db` has no users.
- Onboarding now acts as a first-run setup screen that loads essential runtime environment values plus dependency checks before the first account is created.
- The first account is created as `superuser` with `dashboard:manage`, then immediately enters TOTP setup.
- Passwords are hashed with libsodium Argon2id (`crypto_pwhash_STRPREFIX`) and verified with libsodium.
- Login issues an opaque random session id stored server-side and returned as an `HttpOnly`, `SameSite=Strict` cookie.
- The superuser must confirm a TOTP secret during onboarding.
- Additional users are created without a confirmed TOTP secret and are forced through TOTP setup on their first successful password login.
- Dashboard navigation uses AJAX calls to `/api/page/<page>`; page content is returned without a full reload.
- Dashboard navigation keeps the current page in browser state, so refreshes and revisits reopen the last selected admin page instead of always dropping back to the dashboard root.
- Login now strips any accidental `username` or `password` query params from the browser address bar before submission and preserves only a safe dashboard hash target for post-auth navigation.
- HTTP routing now ignores URL query strings when matching page paths, and `GET /login?...` is actively redirected to a clean `/login` URL so leaked credential-style query params are removed before the login page is served.
- If a remembered or deep-linked service or system-user page no longer exists, the dashboard falls back to the parent list page instead of leaving the operator on a hard 404 after login.
- Dashboard page bodies are rendered from `templates/pages/*.html`, while `static/app.js` now boots a set of smaller `static/js/*` frontend modules after injection.
- Dashboard page loads now show a shared loading overlay with a spinner and status message over the main content region while AJAX page fetches and page initializers are still running.
- Auth screens and dashboard actions now use a shared toast notification layer for success and error feedback in addition to any inline status text.
- All privileged pages are checked on the server before content is returned.
- The Users page loads dashboard users into a dense table, opens create and permission-edit flows in Bootstrap modals, and performs mutations through AJAX without leaving `/dashboard`.
- User deletion now requires an explicit browser confirmation before the dashboard sends the destructive request.
- The Services page discovers host systemd services, shows quick runtime actions in the index table, and opens each service into a dedicated tabbed detail page for runtime, structured unit editing, direct file editing, and enablement control.
- The System page loads host accounts into a table, uses Bootstrap tabs for account vs. file workflows, manages account edits, password and lifecycle controls, a richer privilege posture view with group-based sudo actions, and account deletion through AJAX, and constrains path ownership and mode changes to configured roots.
- The System page also provides an `authorized_keys` editor, a read-only `Logfiles` tab for supported shell history files under each login user's resolved home directory, and a constrained file browser for allowed roots.
- The Nginx page loads allowlisted site configs into a table, edits full config content in a Bootstrap modal, and performs file updates, enable/disable, config tests, and reloads through AJAX.
- The Deploy page now runs a native stack-aware deploy engine through `/api/deploy/run`, writing systemd and nginx configuration directly and optionally creating or updating a Cloudflare DNS record.
- The Codex page now manages project-scoped conversations, starts an interactive Codex CLI session in the selected project root or maintenance workdir, persists transcript and audit data, and resumes the same Codex thread after a panel restart when a stored session id is available.
- The Terminal page requires a fresh TOTP verification every 30 minutes before access is granted.
- After terminal verification, the Terminal page creates a PTY-backed session and exchanges input/output through explicit session APIs.

TOTP workflow:
- TOTP secrets are generated in base32 form and are compatible with standard TOTP apps through an `otpauth://` URI.
- A user with no confirmed TOTP secret is redirected to `/2fa/setup` after onboarding or after a successful first password login.
- TOTP confirmation requires a valid current code before the session is allowed to proceed to `/dashboard`.
- Terminal access uses a separate verification timestamp in the server-side session and expires 30 minutes after a successful OTP check.

User management workflow:
- Only authenticated users with `users:manage` can create, delete, or modify user permissions.
- Creating a user requires a valid username, an Argon2id-hashable strong password, and a role from an allowlist.
- Permission updates accept only known page keys and only `none`, `view`, or `manage`.
- Superuser accounts are intentionally protected from deletion in this phase to avoid lockout and unsafe privilege editing.
- Users with only `users:view` can inspect the user list but cannot submit mutations.

Hidden dependencies and configuration:
- Requires libsodium development headers and library at build time.
- Requires OpenSSL libcrypto for HMAC-SHA1 TOTP generation and verification.
- Uses locally vendored compiled Bootstrap CSS at `static/vendor/bootstrap/5.3.8/bootstrap.min.css` plus the Bootswatch Pulse theme at `static/vendor/bootswatch/5.3.8/pulse.bootstrap.min.css`, so the UI keeps a consistent themed Bootstrap layer without depending on a CSS CDN at runtime.
- A checked-in repo `.env` file now lists every runtime environment variable, is auto-loaded by the server from the current working directory when present, and is written during first-run setup.
- Dashboard shell templates live under `templates/`, and page-specific AJAX content templates live under `templates/pages/`.
- Frontend behavior is split between `static/js/core/` helpers, `static/js/pages/` page modules, and the top-level `static/app.js` bootstrap entrypoint.
- Shared frontend notifications live in `static/js/core/toast.js` and are mounted dynamically into the active page body.
- Runtime state lives in `data/users.db`, created with `0600` permissions when possible.
- Service unit editing is constrained to discovered fragment files under the allowed service-unit roots and can be overridden for tests or controlled deployments with `CUDDLEPANEL_SERVICE_UNIT_ROOTS`.
- System account data reads from the configured passwd, group, and shadow files, and system file actions are limited by `CUDDLEPANEL_SYSTEM_ALLOWED_ROOTS`.
- System account deletion uses the configured `userdel` binary and only removes home directories when the request explicitly opts into the recursive delete flag.
- Nginx registry state lives in `data/nginx.db`.
- The deploy engine uses configured paths for `systemctl`, `certbot`, `python3`, `npm`, `node`, `go`, `curl`, and the target systemd unit directory.
- The terminal shell defaults to `/bin/bash` and can be overridden with `CUDDLEPANEL_TERMINAL_SHELL`.
- Terminal sessions drop to `nobody:nogroup` by default, run from `/tmp`, and support configurable session count plus idle/runtime limits.
- The Codex runner defaults to `/usr/bin/codex`, uses the configured `CUDDLEPANEL_CODEX_WORKDIR` as the maintenance-mode workdir, and can set an optional model plus timeout through environment variables.
- Codex conversation metadata lives in `data/codex_conversations.db`, transcripts live in `data/codex_transcripts/`, and audit history lives in `data/codex_audit/`.
- Cookie `Secure` is enabled when `CUDDLEPANEL_SECURE_COOKIES=1`.
- The listen port defaults to `8080` and can be overridden with `CUDDLEPANEL_PORT`.
- Shell-exported variables take precedence over values from the auto-loaded `.env` file.
- The server now writes startup and runtime diagnostics to stderr and appends the same lines to `data/server.log`.

Permissions:
- `view` means the page can be opened and read.
- `manage` means the page can perform mutations when implemented.
- Superuser bypass is intentionally narrow and represented by role plus explicit dashboard manage permission.

Implemented page keys:
- `dashboard`
- `users`
- `services`
- `system`
- `nginx`
- `terminal`
- `codex`
- `deploy`

Gotchas and debugging:
- Do not add management actions that shell out until the handler has both a page `manage` check and strict allowlisted input validation.
- Direct service editing is intentionally limited to discovered fragment paths under approved roots. Do not widen it to arbitrary paths or drop-in creation without a separate safety design.
- The System page manages `sudo` group membership, not arbitrary `/etc/sudoers` edits. Keep that distinction clear in both code and UI.
- UI hiding is never authorization. Add checks in the C++ route before any action.
- Interactive terminal support should use a PTY implementation with streaming WebSocket/SSE and a constrained execution policy; plain command passthrough is not acceptable.
- Nginx config editing must normalize paths under configured `sites-available` and `sites-enabled` directories before file access.
- When adding more user roles later, update both the server-side role allowlist and the permission editor defaults together.
- If a user record predates TOTP support, treat the missing secret as a first-login setup requirement instead of silently bypassing 2FA.
- If the process exits during startup, check `data/server.log` first. Port bind failures, invalid port configuration, request parsing failures, and unexpected server exceptions are logged there with UTC timestamps.
