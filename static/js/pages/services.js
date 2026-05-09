import {postForm, postParams, requestJson} from "../core/api.js";
import {escapeHtml} from "../core/dom.js";
import {showErrorToast, showSuccessToast} from "../core/toast.js";

function serviceCardMarkup(service) {
    const canManage = document.getElementById("servicesPageState")?.dataset.canManage === "1";
    const stateClass = service.state === "active"
        ? "service-state is-active"
        : service.state === "inactive"
            ? "service-state is-inactive"
            : "service-state";

    return `
        <article class="service-card" data-service-name="${service.name}">
            <div class="service-card-head">
                <div>
                    <h3>${escapeHtml(service.name)}</h3>
                    <div class="${stateClass}">${escapeHtml(service.state)}</div>
                </div>
                <div class="user-actions">
                    <button class="btn btn-outline-success btn-sm service-action-button" data-action="start" type="button" ${canManage ? "" : "disabled"}>Start</button>
                    <button class="btn btn-outline-warning btn-sm service-action-button" data-action="restart" type="button" ${canManage ? "" : "disabled"}>Restart</button>
                    <button class="btn btn-outline-secondary btn-sm service-action-button" data-action="stop" type="button" ${canManage ? "" : "disabled"}>Stop</button>
                </div>
            </div>
            <div class="service-grid">
                <label class="cp-label">Panel name
                    <input class="cp-input" name="name" value="${escapeHtml(service.name)}" ${canManage ? "" : "disabled"}>
                </label>
                <label class="cp-label">Systemd unit
                    <input class="cp-input" name="unit" value="${escapeHtml(service.unit)}" ${canManage ? "" : "disabled"}>
                </label>
            </div>
            <label class="cp-label mt-3">Description
                <textarea class="cp-input cp-textarea" name="description" ${canManage ? "" : "disabled"}>${escapeHtml(service.description)}</textarea>
            </label>
            <div class="service-actions-row">
                <button class="btn btn-outline-primary btn-sm save-service-button" type="button" ${canManage ? "" : "disabled"}>Save details</button>
            </div>
            <pre class="service-output">${escapeHtml(service.detail || "No runtime output yet.")}</pre>
            <div class="small service-status"></div>
        </article>
    `;
}

function renderServices(services) {
    const host = document.getElementById("servicesTableHost");
    if (!services.length) {
        host.innerHTML = "<div class=\"text-secondary\">No allowlisted services yet.</div>";
        return;
    }
    host.innerHTML = services.map(serviceCardMarkup).join("");
}

async function refreshServicesPage() {
    const message = document.getElementById("servicesMessage");
    try {
        const payload = await requestJson("/api/services");
        renderServices(payload.services);
        wireServiceCards();
        message.textContent = "";
    } catch (error) {
        message.textContent = error.message;
    }
}

function wireServiceCards() {
    document.querySelectorAll(".save-service-button").forEach((button) => {
        button.addEventListener("click", async () => {
            const card = button.closest("[data-service-name]");
            const status = card.querySelector(".service-status");
            status.textContent = "";
            try {
                await postParams(`/api/services/${encodeURIComponent(card.dataset.serviceName)}`, {
                    name: card.querySelector('[name="name"]').value,
                    unit: card.querySelector('[name="unit"]').value,
                    description: card.querySelector('[name="description"]').value
                });
                status.textContent = "Service details saved.";
                status.className = "small service-status text-success";
                showSuccessToast(`Saved ${card.dataset.serviceName}.`);
                await refreshServicesPage();
            } catch (error) {
                status.textContent = error.message;
                status.className = "small service-status text-danger";
                showErrorToast(error.message);
            }
        });
    });

    document.querySelectorAll(".service-action-button").forEach((button) => {
        button.addEventListener("click", async () => {
            const card = button.closest("[data-service-name]");
            const status = card.querySelector(".service-status");
            const output = card.querySelector(".service-output");
            status.textContent = "";
            try {
                const payload = await postParams(`/api/services/${encodeURIComponent(card.dataset.serviceName)}/action`, {
                    action: button.dataset.action
                });
                output.textContent = payload.output;
                status.textContent = `${button.dataset.action} completed.`;
                status.className = "small service-status text-success";
                showSuccessToast(`Service ${button.dataset.action} completed for ${card.dataset.serviceName}.`);
                await refreshServicesPage();
            } catch (error) {
                status.textContent = error.message;
                status.className = "small service-status text-danger";
                showErrorToast(error.message);
            }
        });
    });
}

export async function initServicesPage() {
    const createForm = document.getElementById("serviceCreateForm");
    if (!createForm) {
        return;
    }
    const createMessage = document.getElementById("serviceCreateMessage");
    const refreshButton = document.getElementById("refreshServicesButton");

    createForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        createMessage.textContent = "";
        try {
            await postForm("/api/services", createForm);
            createMessage.textContent = "Service added.";
            createMessage.className = "small text-success";
            showSuccessToast("Service added.");
            createForm.reset();
            await refreshServicesPage();
        } catch (error) {
            createMessage.textContent = error.message;
            createMessage.className = "small text-danger";
            showErrorToast(error.message);
        }
    });

    refreshButton.addEventListener("click", refreshServicesPage);
    await refreshServicesPage();
}
