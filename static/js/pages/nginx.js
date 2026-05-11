import {postForm, postParams, requestJson} from "../core/api.js";
import {escapeHtml} from "../core/dom.js";
import {showErrorToast, showSuccessToast} from "../core/toast.js";

const nginxDrafts = new Map();
let nginxSitesCache = [];
let nginxModal = null;

function canManage() {
    return document.getElementById("nginxPageState")?.dataset.canManage === "1";
}

function enabledBadge(enabled) {
    return enabled
        ? '<span class="badge text-bg-success">enabled</span>'
        : '<span class="badge text-bg-secondary">disabled</span>';
}

function renderNginxSites(payload) {
    nginxSitesCache = payload.sites;
    const meta = document.getElementById("nginxMeta");
    meta.textContent = `Available: ${payload.availableDir} | Enabled: ${payload.enabledDir}`;

    const host = document.getElementById("nginxSitesHost");
    if (!payload.sites.length) {
        host.innerHTML = '<tr><td colspan="5">No allowlisted nginx sites yet.</td></tr>';
        return;
    }
    host.innerHTML = payload.sites.map((site) => `
        <tr data-nginx-name="${escapeHtml(site.name)}">
            <td>${escapeHtml(site.name)}</td>
            <td><code>${escapeHtml(site.filename)}</code></td>
            <td>${enabledBadge(site.enabled)}</td>
            <td class="small">${escapeHtml(site.description || "No description")}</td>
            <td class="text-end">
                <div class="btn-toolbar justify-content-end gap-1">
                    <div class="btn-group btn-group-sm">
                        <button class="btn btn-outline-success nginx-action-button" data-action="enable" type="button" ${(canManage() && !site.enabled) ? "" : "disabled"}>Enable</button>
                        <button class="btn btn-outline-secondary nginx-action-button" data-action="disable" type="button" ${(canManage() && site.enabled) ? "" : "disabled"}>Disable</button>
                        <button class="btn btn-outline-warning nginx-action-button" data-action="test" type="button" ${canManage() ? "" : "disabled"}>Test</button>
                        <button class="btn btn-outline-primary nginx-action-button" data-action="reload" type="button" ${canManage() ? "" : "disabled"}>Reload</button>
                    </div>
                    <button class="btn btn-outline-primary btn-sm edit-nginx-button" type="button" ${canManage() ? "" : "disabled"}>Edit</button>
                </div>
            </td>
        </tr>
    `).join("");
}

function setNginxOutput(title, output) {
    document.getElementById("nginxOutputMeta").textContent = title;
    document.getElementById("nginxOutput").textContent = output || "No nginx output yet.";
}

async function refreshNginxPage() {
    const message = document.getElementById("nginxMessage");
    try {
        const payload = await requestJson("/api/nginx/sites");
        renderNginxSites(payload);
        wireNginxRows();
        message.textContent = "";
    } catch (error) {
        message.textContent = error.message;
    }
}

function draftFor(site) {
    return nginxDrafts.get(site.name) || {
        name: site.name,
        filename: site.filename,
        description: site.description,
        content: site.content
    };
}

function openNginxModal(site = null) {
    const form = document.getElementById("nginxEditorForm");
    const message = document.getElementById("nginxEditorMessage");
    const draft = site ? draftFor(site) : {name: "", filename: "", description: "", content: ""};
    form.reset();
    message.textContent = "";
    form.querySelector('[name="original_name"]').value = site?.name || "";
    form.querySelector('[name="name"]').value = draft.name;
    form.querySelector('[name="filename"]').value = draft.filename;
    form.querySelector('[name="description"]').value = draft.description;
    form.querySelector('[name="content"]').value = draft.content;
    document.getElementById("nginxEditorModalTitle").textContent = site ? `Edit site: ${site.name}` : "Register site";
    document.getElementById("nginxEditorModalMeta").textContent = site
        ? "Edit the stored metadata and config without losing unsaved drafts."
        : "Create an allowlisted nginx site entry.";
    nginxModal.show();
}

function captureEditorDraft(form) {
    const original = form.querySelector('[name="original_name"]').value;
    if (!original) {
        return;
    }
    nginxDrafts.set(original, {
        name: form.querySelector('[name="name"]').value,
        filename: form.querySelector('[name="filename"]').value,
        description: form.querySelector('[name="description"]').value,
        content: form.querySelector('[name="content"]').value
    });
}

function wireNginxRows() {
    document.querySelectorAll(".edit-nginx-button").forEach((button) => {
        button.addEventListener("click", () => {
            const row = button.closest("[data-nginx-name]");
            openNginxModal(nginxSitesCache.find((site) => site.name === row.dataset.nginxName) || null);
        });
    });

    document.querySelectorAll(".nginx-action-button").forEach((button) => {
        button.addEventListener("click", async () => {
            const row = button.closest("[data-nginx-name]");
            try {
                const payload = await postParams(`/api/nginx/sites/${encodeURIComponent(row.dataset.nginxName)}/action`, {
                    action: button.dataset.action
                });
                setNginxOutput(`${row.dataset.nginxName}: ${button.dataset.action}`, payload.output);
                showSuccessToast(payload.output || `Nginx action completed for ${row.dataset.nginxName}.`);
                await refreshNginxPage();
            } catch (error) {
                setNginxOutput(`${row.dataset.nginxName}: ${button.dataset.action}`, error.message);
                showErrorToast(error.message);
            }
        });
    });
}

export async function initNginxPage() {
    const form = document.getElementById("nginxEditorForm");
    if (!form) {
        return;
    }

    nginxModal = bootstrap.Modal.getOrCreateInstance(document.getElementById("nginxEditorModal"));
    const message = document.getElementById("nginxEditorMessage");

    document.getElementById("openCreateNginxModalButton").addEventListener("click", () => openNginxModal());
    document.getElementById("refreshNginxButton").addEventListener("click", refreshNginxPage);

    form.querySelectorAll("input[name], textarea[name]").forEach((field) => {
        field.addEventListener("input", () => captureEditorDraft(form));
    });

    form.addEventListener("submit", async (event) => {
        event.preventDefault();
        message.textContent = "";
        const original = form.querySelector('[name="original_name"]').value;
        try {
            if (original) {
                await postParams(`/api/nginx/sites/${encodeURIComponent(original)}`, {
                    name: form.querySelector('[name="name"]').value,
                    filename: form.querySelector('[name="filename"]').value,
                    description: form.querySelector('[name="description"]').value,
                    content: form.querySelector('[name="content"]').value
                });
                nginxDrafts.delete(original);
                showSuccessToast(`Saved nginx site ${original}.`);
            } else {
                await postForm("/api/nginx/sites", form);
                showSuccessToast("Nginx site added.");
            }
            nginxModal.hide();
            await refreshNginxPage();
        } catch (error) {
            message.textContent = error.message;
            message.className = "small";
            showErrorToast(error.message);
        }
    });

    await refreshNginxPage();
}
