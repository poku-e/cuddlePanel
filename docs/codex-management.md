# Codex Management

Purpose: define the dashboard workflow for running Codex as project-scoped conversations instead of one-off prompts. This page exists so administrators can keep Codex context inside named threads, choose an explicit project root, and still use maintenance conversations for broader server administration when no project is selected.

Routes and access:
- `GET /api/page/codex`: requires `codex:view`.
- `GET /api/codex/projects`: requires `codex:view`.
- `POST /api/codex/projects`: requires `codex:manage`.
- `GET /api/codex/conversations`: requires `codex:view`.
- `POST /api/codex/conversations`: requires `codex:manage`.
- `POST /api/codex/conversations/<id>/read`: requires `codex:view`.
- `GET /api/codex/conversations/<id>/transcript`: requires `codex:view`.
- `GET /api/codex/conversations/<id>/history`: requires `codex:view`.
- `POST /api/codex/conversations/<id>/send`: requires `codex:manage`.
- `POST /api/codex/conversations/<id>/close`: requires `codex:manage`.
- `POST /api/codex/run`: still exists as a legacy single-run API, but the dashboard now uses conversations.

Workflow:
- Operators add projects by providing an absolute project root and an optional display name.
- Starting a new conversation requires a title or falls back to the selected project name.
- If a project is selected, Codex starts in that project root.
- If no project is selected, the conversation runs in maintenance mode using the configured maintenance workdir.
- Each conversation launches an interactive Codex CLI session under a PTY and keeps that session alive so follow-up prompts, approvals, and context stay within the same thread.
- The same conversation record now stores the upstream Codex session id, so after a cuddlePanel restart the next read or send will attempt `codex resume <session-id>` and continue the same thread.
- The page polls conversation output, appends new stream content, and sends new user messages back into the active Codex session.
- Operators can export the accumulated transcript and inspect a simple audit history from the same page.
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
- Raw conversation transcripts are appended to `data/codex_transcripts/<conversation-id>.log`.
- Audit events are appended to `data/codex_audit/<conversation-id>.log`.
- Live PTY process state is still in memory only, but the stored Codex session id lets the panel reattach to the same Codex thread after a restart when the host CLI supports `codex resume`.

Safety and operational rules:
- The client cannot pick an arbitrary binary; only the server-configured Codex CLI is executed.
- Project roots must be absolute existing directories.
- Conversation messages are length-limited and null bytes are rejected.
- The server executes Codex directly with `execv`; it does not interpolate shell commands.

Gotchas and debugging:
- Because the page now runs interactive Codex sessions, output may contain terminal control sequences from the CLI.
- If the host Codex CLI is not logged in, the streamed output will usually show the authentication failure directly.
- Session recovery depends on the host Codex CLI session index. If cuddlePanel cannot detect a session id when the conversation starts, the metadata still persists, but restart-time resume may not be available for that specific thread.
- Codex conversation starts and other runner diagnostics are written to `data/server.log`.
