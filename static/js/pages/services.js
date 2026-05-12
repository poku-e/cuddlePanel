import {postParams, requestJson} from "../core/api.js";
import {escapeHtml} from "../core/dom.js";
import {showErrorToast, showSuccessToast} from "../core/toast.js";

let servicesCache = [];
let serviceUnitConfigCache = null;

const SERVICE_MANAGE_SCHEMA = [
    {
        section: "Unit",
        title: "Unit relationships",
        description: "High-level identity, ordering, dependencies, and start-rate behavior.",
        groups: [
            {title: "Identity and docs", description: "Core naming and reference material.", keys: ["Description", "Documentation"]},
            {title: "Dependencies and order", description: "Boot order, relationships, and failure links.", keys: ["Wants", "Requires", "Requisite", "BindsTo", "PartOf", "Conflicts", "Before", "After", "OnFailure", "OnSuccess"]},
            {title: "Limits and conditions", description: "Rate limiting and startup checks.", keys: ["StartLimitIntervalSec", "StartLimitBurst", "ConditionPathExists", "ConditionPathIsDirectory", "ConditionUser", "ConditionGroup", "AssertPathExists"]}
        ],
        fields: [
            {key: "Description", label: "Description", type: "text", placeholder: "Human-friendly summary"},
            {key: "Documentation", label: "Documentation", type: "textarea", placeholder: "https://docs.example.com/service"},
            {key: "Wants", label: "Wants", type: "textarea", placeholder: "network-online.target"},
            {key: "Requires", label: "Requires", type: "textarea", placeholder: "postgresql.service"},
            {key: "Requisite", label: "Requisite", type: "textarea", placeholder: "docker.service"},
            {key: "BindsTo", label: "Binds To", type: "textarea", placeholder: "mnt-data.mount"},
            {key: "PartOf", label: "Part Of", type: "textarea", placeholder: "app-stack.target"},
            {key: "Conflicts", label: "Conflicts", type: "textarea", placeholder: "legacy-app.service"},
            {key: "Before", label: "Before", type: "textarea", placeholder: "nginx.service"},
            {key: "After", label: "After", type: "textarea", placeholder: "network.target"},
            {key: "OnFailure", label: "On Failure", type: "textarea", placeholder: "alert-mailer.service"},
            {key: "OnSuccess", label: "On Success", type: "textarea", placeholder: "follow-up.target"},
            {key: "StartLimitIntervalSec", label: "Start Limit Interval", type: "text", placeholder: "30s"},
            {key: "StartLimitBurst", label: "Start Limit Burst", type: "text", placeholder: "5"},
            {key: "ConditionPathExists", label: "Condition Path Exists", type: "textarea", placeholder: "/srv/app/.env"},
            {key: "ConditionPathIsDirectory", label: "Condition Directory Exists", type: "textarea", placeholder: "/srv/app"},
            {key: "ConditionUser", label: "Condition User", type: "text", placeholder: "deploy"},
            {key: "ConditionGroup", label: "Condition Group", type: "text", placeholder: "www-data"},
            {key: "AssertPathExists", label: "Assert Path Exists", type: "textarea", placeholder: "/srv/app/current"}
        ]
    },
    {
        section: "Service",
        title: "Service runtime",
        description: "Execution model, commands, user context, retries, timeouts, environment, and sandboxing.",
        groups: [
            {title: "Startup and commands", description: "Primary execution flow and lifecycle hooks.", keys: ["Type", "ExecCondition", "ExecStartPre", "ExecStart", "ExecStartPost", "ExecReload", "ExecStop", "ExecStopPost"]},
            {title: "Restart and timing", description: "Recovery behavior and watchdog or timeout controls.", keys: ["Restart", "RestartSec", "TimeoutStartSec", "TimeoutStopSec", "TimeoutAbortSec", "WatchdogSec"]},
            {title: "Identity and environment", description: "User context, working directory, env files, and standard streams.", keys: ["User", "Group", "DynamicUser", "SupplementaryGroups", "WorkingDirectory", "RootDirectory", "Environment", "EnvironmentFile", "PassEnvironment", "UMask", "Nice", "StandardInput", "StandardOutput", "StandardError", "SyslogIdentifier", "PIDFile"]},
            {title: "Directories and paths", description: "Managed runtime directories and filesystem path allowances.", keys: ["RuntimeDirectory", "StateDirectory", "CacheDirectory", "LogsDirectory", "ConfigurationDirectory", "ReadWritePaths", "ReadOnlyPaths", "InaccessiblePaths", "BindPaths", "BindReadOnlyPaths"]},
            {title: "Isolation and hardening", description: "Namespace, protection, capabilities, and device restrictions.", keys: ["PrivateTmp", "PrivateDevices", "PrivateNetwork", "PrivateUsers", "ProtectSystem", "ProtectHome", "ProtectKernelTunables", "ProtectKernelModules", "ProtectControlGroups", "ProtectProc", "ProcSubset", "NoNewPrivileges", "CapabilityBoundingSet", "AmbientCapabilities", "DevicePolicy"]},
            {title: "Resource controls", description: "Kernel resource and cgroup limits.", keys: ["LimitNOFILE", "LimitNPROC", "MemoryMax", "TasksMax", "CPUQuota"]}
        ],
        fields: [
            {key: "Type", label: "Service Type", type: "select", options: ["simple", "exec", "forking", "oneshot", "dbus", "notify", "notify-reload", "idle"]},
            {key: "ExecCondition", label: "ExecCondition", type: "textarea", placeholder: "/usr/bin/test -f /srv/app/.env"},
            {key: "ExecStartPre", label: "ExecStartPre", type: "textarea", placeholder: "/usr/bin/env bash -lc 'echo preparing'"},
            {key: "ExecStart", label: "ExecStart", type: "textarea", placeholder: "/usr/bin/node /srv/app/server.js"},
            {key: "ExecStartPost", label: "ExecStartPost", type: "textarea", placeholder: "/usr/bin/logger app started"},
            {key: "ExecReload", label: "ExecReload", type: "textarea", placeholder: "/bin/kill -HUP $MAINPID"},
            {key: "ExecStop", label: "ExecStop", type: "textarea", placeholder: "/bin/kill -TERM $MAINPID"},
            {key: "ExecStopPost", label: "ExecStopPost", type: "textarea", placeholder: "/usr/bin/logger app stopped"},
            {key: "Restart", label: "Restart Policy", type: "select", options: ["", "no", "on-success", "on-failure", "on-abnormal", "on-watchdog", "on-abort", "always"]},
            {key: "RestartSec", label: "Restart Delay", type: "text", placeholder: "5s"},
            {key: "TimeoutStartSec", label: "Start Timeout", type: "text", placeholder: "60s"},
            {key: "TimeoutStopSec", label: "Stop Timeout", type: "text", placeholder: "30s"},
            {key: "TimeoutAbortSec", label: "Abort Timeout", type: "text", placeholder: "2min"},
            {key: "WatchdogSec", label: "Watchdog", type: "text", placeholder: "0"},
            {key: "User", label: "Run As User", type: "text", placeholder: "deploy"},
            {key: "Group", label: "Run As Group", type: "text", placeholder: "deploy"},
            {key: "DynamicUser", label: "Dynamic User", type: "select", options: ["", "yes", "no"]},
            {key: "SupplementaryGroups", label: "Supplementary Groups", type: "textarea", placeholder: "www-data"},
            {key: "WorkingDirectory", label: "Working Directory", type: "text", placeholder: "/srv/app/current"},
            {key: "RootDirectory", label: "Root Directory", type: "text", placeholder: "/srv/chroot/app"},
            {key: "Environment", label: "Environment", type: "textarea", placeholder: "NODE_ENV=production"},
            {key: "EnvironmentFile", label: "Environment Files", type: "textarea", placeholder: "/etc/default/app"},
            {key: "PassEnvironment", label: "Pass Environment", type: "textarea", placeholder: "HTTP_PROXY HTTPS_PROXY"},
            {key: "UMask", label: "UMask", type: "text", placeholder: "0027"},
            {key: "Nice", label: "Nice", type: "text", placeholder: "5"},
            {key: "StandardInput", label: "Standard Input", type: "text", placeholder: "null"},
            {key: "StandardOutput", label: "Standard Output", type: "text", placeholder: "journal"},
            {key: "StandardError", label: "Standard Error", type: "text", placeholder: "journal"},
            {key: "SyslogIdentifier", label: "Syslog Identifier", type: "text", placeholder: "my-service"},
            {key: "PIDFile", label: "PID File", type: "text", placeholder: "/run/app.pid"},
            {key: "RuntimeDirectory", label: "Runtime Directory", type: "textarea", placeholder: "my-service"},
            {key: "StateDirectory", label: "State Directory", type: "textarea", placeholder: "my-service"},
            {key: "CacheDirectory", label: "Cache Directory", type: "textarea", placeholder: "my-service"},
            {key: "LogsDirectory", label: "Logs Directory", type: "textarea", placeholder: "my-service"},
            {key: "ConfigurationDirectory", label: "Configuration Directory", type: "textarea", placeholder: "my-service"},
            {key: "ReadWritePaths", label: "Read/Write Paths", type: "textarea", placeholder: "/srv/app/storage"},
            {key: "ReadOnlyPaths", label: "Read Only Paths", type: "textarea", placeholder: "/srv/app/config"},
            {key: "InaccessiblePaths", label: "Inaccessible Paths", type: "textarea", placeholder: "/home"},
            {key: "BindPaths", label: "Bind Paths", type: "textarea", placeholder: "/srv/data:/var/lib/app"},
            {key: "BindReadOnlyPaths", label: "Bind Read Only Paths", type: "textarea", placeholder: "/etc/ssl:/etc/ssl"},
            {key: "PrivateTmp", label: "Private Tmp", type: "select", options: ["", "yes", "no"]},
            {key: "PrivateDevices", label: "Private Devices", type: "select", options: ["", "yes", "no"]},
            {key: "PrivateNetwork", label: "Private Network", type: "select", options: ["", "yes", "no"]},
            {key: "PrivateUsers", label: "Private Users", type: "select", options: ["", "yes", "no"]},
            {key: "ProtectSystem", label: "Protect System", type: "select", options: ["", "no", "yes", "full", "strict"]},
            {key: "ProtectHome", label: "Protect Home", type: "select", options: ["", "no", "yes", "read-only", "tmpfs"]},
            {key: "ProtectKernelTunables", label: "Protect Kernel Tunables", type: "select", options: ["", "yes", "no"]},
            {key: "ProtectKernelModules", label: "Protect Kernel Modules", type: "select", options: ["", "yes", "no"]},
            {key: "ProtectControlGroups", label: "Protect Control Groups", type: "select", options: ["", "yes", "no"]},
            {key: "ProtectProc", label: "Protect Proc", type: "select", options: ["", "default", "invisible", "noaccess"]},
            {key: "ProcSubset", label: "Proc Subset", type: "select", options: ["", "all", "pid"]},
            {key: "NoNewPrivileges", label: "No New Privileges", type: "select", options: ["", "yes", "no"]},
            {key: "CapabilityBoundingSet", label: "Capability Bounding Set", type: "textarea", placeholder: "CAP_NET_BIND_SERVICE"},
            {key: "AmbientCapabilities", label: "Ambient Capabilities", type: "textarea", placeholder: "CAP_NET_BIND_SERVICE"},
            {key: "DevicePolicy", label: "Device Policy", type: "select", options: ["", "auto", "closed", "strict"]},
            {key: "LimitNOFILE", label: "Limit NOFILE", type: "text", placeholder: "65535"},
            {key: "LimitNPROC", label: "Limit NPROC", type: "text", placeholder: "4096"},
            {key: "MemoryMax", label: "Memory Max", type: "text", placeholder: "1G"},
            {key: "TasksMax", label: "Tasks Max", type: "text", placeholder: "1024"},
            {key: "CPUQuota", label: "CPU Quota", type: "text", placeholder: "200%"}
        ]
    },
    {
        section: "Install",
        title: "Install targets",
        description: "Enablement and aliasing directives used by systemctl enable/disable.",
        groups: [
            {title: "Targets and aliases", description: "Enablement targets and linked units.", keys: ["WantedBy", "RequiredBy", "Alias", "Also", "DefaultInstance"]}
        ],
        fields: [
            {key: "WantedBy", label: "Wanted By", type: "textarea", placeholder: "multi-user.target"},
            {key: "RequiredBy", label: "Required By", type: "textarea", placeholder: "app-stack.target"},
            {key: "Alias", label: "Alias", type: "textarea", placeholder: "my-app.service"},
            {key: "Also", label: "Also", type: "textarea", placeholder: "my-app-worker.service"},
            {key: "DefaultInstance", label: "Default Instance", type: "text", placeholder: "main"}
        ]
    }
];

function canManageList() {
    return document.getElementById("servicesPageState")?.dataset.canManage === "1";
}

function canManageDetail() {
    return document.getElementById("serviceDetailPageState")?.dataset.canManage === "1";
}

function validServiceUnit(unit) {
    return /^[A-Za-z0-9._@-]{1,128}\.service$/.test(unit);
}

function servicePageKey(unit) {
    return `service:${unit}`;
}

function stateBadge(state) {
    if (state === "active") {
        return '<span class="badge text-bg-success">active</span>';
    }
    if (state === "inactive") {
        return '<span class="badge text-bg-secondary">inactive</span>';
    }
    if (state === "failed") {
        return '<span class="badge text-bg-danger">failed</span>';
    }
    return `<span class="badge text-bg-light">${escapeHtml(state || "unknown")}</span>`;
}

function enabledBadge(state) {
    if (state === "enabled") {
        return '<span class="badge text-bg-primary">enabled</span>';
    }
    if (state === "disabled") {
        return '<span class="badge text-bg-secondary">disabled</span>';
    }
    return `<span class="badge text-bg-light">${escapeHtml(state || "unknown")}</span>`;
}

function openServicePage(unit) {
    window.location.hash = `page=${encodeURIComponent(servicePageKey(unit))}`;
}

function filterServices(services, query) {
    const normalizedQuery = query.trim().toLowerCase();
    if (!normalizedQuery) {
        return services;
    }
    return services.filter((service) => {
        const haystack = [
            service.name,
            service.unit,
            service.description,
            service.state,
            service.subState,
            service.enabledState,
            service.loadState
        ].join(" ").toLowerCase();
        return haystack.includes(normalizedQuery);
    });
}

function renderServices(services) {
    const host = document.getElementById("servicesTableHost");
    if (!services.length) {
        host.innerHTML = '<tr><td colspan="6">No discovered host services were returned by systemctl.</td></tr>';
        return;
    }

    host.innerHTML = services.map((service) => `
        <tr data-service-unit="${escapeHtml(service.unit)}">
            <td>
                <button class="btn btn-link p-0 service-manage-link service-name-link" type="button">${escapeHtml(service.name)}</button>
                <div class="small text-secondary service-secondary-line">${escapeHtml(service.subState || service.loadState || "No additional runtime detail")}</div>
            </td>
            <td><code>${escapeHtml(service.unit)}</code></td>
            <td>${stateBadge(service.state)}</td>
            <td>${enabledBadge(service.enabledState)}</td>
            <td class="small">${escapeHtml(service.description || "No description reported")}</td>
            <td class="text-end">
                <div class="btn-toolbar justify-content-end gap-1">
                    <div class="btn-group btn-group-sm">
                        <button class="btn btn-outline-success service-action-button" data-action="start" type="button" ${canManageList() ? "" : "disabled"}>Start</button>
                        <button class="btn btn-outline-warning service-action-button" data-action="restart" type="button" ${canManageList() ? "" : "disabled"}>Restart</button>
                        <button class="btn btn-outline-secondary service-action-button" data-action="stop" type="button" ${canManageList() ? "" : "disabled"}>Stop</button>
                    </div>
                    <button class="btn btn-outline-primary btn-sm service-manage-button" type="button">Manage</button>
                </div>
            </td>
        </tr>
    `).join("");
}

function updateResultsMeta(totalCount, visibleCount, query) {
    const host = document.getElementById("servicesResultsMeta");
    if (!host) {
        return;
    }
    if (!totalCount) {
        host.textContent = "No discovered services.";
        return;
    }
    if (!query.trim()) {
        host.textContent = `${totalCount} service${totalCount === 1 ? "" : "s"} discovered`;
        return;
    }
    host.textContent = `${visibleCount} of ${totalCount} service${totalCount === 1 ? "" : "s"} match “${query.trim()}”`;
}

function renderFilteredServices() {
    const query = document.getElementById("servicesSearchInput")?.value || "";
    const filtered = filterServices(servicesCache, query);
    renderServices(filtered);
    wireServiceRows();
    updateResultsMeta(servicesCache.length, filtered.length, query);
}

function wireServiceRows() {
    document.querySelectorAll(".service-manage-link, .service-manage-button").forEach((button) => {
        button.addEventListener("click", () => {
            const row = button.closest("[data-service-unit]");
            openServicePage(row.dataset.serviceUnit);
        });
    });

    document.querySelectorAll(".service-action-button").forEach((button) => {
        button.addEventListener("click", async () => {
            const row = button.closest("[data-service-unit]");
            try {
                const payload = await postParams(`/api/services/${encodeURIComponent(row.dataset.serviceUnit)}/action`, {
                    action: button.dataset.action
                });
                showSuccessToast(payload.output || `Service ${button.dataset.action} completed.`);
                await refreshServicesPage();
            } catch (error) {
                showErrorToast(error.message);
            }
        });
    });
}

async function refreshServicesPage() {
    const message = document.getElementById("servicesMessage");
    try {
        const payload = await requestJson("/api/services");
        servicesCache = payload.services || [];
        renderFilteredServices();
        message.textContent = "";
    } catch (error) {
        message.textContent = error.message;
    }
}

function setDetailText(id, value) {
    const element = document.getElementById(id);
    if (element) {
        element.textContent = value;
    }
}

function setStatusMessage(id, text, isError = false) {
    const element = document.getElementById(id);
    if (!element) {
        return;
    }
    element.textContent = text;
    element.className = isError ? "small text-danger mt-3" : "small mt-3";
}

function updateRuntimeOutput(meta, output) {
    setDetailText("serviceDetailRuntimeMeta", meta);
    setDetailText("serviceDetailRuntimeOutput", output || "No runtime output.");
}

function directiveValue(sectionName, key) {
    const values = serviceUnitConfigCache?.sections?.[sectionName]?.[key] || [];
    return values.join("\n");
}

function additionalDirectiveBlock(sectionName, knownKeys) {
    const directives = serviceUnitConfigCache?.sections?.[sectionName] || {};
    const lines = [];
    Object.entries(directives).forEach(([key, values]) => {
        if (knownKeys.has(key)) {
            return;
        }
        values.forEach((value) => {
            lines.push(`${key}=${value}`);
        });
    });
    return lines.join("\n");
}

function renderField(sectionName, field) {
    if (!field) {
        return "";
    }
    const inputId = `service-field-${sectionName}-${field.key}`;
    const escapedId = escapeHtml(inputId);
    const escapedLabel = escapeHtml(field.label);
    const escapedPlaceholder = escapeHtml(field.placeholder || "");
    const escapedName = escapeHtml(`${sectionName}::${field.key}`);
    const value = directiveValue(sectionName, field.key);
    const disabled = canManageDetail() ? "" : " disabled";
    if (field.type === "select") {
        return `
            <label class="cp-label service-manage-field">
                <span>${escapedLabel}</span>
                <select class="cp-input cp-select" id="${escapedId}" data-service-field="${escapedName}"${disabled}>
                    ${field.options.map((option) => `<option value="${escapeHtml(option)}"${value === option ? " selected" : ""}>${escapeHtml(option || "Unspecified")}</option>`).join("")}
                </select>
            </label>
        `;
    }
    if (field.type === "textarea") {
        return `
            <label class="cp-label service-manage-field service-manage-field-wide">
                <span>${escapedLabel}</span>
                <textarea class="cp-input cp-textarea service-manage-textarea" id="${escapedId}" data-service-field="${escapedName}" placeholder="${escapedPlaceholder}"${disabled}>${escapeHtml(value)}</textarea>
            </label>
        `;
    }
    return `
        <label class="cp-label service-manage-field">
            <span>${escapedLabel}</span>
            <input class="cp-input" id="${escapedId}" data-service-field="${escapedName}" value="${escapeHtml(value)}" placeholder="${escapedPlaceholder}"${disabled}>
        </label>
    `;
}

function sectionFieldMap(sectionConfig) {
    return new Map(sectionConfig.fields.map((field) => [field.key, field]));
}

function sectionFieldCount(sectionConfig) {
    return sectionConfig.fields.length;
}

function sectionFilledCount(sectionConfig) {
    return sectionConfig.fields.filter((field) => directiveValue(sectionConfig.section, field.key).trim()).length;
}

function renderManageOverview() {
    const host = document.getElementById("serviceManageOverview");
    if (!host || !serviceUnitConfigCache) {
        return;
    }
    host.innerHTML = SERVICE_MANAGE_SCHEMA.map((sectionConfig) => {
        const groupCount = sectionConfig.groups.length;
        const filledCount = sectionFilledCount(sectionConfig);
        return `
            <article class="service-manage-overview-card">
                <div class="service-manage-overview-label">${escapeHtml(sectionConfig.section)}</div>
                <div class="service-manage-overview-value">${filledCount}/${sectionFieldCount(sectionConfig)}</div>
                <div class="service-manage-overview-note">${groupCount} accordion ${groupCount === 1 ? "group" : "groups"} in ${escapeHtml(sectionConfig.title.toLowerCase())}</div>
            </article>
        `;
    }).join("");
}

function renderManageSections() {
    const host = document.getElementById("serviceManageSections");
    if (!host || !serviceUnitConfigCache) {
        return;
    }
    renderManageOverview();
    host.innerHTML = SERVICE_MANAGE_SCHEMA.map((sectionConfig) => {
        const fieldMap = sectionFieldMap(sectionConfig);
        const knownKeys = new Set(sectionConfig.fields.map((field) => field.key));
        const extraLines = additionalDirectiveBlock(sectionConfig.section, knownKeys);
        const accordionId = `service-manage-accordion-${sectionConfig.section.toLowerCase()}`;
        return `
            <section class="service-manage-section">
                <div class="service-manage-section-head">
                    <div>
                        <h4 class="h6 mb-1">${escapeHtml(sectionConfig.title)}</h4>
                        <div class="small">${escapeHtml(sectionConfig.description)}</div>
                    </div>
                    <div class="service-manage-section-meta">
                        <span>${sectionFilledCount(sectionConfig)} populated</span>
                        <span>${sectionFieldCount(sectionConfig)} fields</span>
                    </div>
                </div>
                <div class="accordion service-manage-accordion" id="${escapeHtml(accordionId)}">
                    ${sectionConfig.groups.map((group, index) => `
                        <div class="accordion-item service-manage-accordion-item">
                            <h5 class="accordion-header">
                                <button class="accordion-button ${index === 0 ? "" : "collapsed"} service-manage-accordion-button" type="button" data-bs-toggle="collapse" data-bs-target="#${escapeHtml(`${accordionId}-${index}`)}" aria-expanded="${index === 0 ? "true" : "false"}" aria-controls="${escapeHtml(`${accordionId}-${index}`)}">
                                    <span>
                                        <span class="service-manage-accordion-title">${escapeHtml(group.title)}</span>
                                        <span class="service-manage-accordion-note">${escapeHtml(group.description)}</span>
                                    </span>
                                </button>
                            </h5>
                            <div id="${escapeHtml(`${accordionId}-${index}`)}" class="accordion-collapse collapse ${index === 0 ? "show" : ""}" data-bs-parent="#${escapeHtml(accordionId)}">
                                <div class="accordion-body">
                                    <div class="service-manage-fields">
                                        ${group.keys.map((key) => renderField(sectionConfig.section, fieldMap.get(key))).join("")}
                                    </div>
                                </div>
                            </div>
                        </div>
                    `).join("")}
                    <div class="accordion-item service-manage-accordion-item">
                        <h5 class="accordion-header">
                            <button class="accordion-button collapsed service-manage-accordion-button" type="button" data-bs-toggle="collapse" data-bs-target="#${escapeHtml(`${accordionId}-extra`)}" aria-expanded="false" aria-controls="${escapeHtml(`${accordionId}-extra`)}">
                                <span>
                                    <span class="service-manage-accordion-title">Additional directives</span>
                                    <span class="service-manage-accordion-note">Use raw `Key=Value` lines for directives not covered above.</span>
                                </span>
                            </button>
                        </h5>
                        <div id="${escapeHtml(`${accordionId}-extra`)}" class="accordion-collapse collapse" data-bs-parent="#${escapeHtml(accordionId)}">
                            <div class="accordion-body">
                                <label class="cp-label service-manage-field service-manage-field-wide">
                                    <span>Additional ${escapeHtml(sectionConfig.section)} directives</span>
                                    <textarea class="cp-input cp-textarea service-manage-textarea" data-service-extra="${escapeHtml(sectionConfig.section)}" placeholder="Directive=Value&#10;AnotherDirective=Value"${canManageDetail() ? "" : " disabled"}>${escapeHtml(extraLines)}</textarea>
                                </label>
                            </div>
                        </div>
                    </div>
                </div>
            </section>
        `;
    }).join("");
}

function splitTextareaLines(value) {
    return value.split("\n").map((line) => line.trim()).filter(Boolean);
}

function appendDirectiveLines(lines, key, value, fieldType) {
    if (!value) {
        return;
    }
    if (fieldType === "textarea") {
        splitTextareaLines(value).forEach((line) => {
            lines.push(`${key}=${line}`);
        });
        return;
    }
    const normalized = value.trim();
    if (!normalized) {
        return;
    }
    lines.push(`${key}=${normalized}`);
}

function collectExtraDirectives(sectionName) {
    const source = document.querySelector(`[data-service-extra="${sectionName}"]`)?.value || "";
    return splitTextareaLines(source).map((line) => {
        const separator = line.indexOf("=");
        if (separator <= 0) {
            throw new Error(`Each additional ${sectionName} directive must be written as Key=Value.`);
        }
        const key = line.slice(0, separator).trim();
        if (!/^[A-Za-z][A-Za-z0-9]*$/.test(key)) {
            throw new Error(`Invalid ${sectionName} directive key: ${key}`);
        }
        return `${key}=${line.slice(separator + 1).trim()}`;
    });
}

function buildManagedServiceContent() {
    const parts = [];
    SERVICE_MANAGE_SCHEMA.forEach((sectionConfig) => {
        const sectionLines = [];
        sectionConfig.fields.forEach((field) => {
            const control = document.querySelector(`[data-service-field="${sectionConfig.section}::${field.key}"]`);
            if (!control) {
                return;
            }
            appendDirectiveLines(sectionLines, field.key, control.value || "", field.type);
        });
        collectExtraDirectives(sectionConfig.section).forEach((line) => {
            sectionLines.push(line);
        });
        if (!sectionLines.length) {
            return;
        }
        parts.push(`[${sectionConfig.section}]`);
        parts.push(...sectionLines);
        parts.push("");
    });
    return `${parts.join("\n").trimEnd()}\n`;
}

async function loadServiceDetail(unit) {
    if (!validServiceUnit(unit)) {
        throw new Error("Invalid service unit.");
    }
    const payload = await requestJson(`/api/services/${encodeURIComponent(unit)}`);
    const service = payload.service;
    setDetailText("serviceDetailName", service.name || unit);
    setDetailText("serviceDetailDescription", service.description || "No description reported.");
    setDetailText("serviceDetailState", service.state || "unknown");
    setDetailText("serviceDetailSubState", service.subState ? `Sub-state: ${service.subState}` : "No sub-state reported.");
    setDetailText("serviceDetailEnabledState", service.enabledState || "unknown");
    setDetailText("serviceDetailLoadState", service.loadState ? `Load state: ${service.loadState}` : "No load state reported.");
    setDetailText("serviceDetailUnit", service.unit || unit);
    setDetailText("serviceDetailFragmentPath", service.fragmentPath || "No fragment path reported.");
    setDetailText("serviceDetailUnitMetadata", service.unit || unit);
    setDetailText("serviceDetailDescriptionMetadata", service.description || "No description reported.");
    setDetailText("serviceDetailFragmentMetadata", service.fragmentPath || "No fragment path reported.");
    setDetailText("serviceDetailLoadMetadata", service.loadState || "unknown");
    setDetailText("serviceDetailStateMetadata", service.state || "unknown");
    setDetailText("serviceDetailSubStateMetadata", service.subState || "unknown");
    setDetailText("serviceDetailEnablementMetadata", service.enabledState || "unknown");
    return service;
}

async function loadServiceUnitConfig(unit) {
    const payload = await requestJson(`/api/services/${encodeURIComponent(unit)}/unit-file`);
    serviceUnitConfigCache = payload.config;
    setDetailText("serviceDetailEditablePath", serviceUnitConfigCache.path || "Unavailable");
    const editor = document.getElementById("serviceAdvancedEditor");
    if (editor) {
        editor.value = serviceUnitConfigCache.content || "";
    }
    renderManageSections();
    setStatusMessage("serviceManageMessage", `Loaded structured fields from ${serviceUnitConfigCache.path}.`);
    setStatusMessage("serviceAdvancedMessage", `Loaded ${serviceUnitConfigCache.path}.`);
}

function clearServiceUnitConfig(message, isError = false) {
    serviceUnitConfigCache = null;
    setDetailText("serviceDetailEditablePath", "Unavailable");
    const editor = document.getElementById("serviceAdvancedEditor");
    if (editor) {
        editor.value = "";
    }
    const sectionsHost = document.getElementById("serviceManageSections");
    if (sectionsHost) {
        sectionsHost.innerHTML = "";
    }
    setStatusMessage("serviceManageMessage", message, isError);
    setStatusMessage("serviceAdvancedMessage", message, isError);
}

async function saveServiceUnitContent(unit, content, source) {
    const payload = await postParams(`/api/services/${encodeURIComponent(unit)}/unit-file`, {content});
    updateRuntimeOutput(`${unit}: ${source}`, payload.output || "Service unit saved.");
    showSuccessToast(payload.output || "Service unit saved.");
    await Promise.all([loadServiceDetail(unit), loadServiceUnitConfig(unit)]);
}

async function runServiceDetailAction(unit, action) {
    const payload = await postParams(`/api/services/${encodeURIComponent(unit)}/action`, {action});
    updateRuntimeOutput(`${unit}: ${action}`, payload.output || "No runtime output.");
    showSuccessToast(payload.output || `Service ${action} completed.`);
    await loadServiceDetail(unit);
}

export async function initServicesPage() {
    if (!document.getElementById("servicesTableHost")) {
        return;
    }
    document.getElementById("refreshServicesButton").addEventListener("click", refreshServicesPage);
    document.getElementById("servicesSearchInput").addEventListener("input", renderFilteredServices);
    await refreshServicesPage();
}

export async function initServiceDetailPage(pageUnit) {
    const state = document.getElementById("serviceDetailPageState");
    if (!state) {
        return;
    }
    const unit = state.dataset.unit || pageUnit;
    if (!validServiceUnit(unit)) {
        throw new Error("Invalid service unit.");
    }

    const refresh = async () => {
        try {
            await loadServiceDetail(unit);
            try {
                await loadServiceUnitConfig(unit);
            } catch (error) {
                clearServiceUnitConfig(error.message, true);
            }
        } catch (error) {
            clearServiceUnitConfig(error.message, true);
            showErrorToast(error.message);
            throw error;
        }
    };

    document.getElementById("serviceDetailBackButton").addEventListener("click", () => {
        window.location.hash = "page=services";
    });
    document.getElementById("serviceDetailRefreshButton").addEventListener("click", refresh);
    document.getElementById("serviceManageReloadButton")?.addEventListener("click", async () => {
        try {
            await loadServiceUnitConfig(unit);
        } catch (error) {
            clearServiceUnitConfig(error.message, true);
            showErrorToast(error.message);
        }
    });
    document.getElementById("serviceAdvancedReloadButton")?.addEventListener("click", async () => {
        try {
            await loadServiceUnitConfig(unit);
        } catch (error) {
            clearServiceUnitConfig(error.message, true);
            showErrorToast(error.message);
        }
    });
    document.getElementById("serviceManageSaveButton")?.addEventListener("click", async () => {
        try {
            setStatusMessage("serviceManageMessage", "Saving structured service configuration...");
            if (!serviceUnitConfigCache) {
                throw new Error("This service does not expose an editable unit file.");
            }
            const content = buildManagedServiceContent();
            const editor = document.getElementById("serviceAdvancedEditor");
            if (editor) {
                editor.value = content;
            }
            await saveServiceUnitContent(unit, content, "managed save");
            setStatusMessage("serviceManageMessage", "Structured service configuration saved.");
            setStatusMessage("serviceAdvancedMessage", "Advanced editor refreshed from saved service file.");
        } catch (error) {
            setStatusMessage("serviceManageMessage", error.message, true);
            showErrorToast(error.message);
        }
    });
    document.getElementById("serviceAdvancedSaveButton")?.addEventListener("click", async () => {
        try {
            setStatusMessage("serviceAdvancedMessage", "Saving service file...");
            if (!serviceUnitConfigCache) {
                throw new Error("This service does not expose an editable unit file.");
            }
            const content = document.getElementById("serviceAdvancedEditor")?.value || "";
            await saveServiceUnitContent(unit, content, "advanced save");
            setStatusMessage("serviceAdvancedMessage", "Service file saved.");
            setStatusMessage("serviceManageMessage", "Structured fields refreshed from the saved file.");
        } catch (error) {
            setStatusMessage("serviceAdvancedMessage", error.message, true);
            showErrorToast(error.message);
        }
    });
    document.querySelectorAll(".service-detail-action-button").forEach((button) => {
        button.disabled = !canManageDetail();
        button.addEventListener("click", async () => {
            try {
                await runServiceDetailAction(unit, button.dataset.action);
            } catch (error) {
                updateRuntimeOutput(`${unit}: ${button.dataset.action}`, error.message);
                showErrorToast(error.message);
            }
        });
    });

    await refresh();
}
