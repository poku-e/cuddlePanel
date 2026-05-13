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

## First-Run Setup

- Expanded onboarding into a first-run setup workflow that loads dependency status, configures essential runtime environment values, and creates the first superadmin in one screen.
- Added setup config parsing and `.env` writing helpers so onboarding can validate and persist the essential runtime configuration before completing.
- Documented the bootstrap workflow and dependency checks in `docs/first-run-setup.md`.

## README

- Added a top-level `README.md` with the `cuddle_logo.png` branding at the top, a concise feature overview, project layout, build/run commands, and configuration notes.

## Host Bootstrap Script

- Added `scripts/install-deps.sh` for Ubuntu/Debian hosts to install common build and runtime dependencies, prepare runtime directories, and seed `.env` if missing.
- Documented the relationship between the host bootstrap script and the browser-based first-run setup in the README and first-run setup doc.

## AGENTS README Rule

- Appended `AGENTS.md` to require checking `README.md` as part of required context and updating it whenever a change affects project overview, setup, configuration, structure, or major features.

## Startup Logging And Error Handling

- Added a shared runtime logger that writes UTC-stamped messages to stderr and appends them to `data/server.log`.
- Hardened startup and socket bind/listen diagnostics so immediate exits now report the failing step and `errno` detail.
- Added request-loop exception handling plus safer `Content-Length` parsing so malformed input and unexpected server errors are logged instead of failing silently.

## Codex Page Implementation

- Added a real Codex runner with prompt validation, fixed server-side binary and workspace configuration, timeout handling, captured final agent messages, and git-based change reporting.
- Implemented `POST /api/codex/run`, a full dashboard Codex page UI, and a dedicated frontend module for prompt submission plus result rendering.
- Extended onboarding, `.env`, `.env.example`, README, and internal docs to include Codex runtime configuration and operator guidance.

## Project-Scoped Codex Conversations

- Added persistent Codex project metadata plus conversation metadata so the dashboard can start Codex threads in an explicit project root or in maintenance mode when no project is selected.
- Reworked the Codex page into a live conversation UI backed by PTY-based interactive Codex sessions, with streamed output, follow-up prompts, and in-thread approval responses.
- Added new Codex project and conversation APIs, updated docs and README, and covered the new PTY conversation manager with focused tests.

## Native Deploy Engine

- Replaced the old deploy-script wrapper with a native deploy engine that writes systemd units and nginx site configs directly for Node.js, Golang, Streamlit, and Python + Vite stacks.
- Added stack-specific deploy form fields, native build and dependency-install steps, and optional Cloudflare DNS create-or-update support using the official DNS record API through a temporary curl config.
- Expanded first-run configuration and tracked env files with deploy tool paths and optional Cloudflare defaults, and updated deploy, auth/admin, first-run, and README docs to match the new workflow.

## Codex Conversation Resilience

- Added persisted Codex session ids to conversation metadata so the panel can attempt `codex resume` and keep the same thread after a cuddlePanel restart.
- Added transcript persistence under `data/codex_transcripts/` plus audit history under `data/codex_audit/`, along with new Codex APIs for transcript export and history inspection.
- Updated the Codex dashboard page to show conversation history and export transcripts, and expanded the Codex chat tests to cover transcript storage and restart-time resume behavior.

## Terminal JSON Escaping

- Hardened the shared JSON string encoder so all control bytes below `0x20` are emitted as `\u00xx` escapes instead of raw bytes.
- This prevents PTY output containing ANSI escape sequences or bell/control characters from breaking terminal page AJAX responses.
- Added a focused terminal test that exercises JSON escaping with real terminal control bytes.

## System Page Layout Refresh

- Reworked the System administration page into `Accounts` and `Files` tabs so host-user actions and file-level tools no longer compete in one long stacked layout.
- Moved host account creation, constrained path actions, and `authorized_keys` editing into modal workflows while keeping the account list visible and mobile-friendly.
- Updated the System page frontend module and styling to support tab switching, modal state, summary cards, and cleaner desktop organization.

## Dashboard UX Hardening

- Persisted the active dashboard page in browser state so refreshes and revisits reopen the last selected admin page instead of always falling back to the dashboard root.
- Renamed the terminal secondary action to `New session` and added confirmation before replacing a live shell.
- Added delete confirmation to the Users page, preserved unsaved nginx editor drafts across runtime actions, and reorganized the Deploy page into `App`, `Build`, and `DNS` tabs with automatic scrolling to the output pane during runs.

## Terminal Input Transport

- Switched browser-to-terminal keystroke writes to a base64 transport so `Esc`, control sequences, and other non-printable PTY bytes survive the HTTP form layer intact.
- Updated the terminal write endpoint to decode `data_base64` server-side while keeping the existing size limits and permission checks in place.

## Bootstrap Admin Layout Pass

- Reworked the Users, Services, Nginx, and System pages away from stacked custom cards and into denser Bootstrap-style table layouts with modal-based editing flows.
- Replaced the fragile custom System tabs with Bootstrap nav tabs, and converted account creation, path actions, SSH key editing, service editing, and nginx config editing into Bootstrap modals.
- Flattened the visual styling toward a more restrained enterprise admin look, added shared output accordions for service and nginx runtime feedback, and refreshed the Deploy tabs to use Bootstrap nav-tab presentation.

## Bootswatch Pulse Theme

- Vendored the official Bootswatch Pulse theme locally at `static/vendor/bootswatch/5.3.8/pulse.bootstrap.min.css`.
- Applied Pulse after the base Bootstrap stylesheet on the login, onboarding, 2FA setup, and dashboard shells so the whole UI shares the same themed Bootstrap layer.

## Card Spacing Pass

- Increased shared padding inside auth cards, dashboard cards, and subpanels so the page surfaces have more breathing room without changing the underlying workflows.
- Added a slightly tighter mobile override so smaller screens still feel comfortable without wasting vertical space.

## System Table Refinement

- Gave the System administration table its own visual treatment with roomier cell padding, a softer bordered wrapper, and a clearer striped admin-grid pattern.
- Kept the change scoped to the System page so the denser tables on other operational pages can stay compact while the host-account view becomes easier to scan.

## Deploy Privilege Split

- Hardened the native deploy engine so build and dependency-install steps now execute as the requested deploy user instead of the panel's root context.

## System Users JSON Fix

- Fixed the `/api/system/users` JSON payload so account records close the `expires_on` string correctly and the System dashboard page can render again.
- Added a regression test that calls the real system-users HTTP handler and verifies the response still includes a valid `expires_on` fragment plus `allowedRoots`.

## Dashboard Text Class Cleanup

- Removed Bootstrap text emphasis classes from dashboard page templates and page-specific JavaScript render paths so page text now renders without color or font-weight utility emphasis.
- Kept the cleanup scoped to `templates/pages/*` and `static/js/pages/*`, leaving layout and workflow behavior unchanged.

## System Account Edit Phase 1

- Added a phase-1 account edit flow on the System page for shell, home path, optional home move behavior, GECOS comment, primary group, and supplementary groups.
- Extended the backend system account model and JSON payload to include comment and group membership data, and added a dedicated `usermod`-backed edit endpoint with server-side validation and root-account protection.
- Expanded the focused system admin test to verify parsed account metadata and the generated `usermod` arguments for the new edit workflow.

## System Account Security Phase 2

- Added a phase-2 security modal on the System page for password reset, force-password-change on next login, and account expiration management.
- Extended the backend system account payload with password-change-required and expiration metadata, and added a dedicated security endpoint that uses validated `chpasswd` and `chage` commands while keeping `root` out of scope.
- Expanded the focused system admin test to verify shadow-derived lifecycle metadata plus the generated `chpasswd` and `chage` inputs for the new security workflow.

## System Account List Focus

- Updated the System administration Accounts tab to render only login-enabled host accounts so `nologin` entries no longer crowd the main operator table.
- Sorted any remaining system accounts with interactive shells after normal login users to keep the dashboard focused on day-to-day admin accounts first.
- Documented that the backend still returns the full passwd-derived dataset while the dashboard applies the operator-focused filter client-side.

## System Account Deletion

- Added host-account deletion to the System administration Accounts tab with a dedicated confirmation modal and an explicit checkbox for recursive home-directory removal.
- Extended the backend system user action handler to support a validated `delete` action through `userdel`, with `root` protected from deletion and home removal only enabled when the request opts in.
- Documented the new delete workflow, the `CUDDLEPANEL_USERDEL_BIN` override, and the recursive-delete safety rule in the system administration docs and README.

## System Action Error Surfacing

- Updated the shared AJAX API helpers to surface backend `output` messages on non-2xx responses instead of collapsing system-action failures into the generic `Request failed`.
- This keeps host-account delete failures actionable in the dashboard by showing the underlying `userdel` reason directly in the modal and toast flow.

## Serialized Account Mutations

- Serialized System-page account creation and account-mutation commands inside the backend so overlapping dashboard requests cannot race for `/etc/passwd` or `/etc/group` locks.
- Hardened the delete-account modal to allow only one in-flight submit at a time, reducing accidental duplicate `userdel` requests from repeated clicks.
- Added `CUDDLEPANEL_DEPLOY_ALLOWED_ROOTS` to first-run setup, tracked env files, and deploy validation so native deploys only accept trusted project roots that stay inside configured deploy roots and are not world-writable.
- Updated deploy and first-run docs plus the README to explain the new two-phase deploy model and trusted-root requirement.

## Deploy Child Path Guard

- Hardened Python + Vite deploy builds to canonicalize `vite_root` before running npm commands and reject symlinked working directories that escape the trusted project root.
- Added a focused deploy test that exercises a symlinked child directory escape attempt and verifies the deploy run fails.

## Deploy Unit Escaping

- Hardened native deploy validation to reject control whitespace in systemd-bound fields such as `service_desc`, absolute paths, relative paths, and Go package values.
- Updated unit generation to quote and escape `Description`, `WorkingDirectory`, `Environment`, and `ExecStart` arguments instead of writing raw deploy fields into the systemd unit.

## HTTP Layer Refactor — Route Table, RequestContext, Domain Split

- Replaced the ~250-line if-chain router in `src/http.cpp` with a `std::vector<Route>` table and a 20-line dispatch loop registered via `App::build_routes()`.
- Introduced `RequestContext` (resolved once per request) carrying all store references plus `require_auth()` and `require_permission()` helpers, eliminating per-handler session/permission boilerplate.
- Moved all 38 handler methods off the `App` class into free functions in eight focused domain files: `http_handlers_auth.cpp`, `http_handlers_page.cpp`, `http_handlers_users.cpp`, `http_handlers_services.cpp`, `http_handlers_nginx.cpp`, `http_handlers_system.cpp`, `http_handlers_terminal.cpp`, `http_handlers_codex.cpp`, and `http_handlers_deploy.cpp`.
- `App` class is now reduced to constructor + `handle()` + private store references + `routes_`.
- `src/http.cpp` shrank from 1823 lines to ~400; all nine handler files together total comparable line count but each file covers a single domain.
- Updated `CMakeLists.txt` to include all nine new handler source files in the `cuddle_core` static library.

## Dedicated User Management Page

- Turned the System page into an account index with `Manage` entry points and added a dedicated per-user `User management` page with tabs for Overview, Profile, Security, SSH, Privileges, Files, and Audit.
- Added dynamic dashboard loading for account-specific system pages plus new backend routes for per-user page HTML, account detail JSON, and audit history.
- Kept existing host-account mutations behind the same validated backend commands while reorganizing the UI around a single account-focused workflow instead of several row modals.
- Added a simple panel-local audit trail for successful account mutations, SSH key saves, and user-scoped file actions, stored in `data/system_account_audit.log` by default.

## System Page Error Cleanup

- Removed the temporary raw-response diagnostics from dashboard page-load failures so the System administration UI no longer renders raw JSON or payload fragments to operators.
- Kept invalid-response details in the browser console for debugging while restoring friendly in-page and toast error messages.

## System JSON Fix

- Fixed malformed JSON from the System administration account-list API by restoring the missing closing quote on the `expires_on` field in the host-user serializer.
- Added a small regression check around escaped system-account JSON field formatting while keeping the System admin focused test suite green.

## User Logfiles Tab

- Added a `Logfiles` tab to the dedicated System user-management page so operators can inspect supported shell history files for a host account without leaving the dashboard.
- Added a read-only `GET /api/system/users/<username>/logfiles` endpoint backed by a fixed allowlist of history filenames under the resolved home directory, with home-boundary checks and a 128 KB tail cap per file.
- Updated system management and auth docs plus the README to describe the new per-user logfile workflow.

## User Logfiles Layout

- Reworked the System user `Logfiles` tab into a two-column review layout with a fixed file table on the left and a dedicated scrollable preview pane on the right.
- Added sticky table headers, active-row highlighting, keyboard-selectable logfile rows, and a mobile fallback that stacks the file list above the preview.

## Privileges Tab Refresh

- Redesigned the System user `Privileges` tab into a more advanced posture view with summary cards, cleaner group presentation, and a dedicated group-based sudo action area.
- Kept the backend permission model unchanged while making the privilege state easier to audit on desktop and mobile.

## Files Browser

- Expanded the System user `Files` tab into a constrained file browser with right-click actions for rename, copy/paste, zip, unzip, chown, and chmod inside allowed roots.
- Added server-side browse and file-action endpoints that keep all path resolution inside configured roots, reject unsafe rename components, and avoid shell interpolation for file operations.

## Services Host Discovery

- Reworked the Services workflow to discover host `.service` units from `systemctl` instead of relying on a manually populated panel registry.
- Added dedicated per-service detail pages with tabs for overview, runtime output, and unit metadata, plus direct `enable`, `disable`, `start`, `stop`, and `restart` actions.

## Services List Polish

- Added a responsive live-search field to the Services index so operators can filter discovered services by name, unit, description, or state as they type.
- Tightened the Services table styling to feel less blocky while keeping readable padding, secondary runtime detail, and mobile-friendly spacing.

## Services Unit Editing

- Added `Manage Service` and `Advanced` tabs to the per-service detail page so operators can either edit a structured set of unit directives or work directly on the discovered service file.
- Added constrained service-unit file APIs that only edit discovered fragment paths under approved systemd roots, write atomically, and run `systemctl daemon-reload` after saving.
- Reworked the `Manage Service` tab into an accordion-driven editor with summary cards and a cleaner split layout so the large directive set is easier to scan and operate.

## Login And Deep-Link Hardening

- Stripped accidental `username` and `password` query params from the login URL before submission while still preserving safe dashboard hash navigation after auth and TOTP setup.
- Normalized request paths server-side so query strings no longer break route matching for pages like `/login` or `/dashboard`.
- Added a server-side redirect for `GET /login?...` so the browser is pushed back to a clean `/login` URL before rendering the login page.
- Added fallback routing so stale `service:` and `system-user:` deep links land on their parent list pages instead of leaving operators on a `not found` error right after login.

## Agent Debugging Guidance

- Added a dedicated Chinese debugging section to `AGENTS.md` that requires exact reproduction, failing-layer identification, root-cause-only fixes, regression coverage, and explicit verification for bug work.
- Included a reusable Chinese prompt template in `AGENTS.md` so future debugging requests push the agent toward proof-first diagnosis instead of speculative patches.

## Services JS Syntax Fix

- Fixed a `services.js` template-literal syntax error in the `Manage Service` accordion copy that was breaking page initialization in the browser.

## Manage Service Simplification

- Simplified the `Manage Service` tab by removing extra side cards and reducing helper copy, while keeping the directive groups available through a cleaner accordion layout.
- Flattened the section styling so the service editor reads as a lighter single workspace instead of nested cards and dense callouts.
- Split the `Manage Service` editor into `Unit`, `Service`, and `Install` sub-tabs, each with its own accordion groups, so operators only focus on one major section at a time.
- Removed the extra outer shell around each sub-tab editor so the accordions sit directly in the tab pane with less visual nesting.

## Services Discovery Batching

- Reworked host service discovery so the panel batches service metadata retrieval through shared `systemctl show` calls instead of shelling out once per discovered unit.
- Expanded the service test fixture to verify that discovery fetches multiple units in a single metadata call while preserving the existing detail and action behavior.
- Hardened the batcher so template or host-specific units that break a shared `systemctl show` call only force smaller fallback groups instead of causing later services to disappear from the list.

## Dashboard Loading Overlay

- Added a shared full-screen dashboard loading overlay with a dark translucent backdrop, animated spinner, and loading message while AJAX page fetches and page initializers are still running.
- Hooked the overlay into both initial dashboard boot and later `loadPage()` navigation so page transitions no longer flash plain `Loading...` text.
- Scoped the overlay to the main content region so the sidebar, header, and footer remain visible while page content is loading.
- Adjusted the overlay scope so it now covers the main content region and footer together, while still leaving the sidebar and header visible.
- Restored the footer’s bottom-pinned layout by making the new content-stage wrapper manage its own `content + footer` grid rows.

## Codex CLI Flag Compatibility

- Hardened Codex conversation startup and resume so cuddlePanel only passes `--skip-git-repo-check` when the configured host Codex CLI advertises support for that flag.
- Expanded the Codex chat test fixture to mimic CLIs that reject the flag, covering the compatibility path behind the dashboard Codex page.
- Updated the Codex management doc to document the runtime probe and the debugging clue for hosts running older or different Codex CLI builds.

## Codex TERM Normalization

- Hardened interactive Codex conversation startup so the PTY child forces a usable `TERM` when the server environment inherits `TERM=dumb`, preventing the CLI from stopping at a startup confirmation prompt.
- Expanded the Codex chat regression test to simulate a CLI that exits when launched under `TERM=dumb`, proving the dashboard conversation path now repairs that environment before exec.
- Updated the Codex management doc with the new debugging clue for immediate-exit conversations caused by limited terminal environments.
