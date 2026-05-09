# Service Management

Purpose: define the first production-safe service management workflow for cuddlePanel. This page exists to let administrators manage an allowlisted set of systemd services without exposing arbitrary command execution.

Routes and access:
- `GET /api/page/services`: requires `services:view`.
- `GET /api/services`: requires `services:view`.
- `POST /api/services`: requires `services:manage`.
- `POST /api/services/<name>`: requires `services:manage`.
- `POST /api/services/<name>/action`: requires `services:manage`.

Workflow:
- Administrators create a service entry in cuddlePanel by providing a panel name, a systemd unit name, and an optional description.
- Service entries are stored in `data/services.db`.
- The Services page loads entries and fetches each unit's current runtime state from `systemctl`.
- Editing a service updates only the stored allowlisted metadata; it does not touch unit files on disk.
- Runtime actions are limited to `start`, `stop`, and `restart`.

Validation and safety rules:
- Service names must be 3-64 characters and may contain letters, numbers, `.`, `_`, and `-`.
- Unit names must end in `.service` and may contain letters, numbers, `.`, `_`, `-`, and `@`.
- Descriptions are optional, capped at 200 characters, and normalized to a single line.
- Only stored allowlisted services can be started, stopped, or restarted.
- The backend must call `/bin/systemctl` directly through `exec`, never through shell interpolation.
- Action values are allowlisted to `start`, `stop`, and `restart`.

Hidden dependencies and configuration:
- Requires a Linux host with `systemctl` available at `/bin/systemctl`.
- Runtime status fetches use `systemctl is-active <unit>`.
- Action output is surfaced back to the dashboard as text for operator debugging.

Gotchas and debugging:
- A service can be valid in the panel and still fail to start because the underlying unit is missing or broken; preserve stderr/stdout in the action response.
- Do not let panel users edit arbitrary unit file paths from this workflow; this phase manages allowlisted units only.
- If service status reads begin to feel slow, batch status retrieval through `systemctl show` in a later phase rather than widening the allowed command surface.
