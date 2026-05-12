# Service Management

Purpose: define the first production-safe service management workflow for cuddlePanel. This page exists to let administrators manage an allowlisted set of systemd services without exposing arbitrary command execution.

Routes and access:
- `GET /api/page/services`: requires `services:view`.
- `GET /api/services`: requires `services:view`.
- `GET /api/services/<unit>`: requires `services:view`.
- `GET /api/services/<unit>/page`: requires `services:view`.
- `POST /api/services/<unit>/action`: requires `services:manage`.

Workflow:
- The Services page discovers host `.service` units directly from `systemctl list-unit-files --type=service`.
- The index page renders each discovered unit into a compact table with active state, enablement state, description, and quick runtime actions.
- The index also provides a live client-side search box that filters by service name, unit, description, and reported state without another round trip to the server.
- Clicking a service name or `Manage` opens a dedicated service detail page for that unit.
- The detail page is tabbed:
  - `Overview`: summary cards plus quick `start`, `stop`, `restart`, `enable`, and `disable` actions.
  - `Runtime`: most recent command output for that service.
  - `Unit`: read-only metadata such as fragment path, load state, active state, sub-state, and enablement.
- Runtime and enablement actions execute immediately through `systemctl` and then reload the service detail view.

Validation and safety rules:
- Unit names must end in `.service` and may contain letters, numbers, `.`, `_`, `-`, and `@`.
- Only discovered `.service` units are shown in the UI and accepted by the detail workflow.
- The backend must call `systemctl` directly through `exec`, never through shell interpolation.
- Action values are allowlisted to `start`, `stop`, `restart`, `enable`, and `disable`.

Hidden dependencies and configuration:
- Requires a Linux host with `systemctl` available at `/bin/systemctl` by default.
- `CUDDLEPANEL_SYSTEMCTL_BIN` can override the `systemctl` path for controlled deployments or tests.
- Service discovery uses:
  - `systemctl list-unit-files --type=service`
  - `systemctl show <unit> --property=Id,Description,LoadState,ActiveState,SubState,UnitFileState,FragmentPath`
- Action output is surfaced back to the dashboard as text for operator debugging.

Gotchas and debugging:
- A discovered service can still fail to start, stop, enable, or disable because the underlying unit is broken or locked by host policy; preserve stderr/stdout in the action response.
- This workflow manages discovered host services only. It does not edit unit file contents, drop-ins, or arbitrary paths from the service screen.
- If service discovery feels slow on hosts with many units, batch metadata retrieval through a more efficient `systemctl show` strategy instead of widening the command surface.
