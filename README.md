

# cuddlePanel

cuddlePanel is an internal server administration panel built in C++ with a Bootstrap-based frontend and AJAX-driven dashboard workflows.

<p align="center">
  <img src="static/cuddle_logo.png" alt="cuddlePanel logo" height="240" width="240">
</p>

## What It Does

- Secure onboarding and login with Argon2id password hashing
- TOTP 2FA setup for superadmin onboarding and first login for new users
- Per-page `view` / `manage` permissions
- User administration
- Host service discovery and systemd control with dedicated per-service management pages
- System administration for host users through a dedicated per-account user management page with tabs for overview, profile, security, SSH keys, logfiles, privileges, audit history, and a constrained file browser with `chown`, `chmod`, rename, copy/paste, zip, and unzip actions
- Nginx site management
- Project-scoped Codex conversations with maintenance-mode fallback, streamed output, in-thread approvals, transcript export, and restart-time resume
- Native stack-aware deploy workflow for Node.js, Golang, Streamlit, and Python + Vite, with optional Cloudflare DNS updates
- PTY-backed browser terminal with fresh OTP verification
- Dashboard page state that survives refreshes, plus safer destructive-action prompts on admin workflows

## Stack

- Backend: C++17
- Frontend: Bootstrap CSS, modular JavaScript, AJAX page loading
- Theme: locally vendored Bootswatch Pulse layered on top of Bootstrap
- Templates: reusable shell partials plus page templates under `templates/pages/`
- Data storage: local flat files under `data/`

## Project Layout

- `src/` - backend application code
- `include/` - headers
- `templates/` - shell templates and partials
- `templates/pages/` - dashboard page bodies
- `static/` - CSS, JavaScript, and branding assets
- `docs/` - operator and implementation docs
- `tests/` - focused native test binaries

## Running

Host bootstrap for Ubuntu/Debian:

```bash
sudo ./scripts/install-deps.sh
```

Build:

```bash
make build
```

Test:

```bash
make test
```

Run:

```bash
./bin/server
```

The server auto-loads `.env` from the current working directory if present. Shell-exported variables still take precedence.
Startup and runtime diagnostics are written to stderr and appended to `data/server.log`.

## Configuration

The repo includes a tracked `.env` file documenting the runtime environment variables used by the app, including:

- `CUDDLEPANEL_PORT`
- `CUDDLEPANEL_SECURE_COOKIES`
- nginx paths and binary overrides
- native deploy engine tool paths and optional Cloudflare defaults
- native deploy roots that are trusted for app builds
- system administration command and file overrides
- terminal runtime policy
- Codex runner path, workspace, model, and timeout

## Notes

- High-risk actions are protected by server-side permission checks.
- Native deploy build steps run as the requested deploy user, and the deploy engine only accepts project roots under `CUDDLEPANEL_DEPLOY_ALLOWED_ROOTS`.
- The terminal uses PTY sessions rather than raw command passthrough.
- System file actions are constrained by allowlisted roots.
- `make build` uses the hardened release profile and will pack `bin/server` with UPX when `upx` is available.
- `scripts/install-deps.sh` is a host bootstrap helper for Ubuntu/Debian systems; it does not replace the browser-based first-run setup.
