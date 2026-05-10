# Nginx Management

Purpose: define the first constrained nginx workflow for cuddlePanel. This page exists to manage allowlisted site config files under known nginx directories without granting arbitrary filesystem editing.

Routes and access:
- `GET /api/page/nginx`: requires `nginx:view`.
- `GET /api/nginx/sites`: requires `nginx:view`.
- `POST /api/nginx/sites`: requires `nginx:manage`.
- `POST /api/nginx/sites/<name>`: requires `nginx:manage`.
- `POST /api/nginx/sites/<name>/action`: requires `nginx:manage`.

Workflow:
- Administrators register a site by providing a panel name, a nginx filename, an optional description, and the config content.
- Site metadata is stored in `data/nginx.db`.
- Config content is written to the configured `sites-available` directory.
- The dashboard reads enabled state by checking for the matching filename in `sites-enabled`.
- The page lists sites in a compact table, edits config content in a Bootstrap modal, and keeps a shared output panel below the table for enable/disable/test/reload feedback.
- Editing a site updates the stored metadata and the file content in `sites-available`.
- Enable creates a symlink in `sites-enabled`; disable removes it.
- Test runs `nginx -t`; reload runs `systemctl reload nginx`.
- Unsaved editor drafts are now kept in the browser while operators run enable, disable, test, or reload actions, so inline config edits are not wiped by a routine runtime action.

Validation and safety rules:
- Site names must be 3-64 characters and may contain letters, numbers, `.`, `_`, and `-`.
- Filenames must end with `.conf`, may contain letters, numbers, `.`, `_`, and `-`, and may not contain `..`.
- Descriptions are optional, capped at 200 characters, and normalized to one line.
- Config content is capped at 256 KiB and may not contain NUL bytes.
- File writes are restricted to the configured `sites-available` directory.
- Enable and disable operate only on the allowlisted filename associated with a stored site entry.
- Runtime commands must use direct `exec`, not shell interpolation.

Hidden dependencies and configuration:
- Defaults to `/etc/nginx/sites-available` and `/etc/nginx/sites-enabled`.
- `CUDDLEPANEL_NGINX_AVAILABLE_DIR` overrides the available directory.
- `CUDDLEPANEL_NGINX_ENABLED_DIR` overrides the enabled directory.
- `CUDDLEPANEL_NGINX_BIN` overrides the nginx binary path for config testing.
- `CUDDLEPANEL_NGINX_RELOAD_SERVICE` overrides the service name used for reload.

Gotchas and debugging:
- A saved config file can still fail `nginx -t`; surface command output so operators can fix syntax errors quickly.
- Renaming a filename must preserve the enabled state when possible by moving the symlink target.
- Do not let this workflow browse arbitrary nginx directories; stay within the configured available/enabled roots.
