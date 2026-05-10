# System Management

Purpose: define the constrained system-administration surface for cuddlePanel. This page exists to cover common host-operator tasks without exposing arbitrary root file edits or shell interpolation through the dashboard.

Routes and access:
- `GET /api/system/users`: requires `system:view`.
- `POST /api/system/users`: requires `system:manage`.
- `POST /api/system/users/<username>/action`: requires `system:manage`.
- `GET /api/system/users/<username>/authorized-keys`: requires `system:manage`.
- `POST /api/system/users/<username>/authorized-keys`: requires `system:manage`.
- `POST /api/system/path-action`: requires `system:manage`.

Workflow:
- The System page loads host account data through `/api/system/users`.
- The page uses Bootstrap nav tabs for `Accounts` and `Files` so account operations and file-management tools stay visually separate.
- Account listing reads the configured passwd, group, and shadow sources and returns username, uid, gid, home, shell, system-account classification, sudo-group membership, and lock state in a compact table.
- Creating an account opens in a Bootstrap modal, uses `useradd` directly, and keeps the account list visible in the background. Normal users are created with a home directory. System accounts are created with `-r -M`.
- Account actions support:
  - `lock`
  - `unlock`
  - `grant-sudo`
  - `revoke-sudo`
- SSH key management supports reading, creating, and replacing `~/.ssh/authorized_keys` for login users only, with the editor opened in a Bootstrap modal from an account row or the Files tab summary card.
- Sudo management in this phase is group-based only. cuddlePanel manages membership in the `sudo` group and does not edit `/etc/sudoers` or drop-in sudoers files.
- Ownership and mode changes run through a dedicated Bootstrap modal launched from the Files tab:
  - `chown` requires an allowed path plus owner and optional group.
  - `chmod` requires an allowed path plus a valid octal mode.
  - Both actions optionally support recursion.

Hidden dependencies and configuration:
- Reads account data from:
  - `CUDDLEPANEL_PASSWD_FILE` or `/etc/passwd`
  - `CUDDLEPANEL_GROUP_FILE` or `/etc/group`
  - `CUDDLEPANEL_SHADOW_FILE` or `/etc/shadow`
- Runtime command paths can be overridden for testing or controlled deployments:
  - `CUDDLEPANEL_USERADD_BIN`
  - `CUDDLEPANEL_PASSWD_BIN`
  - `CUDDLEPANEL_USERMOD_BIN`
  - `CUDDLEPANEL_GPASSWD_BIN`
  - `CUDDLEPANEL_CHOWN_BIN`
  - `CUDDLEPANEL_CHMOD_BIN`
- File ownership and mode changes are limited to roots from `CUDDLEPANEL_SYSTEM_ALLOWED_ROOTS`.
- If `CUDDLEPANEL_SYSTEM_ALLOWED_ROOTS` is unset, cuddlePanel defaults to:
  - `/home`
  - `/srv`
  - `/var/www`
- Paths must already exist, resolve under an allowed root after canonicalization, and avoid traversal outside that root.
- `authorized_keys` content is written directly under the selected user's home directory after that home path is resolved on the server.

Safety rules:
- The `root` account cannot be locked and cannot have sudo access revoked through the panel.
- Unknown actions are rejected server-side.
- Account names, shell paths, home paths, owner names, group names, and octal modes are all validated before execution.
- `authorized_keys` editing is limited to login users with interactive shells and non-system UIDs. cuddlePanel does not expose arbitrary file editing under `.ssh/`.
- Commands are executed with direct `execv` argument vectors, not through a shell.
- New `.ssh` directories are created with `0700` and `authorized_keys` files with `0600`, then assigned to the target account's uid and gid.

Gotchas and debugging:
- Some hosts use an admin group other than `sudo`; this phase intentionally targets `sudo` only.
- If shadow data is unreadable, lock-state reporting falls back to unlocked in the UI rather than failing the whole page load.
- Recursive `chown` and `chmod` are powerful even with path allowlists. Keep `system:manage` narrowly assigned.
- `authorized_keys` is treated as structured operator data, not as a general-purpose text editor surface. Keep the server-side username-to-home resolution authoritative.
- The page keeps account actions in cards and moves larger write operations into modals on purpose. If the layout starts drifting back toward stacked forms, preserve the tab-and-modal organization instead of reintroducing every tool inline.
