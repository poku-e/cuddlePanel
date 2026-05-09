# Deploy Site Workflow

Purpose: define the first deploy workflow for cuddlePanel by mirroring the host deploy helper through an explicit form and a constrained backend runner. This page exists to execute a known deploy script with validated arguments instead of exposing shell access.

Script source:
- The host currently provides the deploy helper at `/usr/local/sbin/deploy-site`.
- `CUDDLEPANEL_DEPLOY_SITE_BIN` can override the script path.
- The original product note referenced `/sbin/deploy-site`; cuddlePanel now defaults to the discovered host path and treats the binary path as configuration.

Routes and access:
- `GET /api/page/deploy`: requires `deploy:view`.
- `POST /api/deploy/run`: requires `deploy:manage`.

Form fields mirrored from the script:
- `domain`
- `port`
- `user`
- `project-root`
- `service-desc`
- `email`
- `site-name`
- `upstream-host`
- `skip-dns-check`
- `skip-certbot`
- `force-overwrite-site`
- `skip-service-start`

Workflow:
- The dashboard collects explicit values for the deploy helper.
- The backend validates and normalizes all fields before execution.
- The backend always runs the script in non-interactive mode so failed validation or missing values return immediately.
- Output from the helper is captured and returned to the dashboard as plain text.

Validation and safety rules:
- `domain` must contain only letters, numbers, `.`, and `-`.
- `port` must be numeric and between 1 and 65535.
- `user` and `site-name` must be 1-64 characters and may contain only letters, numbers, `.`, `_`, and `-`.
- `project-root` must be an absolute path without NUL bytes.
- `service-desc` is required and capped at 200 characters.
- `email` is required unless `skip-certbot` is enabled.
- `upstream-host` is required and capped at 255 characters.
- The backend must call the configured deploy helper directly through `exec`, never through shell interpolation.
- The backend must always pass `--non-interactive`.

Hidden dependencies and configuration:
- The deploy helper itself installs and manages packages, nginx, certbot, linux users, SSH keys, and systemd services.
- The helper requires root privileges and host tools such as `apt-get`, `systemctl`, `certbot`, and `nginx`.
- The dashboard runner does not attempt to simulate those dependencies; it only validates input and executes the configured helper.

Gotchas and debugging:
- Because the script is non-interactive in the dashboard, any missing field becomes an immediate error instead of a prompt.
- `skip-certbot` changes whether `email` is required; keep that validation in sync with the helper.
- Captured output is operationally sensitive. Do not include secrets in new script arguments or in UI logging.
