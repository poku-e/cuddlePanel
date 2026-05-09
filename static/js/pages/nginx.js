import {postForm, postParams, requestJson} from "../core/api.js";
import {escapeHtml} from "../core/dom.js";
import {showErrorToast, showSuccessToast} from "../core/toast.js";

function nginxCardMarkup(site) {
    const canManage = document.getElementById("nginxPageState")?.dataset.canManage === "1";
    return `
        <article class="nginx-card" data-nginx-name="${site.name}">
            <div class="service-card-head">
                <div>
                    <h3>${escapeHtml(site.name)}</h3>
                    <div class="${site.enabled ? "service-state is-active" : "service-state is-inactive"}">${site.enabled ? "enabled" : "disabled"}</div>
                </div>
                <div class="user-actions">
                    <button class="btn btn-outline-success btn-sm nginx-action-button" data-action="enable" type="button" ${(canManage && !site.enabled) ? "" : "disabled"}>Enable</button>
                    <button class="btn btn-outline-secondary btn-sm nginx-action-button" data-action="disable" type="button" ${(canManage && site.enabled) ? "" : "disabled"}>Disable</button>
                    <button class="btn btn-outline-warning btn-sm nginx-action-button" data-action="test" type="button" ${canManage ? "" : "disabled"}>Test</button>
                    <button class="btn btn-outline-primary btn-sm nginx-action-button" data-action="reload" type="button" ${canManage ? "" : "disabled"}>Reload</button>
                </div>
            </div>
            <div class="service-grid">
                <label class="cp-label">Panel name
                    <input class="cp-input" name="name" value="${escapeHtml(site.name)}" ${canManage ? "" : "disabled"}>
                </label>
                <label class="cp-label">Filename
                    <input class="cp-input" name="filename" value="${escapeHtml(site.filename)}" ${canManage ? "" : "disabled"}>
                </label>
            </div>
            <label class="cp-label mt-3">Description
                <input class="cp-input" name="description" value="${escapeHtml(site.description)}" ${canManage ? "" : "disabled"}>
            </label>
            <label class="cp-label mt-3">Config
                <textarea class="cp-input cp-textarea cp-codearea" name="content" ${canManage ? "" : "disabled"}>${escapeHtml(site.content)}</textarea>
            </label>
            <div class="service-actions-row">
                <button class="btn btn-outline-primary btn-sm save-nginx-button" type="button" ${canManage ? "" : "disabled"}>Save site</button>
            </div>
            <div class="small nginx-status"></div>
        </article>
    `;
}

function renderNginxSites(payload) {
    const meta = document.getElementById("nginxMeta");
    meta.textContent = `Available: ${payload.availableDir} | Enabled: ${payload.enabledDir}`;

    const host = document.getElementById("nginxSitesHost");
    if (!payload.sites.length) {
        host.innerHTML = "<div class=\"text-secondary\">No allowlisted nginx sites yet.</div>";
        return;
    }
    host.innerHTML = payload.sites.map(nginxCardMarkup).join("");
}

async function refreshNginxPage() {
    const message = document.getElementById("nginxMessage");
    try {
        const payload = await requestJson("/api/nginx/sites");
        renderNginxSites(payload);
        wireNginxCards();
        message.textContent = "";
    } catch (error) {
        message.textContent = error.message;
    }
}

function wireNginxCards() {
    document.querySelectorAll(".save-nginx-button").forEach((button) => {
        button.addEventListener("click", async () => {
            const card = button.closest("[data-nginx-name]");
            const status = card.querySelector(".nginx-status");
            status.textContent = "";
            try {
                await postParams(`/api/nginx/sites/${encodeURIComponent(card.dataset.nginxName)}`, {
                    name: card.querySelector('[name="name"]').value,
                    filename: card.querySelector('[name="filename"]').value,
                    description: card.querySelector('[name="description"]').value,
                    content: card.querySelector('[name="content"]').value
                });
                status.textContent = "Nginx site saved.";
                status.className = "small nginx-status text-success";
                showSuccessToast(`Saved nginx site ${card.dataset.nginxName}.`);
                await refreshNginxPage();
            } catch (error) {
                status.textContent = error.message;
                status.className = "small nginx-status text-danger";
                showErrorToast(error.message);
            }
        });
    });

    document.querySelectorAll(".nginx-action-button").forEach((button) => {
        button.addEventListener("click", async () => {
            const card = button.closest("[data-nginx-name]");
            const status = card.querySelector(".nginx-status");
            status.textContent = "";
            try {
                const payload = await postParams(`/api/nginx/sites/${encodeURIComponent(card.dataset.nginxName)}/action`, {
                    action: button.dataset.action
                });
                status.textContent = payload.output;
                status.className = "small nginx-status text-success";
                showSuccessToast(payload.output || `Nginx action completed for ${card.dataset.nginxName}.`);
                await refreshNginxPage();
            } catch (error) {
                status.textContent = error.message;
                status.className = "small nginx-status text-danger";
                showErrorToast(error.message);
            }
        });
    });
}

export async function initNginxPage() {
    const createForm = document.getElementById("nginxCreateForm");
    if (!createForm) {
        return;
    }
    const createMessage = document.getElementById("nginxCreateMessage");
    const refreshButton = document.getElementById("refreshNginxButton");

    createForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        createMessage.textContent = "";
        try {
            await postForm("/api/nginx/sites", createForm);
            createMessage.textContent = "Nginx site added.";
            createMessage.className = "small text-success";
            showSuccessToast("Nginx site added.");
            createForm.reset();
            await refreshNginxPage();
        } catch (error) {
            createMessage.textContent = error.message;
            createMessage.className = "small text-danger";
            showErrorToast(error.message);
        }
    });

    refreshButton.addEventListener("click", refreshNginxPage);
    await refreshNginxPage();
}
