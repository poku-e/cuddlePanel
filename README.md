<p align="center">
  <img src="static/cuddle_logo.png" alt="cuddlePanel logo" width="120">
</p>

# cuddlePanel

cuddlePanel is an internal server administration panel built in C++ with a Bootstrap-based frontend and AJAX-driven dashboard workflows.

## What It Does

- Secure onboarding and login with Argon2id password hashing
- TOTP 2FA setup for superadmin onboarding and first login for new users
- Per-page `view` / `manage` permissions
- User administration
- Service registry and systemd control
- System administration for host users, sudo group membership, constrained `chown` / `chmod`, and `authorized_keys`
- Nginx site management
- Deploy helper form for `deploy-site`
- PTY-backed browser terminal with fresh OTP verification

## Stack

- Backend: C++17
- Frontend: Bootstrap CSS, modular JavaScript, AJAX page loading
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

## Configuration

The repo includes a tracked `.env` file documenting the runtime environment variables used by the app, including:

- `CUDDLEPANEL_PORT`
- `CUDDLEPANEL_SECURE_COOKIES`
- nginx paths and binary overrides
- deploy helper path
- system administration command and file overrides
- terminal runtime policy

## Notes

- High-risk actions are protected by server-side permission checks.
- The terminal uses PTY sessions rather than raw command passthrough.
- System file actions are constrained by allowlisted roots.
- `make build` uses the hardened release profile and will pack `bin/server` with UPX when `upx` is available.
