# Service Management

Purpose: define the production-safe service management workflow for cuddlePanel. This page exists to let administrators discover host systemd services, control them, and edit their unit files without exposing arbitrary command execution or unconstrained filesystem access.

Routes and access:
- `GET /api/page/services`: requires `services:view`.
- `GET /api/services`: requires `services:view`.
- `GET /api/services/<unit>`: requires `services:view`.
- `GET /api/services/<unit>/page`: requires `services:view`.
- `GET /api/services/<unit>/unit-file`: requires `services:view`.
- `POST /api/services/<unit>/unit-file`: requires `services:manage`.
- `POST /api/services/<unit>/action`: requires `services:manage`.

Workflow:
- The Services page discovers host `.service` units directly from `systemctl list-unit-files --type=service`.
- The index page renders each discovered unit into a compact table with active state, enablement state, description, and quick runtime actions.
- The index also provides a live client-side search box that filters by service name, unit, description, and reported state without another round trip to the server.
- Clicking a service name or `Manage` opens a dedicated service detail page for that unit.
- The detail page is tabbed:
  - `Overview`: summary cards plus quick `start`, `stop`, `restart`, `enable`, and `disable` actions.
  - `Manage Service`: a structured editor for common `Unit`, `Service`, and `Install` directives, grouped into accordion sections so operators can open one directive family at a time. Saving this tab rewrites the unit with a normalized layout.
  - `Advanced`: direct editing of the discovered service unit file content with a code-style textarea.
  - `Runtime`: most recent command output for that service.
  - `Unit`: read-only metadata such as fragment path, load state, active state, sub-state, and enablement.
- Saving either editing tab writes only the discovered fragment file for that unit and then runs `systemctl daemon-reload`.
- Runtime and enablement actions execute immediately through `systemctl` and then reload the service detail view.

Validation and safety rules:
- Unit names must end in `.service` and may contain letters, numbers, `.`, `_`, `-`, and `@`.
- Only discovered `.service` units are shown in the UI and accepted by the detail workflow.
- Unit file editing is allowed only when the discovered `FragmentPath` resolves inside the configured service-unit roots and the file already exists.
- Unit file content is capped at 256 KB, rejects embedded null bytes, and is written atomically before `daemon-reload`.
- The backend must call `systemctl` directly through `exec`, never through shell interpolation.
- Action values are allowlisted to `start`, `stop`, `restart`, `enable`, and `disable`.

Hidden dependencies and configuration:
- Requires a Linux host with `systemctl` available at `/bin/systemctl` by default.
- `CUDDLEPANEL_SYSTEMCTL_BIN` can override the `systemctl` path for controlled deployments or tests.
- `CUDDLEPANEL_SERVICE_UNIT_ROOTS` can override the editable systemd unit roots as a colon-delimited list. The default roots are `/etc/systemd/system`, `/usr/lib/systemd/system`, and `/lib/systemd/system`.
- Service discovery uses:
  - `systemctl list-unit-files --type=service`
  - `systemctl show <unit> --property=Id,Description,LoadState,ActiveState,SubState,UnitFileState,FragmentPath`
- Unit-file saves also call `systemctl daemon-reload`, and the combined output is surfaced back to the dashboard as text for operator debugging.

Gotchas and debugging:
- A discovered service can still fail to start, stop, enable, or disable because the underlying unit is broken or locked by host policy; preserve stderr/stdout in the action response.
- Structured `Manage Service` saves intentionally normalize ordering and drop comments. Use `Advanced` when you need exact comments, spacing, or directives not covered by the structured form.
- The direct editor only writes the discovered fragment file. It does not manage drop-in directories or create new unit files in arbitrary paths.
- A bad unit file can still be saved and then fail at `daemon-reload`; keep the runtime output visible so operators can recover quickly.
- If service discovery feels slow on hosts with many units, batch metadata retrieval through a more efficient `systemctl show` strategy instead of widening the command surface.
