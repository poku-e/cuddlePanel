# Terminal Management

Purpose: define the PTY-backed terminal workflow for cuddlePanel. This page exists to provide interactive terminal sessions for administrators without broadening the command surface into generic unauthenticated process execution.

Routes and access:
- `GET /api/page/terminal`: requires `terminal:view`.
- `POST /api/2fa/verify-terminal`: requires a configured TOTP secret and grants 30 minutes of terminal access.
- `POST /api/terminal/session`: requires `terminal:manage` and a fresh terminal OTP verification.
- `POST /api/terminal/session/<id>/read`: requires `terminal:manage` and a fresh terminal OTP verification.
- `POST /api/terminal/session/<id>/write`: requires `terminal:manage` and a fresh terminal OTP verification.
- `POST /api/terminal/session/<id>/resize`: requires `terminal:manage` and a fresh terminal OTP verification.
- `POST /api/terminal/session/<id>/close`: requires `terminal:manage` and a fresh terminal OTP verification.

Workflow:
- The Terminal page first requires a valid TOTP code if the last terminal verification is older than 30 minutes.
- After verification, the page creates a PTY session tied to the authenticated username.
- The browser polls for output deltas, writes keystrokes to the PTY through a binary-safe base64 transport, and sends resize events when the terminal viewport changes.
- Closing the page or pressing the close control requests PTY shutdown through the API.
- The secondary terminal button now starts a new session explicitly; if an active shell exists, the UI asks for confirmation before replacing it.

Security and runtime rules:
- Terminal sessions are backed by PTYs created through `forkpty`, not by shelling out through ad hoc commands.
- The default shell is `/bin/bash`, overridden by `CUDDLEPANEL_TERMINAL_SHELL` if configured.
- The default runtime account is `nobody:nogroup`, overridden by `CUDDLEPANEL_TERMINAL_RUN_AS_USER` and `CUDDLEPANEL_TERMINAL_RUN_AS_GROUP`.
- The default working directory is `/tmp`, overridden by `CUDDLEPANEL_TERMINAL_WORKDIR`.
- Each session tracks ownership by authenticated username; users cannot read or write other users' PTYs.
- Terminal APIs require `terminal:manage`, not just `terminal:view`.
- Sessions are limited per user through `CUDDLEPANEL_TERMINAL_MAX_SESSIONS_PER_USER` and expire on idle/runtime limits.
- Output is buffered server-side with a bounded in-memory window. If the browser falls behind, the server reports truncation instead of growing without limit.
- Terminal output is JSON-escaped on the server for all control bytes below `0x20`, so ANSI escape sequences and other PTY control characters do not break the AJAX response format.
- Terminal input is base64-encoded in the browser before submission so `Esc`, control-key sequences, and other non-printable PTY bytes survive the HTTP form layer intact.
- Sessions that exit are marked closed and can still be read for buffered output until the client closes them.
- Child processes start with a minimal environment and `no_new_privs` enabled before the shell exec.

Hidden dependencies and configuration:
- Requires `forkpty` support from the host libc/util stack.
- `TERM` is set to `xterm-256color` for spawned shells.
- The browser UI uses xterm.js for terminal rendering and resize behavior.
- Default idle timeout is 900 seconds.
- Default max session lifetime is 7200 seconds.

Gotchas and debugging:
- A PTY session may exit immediately if the configured shell path is missing or not executable.
- A PTY session may also fail immediately if the configured runtime user/group cannot be resolved or if the working directory does not exist.
- Output polling is not a websocket; brief latency is expected, so keep polling intervals modest.
- The terminal is safer than before because it drops privilege by default, but it is still an administrative surface and should remain tightly permissioned and OTP-gated.
