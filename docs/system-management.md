# System Management

Purpose: define the constrained system-administration surface for cuddlePanel. This page exists to cover common host-operator tasks without exposing arbitrary root file edits or shell interpolation through the dashboard.

Routes and access:
- `GET /api/system/users`: requires `system:view`.
- `GET /api/system/users/<username>`: requires `system:view`.
- `GET /api/system/users/<username>/page`: requires `system:view`.
- `GET /api/system/users/<username>/audit`: requires `system:view`.
- `GET /api/system/users/<username>/logfiles`: requires `system:view`.
- `POST /api/system/files/browse`: requires `system:view`.
- `POST /api/system/users`: requires `system:manage`.
- `POST /api/system/users/<username>/edit`: requires `system:manage`.
- `POST /api/system/users/<username>/security`: requires `system:manage`.
- `POST /api/system/users/<username>/action`: requires `system:manage`.
- `GET /api/system/users/<username>/authorized-keys`: requires `system:manage`.
- `POST /api/system/users/<username>/authorized-keys`: requires `system:manage`.
- `POST /api/system/path-action`: requires `system:manage`.
- `POST /api/system/files/action`: requires `system:manage`.

Workflow:
- The System page loads host account data through `/api/system/users`.
- The System page is now the account index. It keeps account creation plus a global allowlisted path-action tool, and sends each account into a dedicated per-user management page.
- Clicking a username or `Manage` opens `/api/system/users/<username>/page`, which renders a tabbed `User management` surface for that account.
- Account listing reads the configured passwd, group, and shadow sources and returns username, uid, gid, home, shell, system-account classification, sudo-group membership, and lock state in a compact table.
- The `Accounts` tab intentionally renders only login-enabled users. Entries with `nologin` or `false` shells stay out of the main dashboard list, and any remaining system accounts with interactive shells are sorted after normal login users.
- Creating an account opens in a Bootstrap modal, uses `useradd` directly, and keeps the account list visible in the background. Normal users are created with a home directory. System accounts are created with `-r -M`.
- The per-user page groups controls into tabs:
  - `Overview`: summary state plus quick actions for lock/unlock, sudo, and account deletion.
  - `Profile`: shell, home path, optional home move behavior, GECOS comment, primary group, and supplementary groups through `usermod`.
  - `Security`: password reset, force-password-change on next login, and account expiration using `chpasswd` and `chage`.
  - `SSH`: `authorized_keys` read/write for login-enabled accounts only.
  - `Logfiles`: read-only shell history surfaced from supported history files inside the account home directory.
  - `Privileges`: a richer privilege posture panel for sudo state, account classification, primary vs. supplementary groups, and direct group-based sudo actions.
  - `Files`: a per-user constrained file browser rooted in allowed paths, with server-validated `chown`, `chmod`, rename, copy/paste, zip, and unzip actions.
  - `Audit`: panel-originated account history for that user.
- Account actions support `lock`, `unlock`, `grant-sudo`, `revoke-sudo`, and `delete`.
- Account deletion still uses a confirmation modal. Operators can optionally request recursive home-directory removal, which maps to `userdel -r` on the server.
- Account creation and account-mutation commands are serialized inside the panel process so overlapping dashboard requests do not race each other for `/etc/passwd` and `/etc/group` locks.
- SSH key management supports reading, creating, and replacing `~/.ssh/authorized_keys` for login users only, from the dedicated `SSH` tab.
- The `Logfiles` tab reads only a fixed allowlist of history filenames under the resolved home directory: `.bash_history`, `.zsh_history`, `.sh_history`, and `.ash_history`.
- Logfile reads are capped to the newest 128 KB per file and trimmed to a newline boundary when truncated, so large history files do not flood the dashboard.
- Sudo management in this phase is group-based only. cuddlePanel manages membership in the `sudo` group and does not edit `/etc/sudoers` or drop-in sudoers files.
- Ownership and mode changes still support the global Files tool on the index page, and they also run from the per-user `Files` tab:
  - `chown` requires an allowed path plus owner and optional group.
  - `chmod` requires an allowed path plus a valid octal mode.
  - Both actions optionally support recursion.
- The per-user `Files` tab now also provides:
  - Directory browsing for existing paths inside allowed roots.
  - Rename within the same parent directory.
  - Copy/paste into another allowed directory, with destination conflicts rejected.
  - `zip` creation beside the selected item and `unzip` extraction into the currently opened allowed directory.
- Successful panel-originated account mutations append a simple per-user audit line to `data/system_account_audit.log` by default, and the `Audit` tab reads from that file.

Hidden dependencies and configuration:
- Reads account data from:
  - `CUDDLEPANEL_PASSWD_FILE` or `/etc/passwd`
  - `CUDDLEPANEL_GROUP_FILE` or `/etc/group`
  - `CUDDLEPANEL_SHADOW_FILE` or `/etc/shadow`
- Runtime command paths can be overridden for testing or controlled deployments:
  - `CUDDLEPANEL_USERADD_BIN`
  - `CUDDLEPANEL_PASSWD_BIN`
  - `CUDDLEPANEL_USERMOD_BIN`
  - `CUDDLEPANEL_CHPASSWD_BIN`
  - `CUDDLEPANEL_CHAGE_BIN`
  - `CUDDLEPANEL_GPASSWD_BIN`
  - `CUDDLEPANEL_USERDEL_BIN`
  - `CUDDLEPANEL_CHOWN_BIN`
  - `CUDDLEPANEL_CHMOD_BIN`
  - `CUDDLEPANEL_ZIP_BIN`
  - `CUDDLEPANEL_UNZIP_BIN`
- The per-user audit log path can be overridden with:
  - `CUDDLEPANEL_SYSTEM_AUDIT_LOG`
- File ownership and mode changes are limited to roots from `CUDDLEPANEL_SYSTEM_ALLOWED_ROOTS`.
- If `CUDDLEPANEL_SYSTEM_ALLOWED_ROOTS` is unset, cuddlePanel defaults to:
  - `/home`
  - `/srv`
  - `/var/www`
- Paths must already exist, resolve under an allowed root after canonicalization, and avoid traversal outside that root.
- New names created through rename stay in the same parent directory and are validated as single path components, not arbitrary relative paths.
- `authorized_keys` content is written directly under the selected user's home directory after that home path is resolved on the server.

Safety rules:
- The `root` account cannot be locked, deleted, or have sudo access revoked through the panel.
- The `root` account is also excluded from the phase-1 edit flow so shell, home, and group changes for the most sensitive account still require an out-of-band host workflow.
- The `root` account is excluded from the phase-2 security flow as well, so password and expiration changes for the most sensitive account still require a direct host workflow.
- The per-user page still allows viewing `root`, but the profile, security, sudo, and delete controls are intentionally disabled to match the server-side restrictions.
- Unknown actions are rejected server-side.
- Account names, shell paths, home paths, GECOS comments, owner names, group names, passwords, ISO expiration dates, and octal modes are all validated before execution.
- `authorized_keys` editing is limited to login users with interactive shells and non-system UIDs. cuddlePanel does not expose arbitrary file editing under `.ssh/`.
- Logfile viewing follows the same account-class restriction as SSH key management: only login users with interactive shells and non-system UIDs qualify.
- File-browser mutations reject symlink targets for most actions, and recursive copy/zip flows reject directories that contain symlinks so the panel does not smuggle references outside allowed roots.
- Commands are executed with direct `execv` argument vectors, not through a shell.
- Recursive home deletion is only available through the explicit delete-account confirmation modal; it is never implied by a plain account delete request.
- New `.ssh` directories are created with `0700` and `authorized_keys` files with `0600`, then assigned to the target account's uid and gid.

Gotchas and debugging:
- Some hosts use an admin group other than `sudo`; this phase intentionally targets `sudo` only.
- If shadow data is unreadable, lock-state reporting falls back to unlocked in the UI rather than failing the whole page load.
- If the System index shows a client-side `invalid response` error, inspect `GET /api/system/users` first. The dashboard expects a valid JSON document containing both `users` and `allowedRoots`, and malformed account fields will block the page before any rows render.
- Recursive `chown` and `chmod` are powerful even with path allowlists. Keep `system:manage` narrowly assigned.
- `authorized_keys` is treated as structured operator data, not as a general-purpose text editor surface. Keep the server-side username-to-home resolution authoritative.
- If an expected history file does not appear in `Logfiles`, verify that it is a regular file under the resolved home directory. Symlinks that escape the home path are ignored intentionally.
- If zip or unzip actions fail unexpectedly, verify that the configured `zip` and `unzip` binaries exist on the host and that the selected archive path is still inside the configured allowed roots.
- The dedicated user page is intentionally tabbed so account management can grow without turning the System index into a wall of modals. Keep new user-scoped features on the per-user page unless they are truly global host tools.
- The API still returns all parsed passwd entries because backend actions and tests rely on the full dataset; the dashboard applies the login-user filter at render time to keep the operator view focused.
- Audit history is panel-local. Direct host-side `useradd`, `usermod`, `passwd`, `userdel`, or manual file edits do not appear in the `Audit` tab unless they flowed through cuddlePanel.
