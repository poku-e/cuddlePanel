# Deploy Workflow

Purpose: define the native cuddlePanel deploy workflow that writes stack-specific systemd and nginx configuration directly from the dashboard. This page exists to deploy supported app stacks without relying on an external shell script.

Routes and access:
- `GET /api/page/deploy`: requires `deploy:view`.
- `POST /api/deploy/run`: requires `deploy:manage`.

Supported stacks:
- `nodejs`
- `golang`
- `streamlit`
- `python_vite`

Workflow:
- The dashboard now splits deploy inputs into `App`, `Build`, and `DNS` tabs so operators do not have to scan the full workflow in one long form.
- The `App` tab collects common deployment fields such as domain, project root, user, service description, and upstream port.
- The `Build` tab groups install/build/certbot/restart toggles.
- The `DNS` tab groups optional Cloudflare A-record settings and hides those fields until DNS automation is enabled.
- The selected stack reveals only the extra fields that matter for that stack:
  - Node.js: entry file
  - Golang: package and binary output path
  - Streamlit: entry file
  - Python + Vite: Python module, backend dir, frontend root, and Vite dist dir
- The backend validates all fields, optionally runs dependency install and build steps, writes a systemd unit, writes and enables an nginx site, reloads nginx, optionally runs certbot, and optionally creates or updates a Cloudflare DNS A record.
- Build and dependency-install steps now run under the requested deploy account instead of the panel's root context, while systemd/nginx/certbot/DNS actions remain in the privileged phase.
- When a deploy starts or finishes, the page scrolls operators directly to the output pane so mobile users do not lose the run result below the fold.

Native deploy behavior:
- Node.js:
  - optional `npm ci`
  - optional `npm run build`
  - systemd starts `node <entry>`
- Golang:
  - optional `go build -o <binary> <package>`
  - systemd starts the built binary
- Streamlit:
  - creates `.venv`
  - optional `pip install -r requirements.txt`
  - systemd starts `streamlit run <entry>`
- Python + Vite:
  - creates `.venv`
  - optional `pip install -r requirements.txt`
  - optional `npm ci` and `npm run build` inside the Vite root
  - systemd starts `gunicorn`
  - nginx serves the Vite dist directory and proxies `/api/` to the Python backend

Cloudflare DNS support:
- When enabled, the deployer looks up an existing A record for the full requested domain in the target Cloudflare zone.
- If the record exists, the deployer patches it with the requested IP and proxy setting.
- If the record does not exist, the deployer creates it.
- The API uses the official Cloudflare DNS record endpoints:
  - `GET /zones/{zone_id}/dns_records`
  - `POST /zones/{zone_id}/dns_records`
  - `PATCH /zones/{zone_id}/dns_records/{dns_record_id}`
- The server can use per-run zone id and token values, or fall back to `CUDDLEPANEL_CLOUDFLARE_ZONE_ID` and `CUDDLEPANEL_CLOUDFLARE_API_TOKEN`.

Validation and safety rules:
- `domain` must contain only letters, numbers, `.`, `_`, and `-`-safe hostname characters used by this implementation.
- `port` must be numeric and between 1 and 65535.
- `user` and `site_name` must be 1-64 characters and may contain only letters, numbers, `.`, `_`, and `-`.
- `project_root` must be an existing absolute directory.
- `project_root` must resolve under `CUDDLEPANEL_DEPLOY_ALLOWED_ROOTS`, may not be world-writable, and must be owned by `root` or the requested deploy user.
- Any stack-specific build working directory, such as `vite_root`, must also resolve inside that trusted project root after canonicalization.
- Stack-specific path fields must be relative paths without `..`.
- `python_module` must be a bounded token such as `app:app`.
- Cloudflare DNS creation requires a valid IPv4 public address.
- The backend executes host tools directly with `execv`; it does not shell-interpolate user input.

Hidden dependencies and configuration:
- Uses:
  - `CUDDLEPANEL_DEPLOY_SYSTEMD_DIR`
  - `CUDDLEPANEL_DEPLOY_ALLOWED_ROOTS`
  - `CUDDLEPANEL_SYSTEMCTL_BIN`
  - `CUDDLEPANEL_CERTBOT_BIN`
  - `CUDDLEPANEL_PYTHON3_BIN`
  - `CUDDLEPANEL_NPM_BIN`
  - `CUDDLEPANEL_NODE_BIN`
  - `CUDDLEPANEL_GO_BIN`
  - `CUDDLEPANEL_CURL_BIN`
  - nginx path settings already used elsewhere in the app
- The deployer assumes it has sufficient privilege to write nginx and systemd files and to reload or restart services.
- The deployer also assumes the panel process can drop to the requested deploy user before running app-controlled build steps.
- Cloudflare API token handling uses a temporary curl config file so the bearer token is not exposed in process arguments.

Gotchas and debugging:
- The current deployer is deliberately opinionated. It supports four stacks with bounded native workflows instead of arbitrary shell steps.
- `deploy:manage` still authorizes powerful app-controlled build logic, but those build hooks execute as the deploy user. Keep deploy roots narrow and assign that permission carefully.
- Some real projects need extra env files or pre/post hooks; those are not modeled yet.
- Python + Vite assumes `/api/` should proxy to the backend and the frontend should be served as a static SPA from the Vite dist directory.
- Deploy output is operationally sensitive. Do not add code that echoes Cloudflare API tokens or other secrets into the returned output.
