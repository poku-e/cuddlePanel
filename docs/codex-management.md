# Codex Management

Purpose: define the dashboard workflow for running Codex as project-scoped conversations instead of one-off prompts. This page exists so administrators can keep Codex context inside named threads, choose an explicit project root, and still use maintenance conversations for broader server administration when no project is selected.

Routes and access:
- `GET /api/page/codex`: requires `codex:view`.
- `GET /api/codex/projects`: requires `codex:view`.
- `POST /api/codex/projects`: requires `codex:manage`.
- `GET /api/codex/conversations`: requires `codex:view`.
- `POST /api/codex/conversations`: requires `codex:manage`.
- `POST /api/codex/conversations/<id>/read`: requires `codex:view`.
- `POST /api/codex/conversations/<id>/send`: requires `codex:manage`.
- `POST /api/codex/conversations/<id>/close`: requires `codex:manage`.
- `POST /api/codex/run`: still exists as a legacy single-run API, but the dashboard now uses conversations.

Workflow:
- Operators add projects by providing an absolute project root and an optional display name.
- Starting a new conversation requires a title or falls back to the selected project name.
- If a project is selected, Codex starts in that project root.
- If no project is selected, the conversation runs in maintenance mode using the configured maintenance workdir.
- Each conversation launches an interactive Codex CLI session under a PTY and keeps that session alive so follow-up prompts, approvals, and context stay within the same thread.
- The page polls conversation output, appends new stream content, and sends new user messages back into the active Codex session.
- Closing a conversation terminates the live Codex process and marks the conversation closed in metadata.

Runtime behavior:
- Codex runs with:
  - `danger-full-access` sandbox mode
  - approval policy `on-request`
  - the configured model when one is set
  - `--skip-git-repo-check` automatically for maintenance mode or non-git roots
- Approval prompts are handled in-band: they appear in the streamed conversation output, and the operator answers them through the same message box.

Hidden dependencies and configuration:
- `CUDDLEPANEL_CODEX_BIN` controls the Codex CLI path.
- `CUDDLEPANEL_CODEX_WORKDIR` acts as the maintenance-mode working directory when no project is selected.
- `CUDDLEPANEL_CODEX_MODEL` is optional.
- Project metadata is stored in `data/codex_projects.db`.
- Conversation metadata is stored in `data/codex_conversations.db`.
- Live conversation process state is in memory only; after a server restart, old metadata remains visible but the previous live PTY session does not resume.

Safety and operational rules:
- The client cannot pick an arbitrary binary; only the server-configured Codex CLI is executed.
- Project roots must be absolute existing directories.
- Conversation messages are length-limited and null bytes are rejected.
- The server executes Codex directly with `execv`; it does not interpolate shell commands.

Gotchas and debugging:
- Because the page now runs interactive Codex sessions, output may contain terminal control sequences from the CLI.
- If the host Codex CLI is not logged in, the streamed output will usually show the authentication failure directly.
- Metadata persistence and live PTY persistence are separate: project and conversation records survive restarts, but open conversations do not automatically reconnect to an old Codex process after a restart.
- Codex conversation starts and other runner diagnostics are written to `data/server.log`.
