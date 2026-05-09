# First-Run Setup

Purpose: define the first-run bootstrap workflow for cuddlePanel before normal login is available. This screen exists so operators can configure the runtime environment, review dependency status, and create the first superadmin in one pass.

Routes and access:
- `GET /onboarding`: public only while no users exist.
- `GET /api/setup/status`: public only while no users exist.
- `POST /api/onboarding`: public only while no users exist.

Workflow:
- If `data/users.db` is empty, `/` redirects to `/onboarding`.
- The onboarding page loads `/api/setup/status` to populate essential runtime environment values and display dependency checks.
- The operator edits the essential runtime values, then enters the first superadmin username and password.
- `POST /api/onboarding` validates the first-run config, writes `.env`, applies the values to the current process, creates the superadmin, and signs that user into the pending TOTP setup flow.
- After successful onboarding, the browser is redirected to `/2fa/setup`.

Environment scope in this phase:
- Network and cookie mode:
  - `CUDDLEPANEL_PORT`
  - `CUDDLEPANEL_SECURE_COOKIES`
- Deploy helper:
  - `CUDDLEPANEL_DEPLOY_SITE_BIN`
- Nginx:
  - `CUDDLEPANEL_NGINX_AVAILABLE_DIR`
  - `CUDDLEPANEL_NGINX_ENABLED_DIR`
  - `CUDDLEPANEL_NGINX_BIN`
  - `CUDDLEPANEL_NGINX_RELOAD_SERVICE`
- System action roots:
  - `CUDDLEPANEL_SYSTEM_ALLOWED_ROOTS`
- Terminal:
  - `CUDDLEPANEL_TERMINAL_SHELL`
  - `CUDDLEPANEL_TERMINAL_RUN_AS_USER`
  - `CUDDLEPANEL_TERMINAL_RUN_AS_GROUP`
  - `CUDDLEPANEL_TERMINAL_WORKDIR`
  - `CUDDLEPANEL_TERMINAL_MAX_SESSIONS_PER_USER`
  - `CUDDLEPANEL_TERMINAL_IDLE_TIMEOUT_SECONDS`
  - `CUDDLEPANEL_TERMINAL_MAX_SESSION_SECONDS`
- Codex:
  - `CUDDLEPANEL_CODEX_BIN`
  - `CUDDLEPANEL_CODEX_WORKDIR` (maintenance-mode workdir when no project is selected)
  - `CUDDLEPANEL_CODEX_MODEL`
  - `CUDDLEPANEL_CODEX_TIMEOUT_SECONDS`

Dependency status behavior:
- The first-run page reports whether key host binaries are present and executable.
- For Ubuntu/Debian hosts, `scripts/install-deps.sh` provides a companion bootstrap path for installing the common packages these checks expect.
- Required in this phase:
  - terminal shell path
- Checked as optional feature dependencies:
  - deploy helper
  - nginx binary
  - Codex CLI
  - `systemctl`
  - system administration commands such as `useradd`, `passwd`, `usermod`, `gpasswd`, `chown`, and `chmod`

Safety rules:
- Shell-exported environment variables still take precedence over `.env` values on future starts.
- The setup page writes a controlled `.env` file and does not accept arbitrary environment keys.
- First-run setup remains available only until the first user exists.

Gotchas and debugging:
- Dependency status is informative in this phase; it does not attempt distro-specific package installation.
- The repo ships a distro-specific helper at `scripts/install-deps.sh`, but the onboarding page itself still only reports dependency state and writes `.env`.
- If `.env` cannot be written, onboarding fails before the superadmin is created.
- Changes saved during onboarding apply immediately to the current server process for the completion response and future runtime behavior.
- Server startup and runtime failures are appended to `data/server.log`, which is the first place to look when the process exits immediately after launch.
