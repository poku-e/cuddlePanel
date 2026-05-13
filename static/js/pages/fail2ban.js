import {postParams, requestJson} from "../core/api.js";
import {escapeHtml} from "../core/dom.js";
import {showErrorToast, showSuccessToast} from "../core/toast.js";

let jailCache = [];
let selectedJail = "";

function canManage() {
    return document.getElementById("fail2banPageState")?.dataset.canManage === "1";
}

function setOutput(title, output) {
    const meta = document.getElementById("fail2banOutputMeta");
    const body = document.getElementById("fail2banOutput");
    meta.textContent = title;
    body.textContent = output || "No output.";
}

function runningBadge(running) {
    return running
        ? '<span class="badge text-bg-success">running</span>'
        : '<span class="badge text-bg-secondary">stopped</span>';
}

function setIgnoredListHtml(hostId, items, emptyMessage, buttonClass) {
    const host = document.getElementById(hostId);
    if (!items.length) {
        host.textContent = emptyMessage;
        return;
    }

    if (!buttonClass) {
        host.innerHTML = items.map((ip) => `<code>${escapeHtml(ip)}</code>`).join(" ");
        return;
    }

    host.innerHTML = items.map((ip) =>
        `<button class="btn btn-outline-secondary btn-sm ${buttonClass}" data-ip="${escapeHtml(ip)}" type="button">${escapeHtml(ip)}</button>`
    ).join(" ");
}

function setBannedTableHtml(items, emptyMessage) {
    const host = document.getElementById("fail2banBannedIps");
    if (!items.length) {
        host.innerHTML = `<tr><td class="small">${escapeHtml(emptyMessage)}</td></tr>`;
        return;
    }

    host.innerHTML = items.map((ip) => `
        <tr class="fail2ban-banned-row" data-ip="${escapeHtml(ip)}" tabindex="0" role="button" aria-label="Use IP ${escapeHtml(ip)}">
            <td><code>${escapeHtml(ip)}</code></td>
        </tr>
    `).join("");
}

function renderJails() {
    const host = document.getElementById("fail2banJailsHost");
    if (!jailCache.length) {
        host.innerHTML = '<tr><td colspan="5">No fail2ban jails reported.</td></tr>';
        return;
    }

    host.innerHTML = jailCache.map((jail) => `
        <tr data-jail-name="${escapeHtml(jail.name)}" class="fail2ban-jail-row ${selectedJail === jail.name ? "table-active" : ""}">
            <td><button class="btn btn-link p-0 fail2ban-jail-select" type="button">${escapeHtml(jail.name)}</button></td>
            <td>${runningBadge(jail.running)}</td>
            <td>${escapeHtml(String(jail.currentlyFailed))}</td>
            <td>${escapeHtml(String(jail.currentlyBanned))}</td>
            <td class="text-end">
                <div class="btn-group btn-group-sm">
                    <button class="btn btn-outline-success fail2ban-jail-action" data-action="start" type="button" ${canManage() ? "" : "disabled"}>Start</button>
                    <button class="btn btn-outline-secondary fail2ban-jail-action" data-action="stop" type="button" ${canManage() ? "" : "disabled"}>Stop</button>
                    <button class="btn btn-outline-primary fail2ban-jail-action" data-action="reload" type="button" ${canManage() ? "" : "disabled"}>Reload</button>
                </div>
            </td>
        </tr>
    `).join("");

    wireJailRows();
}

function wireJailRows() {
    document.querySelectorAll(".fail2ban-jail-select").forEach((button) => {
        button.addEventListener("click", async () => {
            const row = button.closest("[data-jail-name]");
            selectedJail = row.dataset.jailName;
            renderJails();
            await refreshJailDetail();
        });
    });

    document.querySelectorAll(".fail2ban-jail-action").forEach((button) => {
        button.addEventListener("click", async () => {
            if (!selectedJail) {
                showErrorToast("Select a jail first.");
                return;
            }
            try {
                const payload = await postParams(`/api/fail2ban/jails/${encodeURIComponent(selectedJail)}/action`, {
                    action: button.dataset.action
                });
                setOutput(`${selectedJail}: ${button.dataset.action}`, payload.output);
                showSuccessToast(payload.output || "Fail2ban jail action completed.");
                await refreshJails();
                await refreshJailDetail();
            } catch (error) {
                setOutput(`${selectedJail}: ${button.dataset.action}`, error.message);
                showErrorToast(error.message);
            }
        });
    });
}

async function refreshJails() {
    const message = document.getElementById("fail2banMessage");
    try {
        const payload = await requestJson("/api/fail2ban/jails");
        jailCache = payload.jails || [];
        if (!selectedJail && jailCache.length) {
            selectedJail = jailCache[0].name;
        }
        if (selectedJail && !jailCache.some((jail) => jail.name === selectedJail)) {
            selectedJail = jailCache.length ? jailCache[0].name : "";
        }
        renderJails();
        message.textContent = "";
    } catch (error) {
        message.textContent = error.message;
    }
}

async function refreshJailDetail() {
    const detailMeta = document.getElementById("fail2banDetailMeta");
    if (!selectedJail) {
        detailMeta.textContent = "No jail selected.";
        setBannedTableHtml([], "No jail selected.");
        setIgnoredListHtml("fail2banIgnoredIps", [], "No jail selected.");
        return;
    }

    try {
        const payload = await requestJson(`/api/fail2ban/jails/${encodeURIComponent(selectedJail)}`);
        const jail = payload.jail;
        detailMeta.textContent = `${jail.name}: failed ${jail.currentlyFailed}, banned ${jail.currentlyBanned}`;
        setBannedTableHtml(jail.bannedIps || [], "No banned IPs.");
        setIgnoredListHtml("fail2banIgnoredIps", jail.ignoredIps || [], "No ignored IPs.", "fail2ban-ignored-chip");
        wireIpSelectors();
    } catch (error) {
        detailMeta.textContent = error.message;
        setBannedTableHtml([], "Unable to load banned IPs.");
        setIgnoredListHtml("fail2banIgnoredIps", [], "Unable to load ignored IPs.");
    }
}

function wireIpSelectors() {
    const banInput = document.querySelector("#fail2banBanForm [name='ip']");
    const ignoreInput = document.querySelector("#fail2banIgnoreForm [name='ip']");

    document.querySelectorAll(".fail2ban-banned-row").forEach((row) => {
        const selectBannedIp = () => {
            banInput.value = row.dataset.ip;
        };

        row.addEventListener("click", selectBannedIp);
        row.addEventListener("keydown", (event) => {
            if (event.key === "Enter" || event.key === " ") {
                event.preventDefault();
                selectBannedIp();
            }
        });
    });

    document.querySelectorAll(".fail2ban-ignored-chip").forEach((button) => {
        button.addEventListener("click", () => {
            ignoreInput.value = button.dataset.ip;
        });
    });
}

async function runGlobalAction(action) {
    try {
        const payload = await postParams("/api/fail2ban/action", {action});
        setOutput(`global: ${action}`, payload.output);
        showSuccessToast(payload.output || "Fail2ban action completed.");
        await refreshJails();
        await refreshJailDetail();
        await refreshLogs();
    } catch (error) {
        setOutput(`global: ${action}`, error.message);
        showErrorToast(error.message);
    }
}

async function runIpAction(endpoint, title, ip) {
    if (!selectedJail) {
        showErrorToast("Select a jail first.");
        return;
    }
    try {
        const payload = await postParams(`/api/fail2ban/jails/${encodeURIComponent(selectedJail)}/${endpoint}`, {ip});
        setOutput(`${selectedJail}: ${title}`, payload.output);
        showSuccessToast(payload.output || "Fail2ban IP action completed.");
        await refreshJails();
        await refreshJailDetail();
    } catch (error) {
        setOutput(`${selectedJail}: ${title}`, error.message);
        showErrorToast(error.message);
    }
}

async function runWhitelistAction(action, ip) {
    if (!selectedJail) {
        showErrorToast("Select a jail first.");
        return;
    }
    try {
        const payload = await postParams(`/api/fail2ban/jails/${encodeURIComponent(selectedJail)}/whitelist`, {action, ip});
        setOutput(`${selectedJail}: whitelist ${action}`, payload.output);
        showSuccessToast(payload.output || "Fail2ban whitelist updated.");
        await refreshJailDetail();
    } catch (error) {
        setOutput(`${selectedJail}: whitelist ${action}`, error.message);
        showErrorToast(error.message);
    }
}

async function refreshLogs() {
    try {
        const payload = await requestJson("/api/fail2ban/logs?lines=200");
        const output = document.getElementById("fail2banLogsOutput");
        output.textContent = (payload.lines || []).join("\n") || "No fail2ban log lines found.";
    } catch (error) {
        document.getElementById("fail2banLogsOutput").textContent = error.message;
    }
}

export async function initFail2banPage() {
    document.getElementById("refreshFail2banButton")?.addEventListener("click", async () => {
        await refreshJails();
        await refreshJailDetail();
    });

    document.getElementById("reloadFail2banButton")?.addEventListener("click", async () => {
        await runGlobalAction("reload");
    });

    document.getElementById("restartFail2banButton")?.addEventListener("click", async () => {
        await runGlobalAction("restart");
    });

    document.getElementById("refreshFail2banLogsButton")?.addEventListener("click", refreshLogs);

    const banForm = document.getElementById("fail2banBanForm");
    banForm?.addEventListener("submit", async (event) => {
        event.preventDefault();
        const ip = banForm.querySelector("[name='ip']").value.trim();
        if (!ip) {
            showErrorToast("Enter an IP first.");
            return;
        }
        await runIpAction("ban", "ban", ip);
    });

    document.getElementById("fail2banUnbanButton")?.addEventListener("click", async () => {
        const ip = document.querySelector("#fail2banBanForm [name='ip']")?.value.trim();
        if (!ip) {
            showErrorToast("Enter an IP first.");
            return;
        }
        await runIpAction("unban", "unban", ip);
    });

    document.getElementById("fail2banIgnoreAddButton")?.addEventListener("click", async () => {
        const ip = document.querySelector("#fail2banIgnoreForm [name='ip']")?.value.trim();
        if (!ip) {
            showErrorToast("Enter an IP first.");
            return;
        }
        await runWhitelistAction("add", ip);
    });

    document.getElementById("fail2banIgnoreRemoveButton")?.addEventListener("click", async () => {
        const ip = document.querySelector("#fail2banIgnoreForm [name='ip']")?.value.trim();
        if (!ip) {
            showErrorToast("Enter an IP first.");
            return;
        }
        await runWhitelistAction("remove", ip);
    });

    await refreshJails();
    await refreshJailDetail();
    await refreshLogs();
}
