# Nginx Management

Purpose: define the constrained nginx workflow for cuddlePanel. This page reads site configs directly from allowlisted nginx directories and manages them without granting arbitrary filesystem editing.

Routes and access:
- `GET /api/page/nginx`: requires `nginx:view`.
- `GET /api/nginx/sites`: requires `nginx:view`.
- `POST /api/nginx/sites`: requires `nginx:manage`.
- `POST /api/nginx/sites/<id>`: requires `nginx:manage`.
- `POST /api/nginx/sites/<id>/action`: requires `nginx:manage`.

Workflow:
- The dashboard discovers site files by scanning the configured `sites-available` directory.
- Each discovered site file with a safe basename is listed even when no metadata exists in `data/nginx.db`.
- Optional metadata (`name`, `description`) from `data/nginx.db` is used only as display overrides.
- The dashboard reads enabled state by checking for the matching filename in `sites-enabled`.
- The page lists sites in a compact table, edits config content in a Bootstrap modal, and keeps a shared output panel below the table for enable/disable/test/reload feedback.
- Editing a site updates file content in `sites-available`; metadata is written if provided.
- Enable creates a symlink in `sites-enabled`; disable removes it.
- Test runs `nginx -t`; reload runs `systemctl reload nginx`.
- Unsaved editor drafts are now kept in the browser while operators run enable, disable, test, or reload actions, so inline config edits are not wiped by a routine runtime action.

Validation and safety rules:
- Display names are optional; when provided they must be 3-64 characters and may contain letters, numbers, `.`, `_`, and `-`.
- Filenames may contain letters, numbers, `.`, `_`, and `-`, may not contain `..`, and are constrained to the configured site directories.
- Descriptions are optional, capped at 200 characters, and normalized to one line.
- Config content is capped at 256 KiB and may not contain NUL bytes.
- File writes are restricted to the configured `sites-available` directory.
- Enable and disable resolve targets from discovered filenames (and optional metadata aliases).
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
- Legacy `data/nginx.db` rows that only contain `name<TAB>filename` are still loaded; missing descriptions are normalized to empty strings.
- If a metadata row points to a missing file, it is ignored until the file exists in `sites-available`.
- Do not let this workflow browse arbitrary nginx directories; stay within the configured available/enabled roots.
