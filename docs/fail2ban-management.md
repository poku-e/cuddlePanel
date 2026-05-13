# Fail2ban Management

Purpose: define the constrained fail2ban workflow for cuddlePanel. This page exists to let operators inspect jails, manage banned IPs, and maintain ignore lists without exposing arbitrary shell execution.

Routes and access:
- GET /api/page/fail2ban: requires fail2ban:view.
- GET /api/fail2ban/jails: requires fail2ban:view.
- GET /api/fail2ban/jails/<jail>: requires fail2ban:view.
- POST /api/fail2ban/jails/<jail>/action: requires fail2ban:manage.
- POST /api/fail2ban/jails/<jail>/ban: requires fail2ban:manage.
- POST /api/fail2ban/jails/<jail>/unban: requires fail2ban:manage.
- POST /api/fail2ban/jails/<jail>/whitelist: requires fail2ban:manage.
- POST /api/fail2ban/action: requires fail2ban:manage.
- GET /api/fail2ban/logs: requires fail2ban:view.

Workflow:
- The page opens with two tabs: Jails and Logs.
- Jails tab loads the active fail2ban jail list and summary counters (currently failed and currently banned).
- Selecting a jail loads detail data for that jail, including banned IPs and ignore IPs.
- Operators can ban and unban specific IPs in the selected jail.
- Operators can add or remove ignore IP entries in the selected jail.
- Operators can run jail-level start, stop, and reload actions, plus global reload and restart actions.
- Logs tab displays recent lines from the configured fail2ban log file and can refresh on demand.

Validation and safety rules:
- Jail names must match [A-Za-z0-9_-]{1,64}.
- IP values must be valid IPv4 or IPv6, optionally with CIDR masks.
- Jail actions are allowlisted to start, stop, and reload.
- Global actions are allowlisted to reload and restart.
- Whitelist actions are allowlisted to add and remove.
- Runtime commands execute through direct exec calls to fail2ban-client; shell interpolation is not used.
- Log reads are capped to 500 lines per request.

Hidden dependencies and configuration:
- Requires fail2ban-client on the host.
- CUDDLEPANEL_FAIL2BAN_CLIENT_BIN overrides the fail2ban-client binary path. Default: /usr/bin/fail2ban-client.
- CUDDLEPANEL_FAIL2BAN_LOG overrides the fail2ban log file path. Default: /var/log/fail2ban.log.

Gotchas and debugging:
- Some fail2ban installations disable certain commands depending on version and jail backend. Surface command output directly so operators can diagnose host-specific behavior.
- A jail listed by status can still reject edits if its configuration is invalid or pending reload.
- Ignore IP output format differs by fail2ban version. The parser only keeps valid IPv4/IPv6 tokens and skips unknown text.
