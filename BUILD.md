## Initial Backend And Dashboard Foundation

- Added a C++17 backend scaffold with CMake and `make build` producing `bin/server`.
- Implemented first-run onboarding, Argon2id password hashing through libsodium, login, server-side sessions, and secure cookie defaults.
- Added server-side page permission checks for the dashboard surface and initial page keys for users, services, system, nginx, terminal, codex, and deploy.
- Added Bootstrap-based templates, reusable sidebar/header/footer partials, and AJAX page loading without full refreshes inside the dashboard.
- Documented the authentication/admin route model, hidden dependencies, permissions, and security gotchas in `docs/auth-and-admin.md`.
- Hardened the HTTP loop against client disconnects by ignoring `SIGPIPE` during response writes.

## Local Bootstrap CSS

- Downloaded Bootstrap `5.3.8` compiled CSS into `static/vendor/bootstrap/5.3.8/`.
- Updated onboarding, login, and dashboard templates to load the local Bootstrap CSS instead of the jsDelivr CSS URL.
- Documented the local Bootstrap runtime dependency in `docs/auth-and-admin.md`.

## Project Spec In AGENTS

- Appended `AGENTS.md` with the cuddlePanel product specification, covering stack choices, AJAX navigation expectations, required admin features, and extra safety constraints for high-risk server operations.

## Phase 2 User Management

- Added phase 2 user-management docs covering AJAX routes, permission enforcement, role constraints, and superuser protection rules.
- Implemented user creation, deletion, and per-page permission updates in the C++ backend with server-side `users:view` and `users:manage` checks.
- Replaced the Users placeholder page with a Bootstrap/AJAX management UI for listing users, creating users, and editing per-page access without a full reload.
- Expanded tests to cover non-superuser creation, duplicate rejection, superuser creation rejection, permission updates, and protected-user deletion rules.

## Phase 3 Service Management

- Added `docs/services-management.md` to define the allowlisted systemd service workflow, validation rules, and runtime command constraints.
- Implemented a persisted service registry in `data/services.db` with create and edit support for panel service entries.
- Added services APIs and a dashboard Services page for viewing status, updating service metadata, and running `start`, `stop`, and `restart` actions through `/bin/systemctl`.
- Added focused service tests for registry validation and update behavior.

## Phase 4 Nginx Management

- Added `docs/nginx-management.md` to define the allowlisted nginx site workflow, directory constraints, and runtime validation rules.
- Implemented a persisted nginx site registry in `data/nginx.db` plus constrained file writes under configured `sites-available` and `sites-enabled` roots.
- Added nginx APIs and a dashboard Nginx page for creating/editing site configs, enabling or disabling symlinks, and running config test and reload actions.
- Hardened dashboard card rendering by escaping dynamic content before injecting user, service, and nginx data into HTML.
- Added focused nginx tests for filename validation, config persistence, symlink enable/disable behavior, and rename handling.

## Phase 5 Deploy Helper

- Added `docs/deploy-management.md` to mirror the host deploy helper contract, including the discovered default script path at `/usr/local/sbin/deploy-site`.
- Implemented a validated deploy runner with direct `exec` execution, non-interactive mode, and captured output for dashboard use.
- Replaced the Deploy placeholder page with an AJAX form that mirrors the deploy helper flags and shows returned output inline.
- Added focused deploy tests covering request validation, argument building, and runner execution with an overridden binary path.

## Branding Update

- Updated onboarding, login, and dashboard templates to use `static/cuddle_logo.png` as the favicon.
- Added `cuddle_logo` to the auth views and sidebar brand lockup for visible site branding.

## Phase 6 TOTP Security

- Added TOTP-based 2FA setup for onboarding and first login, using base32 secrets and standard `otpauth://` URIs compatible with common authenticator apps.
- Extended user persistence and server-side sessions to track TOTP setup state and a separate 30-minute terminal OTP verification window.
- Added a dedicated 2FA setup page plus a terminal OTP gate in the dashboard flow.
- Expanded auth tests to cover TOTP generation/verification, 2FA persistence state, and session expiry tracking for terminal access.

## Phase 7 PTY Terminal

- Added `docs/terminal-management.md` to define the PTY workflow, OTP gate, session APIs, and shell/runtime constraints.
- Implemented a PTY-backed terminal session manager with ownership checks, bounded output buffering, resize support, and explicit close semantics.
- Replaced the terminal placeholder with an xterm.js-powered browser terminal that creates, polls, writes to, resizes, and closes PTY sessions over AJAX.
- Added focused terminal tests covering session creation, echoed output, ownership enforcement, resize, and close behavior.

## Terminal Hardening

- Hardened terminal sessions to drop privileges to `nobody:nogroup` by default, start in `/tmp`, and use a minimal child environment with `no_new_privs`.
- Added configurable per-user session limits plus idle and max-lifetime expiry for PTY sessions.
- Surfaced the active terminal runtime policy in the terminal UI so operators can see the shell account and working directory.

## Phase 8 System Administration

- Added `docs/system-management.md` to define the host-account workflow, sudo-group scope, allowed-root path actions, and runtime command dependencies.
- Implemented a constrained system-administration module for listing host accounts, creating users, locking or unlocking accounts, granting or revoking `sudo` membership, and running allowlisted `chown` or `chmod` actions.
- Replaced the System placeholder page with an AJAX management UI for host account actions and path ownership or mode changes.
- Added focused system administration tests covering passwd/group/shadow parsing, allowed-root path validation, protected root actions, and command dispatch with overridden binaries.

## SSH authorized_keys Management

- Extended the System page with a dedicated editor for `~/.ssh/authorized_keys` on login users only.
- Added server-side read and write support that derives the target path from the user's home directory, creates `.ssh` with `0700`, writes `authorized_keys` with `0600`, and reapplies the target uid and gid.
- Documented the new routes and login-user restrictions in the system and auth/admin docs.

## Frontend Template Extraction

- Moved dashboard page body markup for dashboard, users, services, system, nginx, terminal, codex, and deploy into dedicated `templates/pages/*.html` files.
- Simplified `src/http.cpp` so it now renders those page templates with small value maps instead of assembling large inline HTML strings.
- Documented the split between dashboard shell templates and page-body templates in the auth/admin doc.

## Frontend JS Modularization

- Split the monolithic `static/app.js` into smaller ES modules under `static/js/core/`, `static/js/pages/`, plus focused `auth` and `dashboard` entry modules.
- Updated auth, onboarding, TOTP setup, and dashboard templates to use the module bootstrap with `data-view` detection instead of inline global boot calls.
- Kept the existing AJAX behavior intact while making page-specific frontend logic easier to extend without growing one giant script file.

## Toast Notifications

- Added a shared toast notification module and styling for success and error feedback across auth screens and dashboard actions.
- Wired toast notifications into user, service, system, nginx, deploy, and terminal actions while keeping existing inline status text in place for local context.

## Release Build Hardening

- Updated `make build` to configure a `Release` build by default.
- Added release-oriented compiler and linker settings for GNU/Clang builds, including `-O3`, hidden symbol visibility, function and data section splitting, linker garbage collection, and stripped server output.
- Enabled release IPO/LTO automatically when the toolchain supports it.

## UPX Packaging

- Added optional UPX packing for the final `server` binary as a post-build step using `--best --lzma`.
- Updated `make build` to request UPX packing by default and emit a clear CMake warning when `upx` is not installed on the build host.

## Environment File

- Added a checked-in `.env` file that enumerates all runtime environment variables used by the app with their default values and intent.
- Updated `.gitignore` so the repo `.env` can be tracked.

## .env Autoload

- Updated the server startup path to auto-load `.env` from the current working directory before reading runtime configuration.
- Kept shell-exported environment variables higher priority than `.env` values so explicit operator overrides still win.
