import {postForm, postParams, requestJson} from "../core/api.js";
import {escapeHtml} from "../core/dom.js";
import {showErrorToast, showSuccessToast} from "../core/toast.js";

let servicesCache = [];
let serviceModal = null;

function canManage() {
    return document.getElementById("servicesPageState")?.dataset.canManage === "1";
}

function stateBadge(state) {
    if (state === "active") {
        return '<span class="badge text-bg-success">active</span>';
    }
    if (state === "inactive") {
        return '<span class="badge text-bg-secondary">inactive</span>';
    }
    return `<span class="badge text-bg-light">${escapeHtml(state)}</span>`;
}

function renderServices(services) {
    const host = document.getElementById("servicesTableHost");
    if (!services.length) {
        host.innerHTML = '<tr><td colspan="5" class="text-secondary">No allowlisted services yet.</td></tr>';
        return;
    }

    host.innerHTML = services.map((service) => `
        <tr data-service-name="${escapeHtml(service.name)}">
            <td class="fw-semibold">${escapeHtml(service.name)}</td>
            <td><code>${escapeHtml(service.unit)}</code></td>
            <td>${stateBadge(service.state)}</td>
            <td class="small text-secondary">${escapeHtml(service.description || "No description")}</td>
            <td class="text-end">
                <div class="btn-toolbar justify-content-end gap-1">
                    <div class="btn-group btn-group-sm">
                        <button class="btn btn-outline-success service-action-button" data-action="start" type="button" ${canManage() ? "" : "disabled"}>Start</button>
                        <button class="btn btn-outline-warning service-action-button" data-action="restart" type="button" ${canManage() ? "" : "disabled"}>Restart</button>
                        <button class="btn btn-outline-secondary service-action-button" data-action="stop" type="button" ${canManage() ? "" : "disabled"}>Stop</button>
                    </div>
                    <button class="btn btn-outline-primary btn-sm edit-service-button" type="button" ${canManage() ? "" : "disabled"}>Edit</button>
                </div>
            </td>
        </tr>
    `).join("");
}

function setServiceOutput(title, output) {
    document.getElementById("servicesOutputMeta").textContent = title;
    document.getElementById("servicesOutput").textContent = output || "No runtime output yet.";
}

async function refreshServicesPage() {
    const message = document.getElementById("servicesMessage");
    try {
        const payload = await requestJson("/api/services");
        servicesCache = payload.services;
        renderServices(payload.services);
        wireServiceRows();
        message.textContent = "";
    } catch (error) {
        message.textContent = error.message;
    }
}

function openServiceModal(service = null) {
    const form = document.getElementById("serviceEditorForm");
    const message = document.getElementById("serviceEditorMessage");
    form.reset();
    message.textContent = "";
    form.querySelector('[name="original_name"]').value = service?.name || "";
    form.querySelector('[name="name"]').value = service?.name || "";
    form.querySelector('[name="unit"]').value = service?.unit || "";
    form.querySelector('[name="description"]').value = service?.description || "";
    document.getElementById("serviceEditorModalTitle").textContent = service ? `Edit service: ${service.name}` : "Register service";
    document.getElementById("serviceEditorModalMeta").textContent = service
        ? "Update the allowlisted name, unit, or description."
        : "Create a new systemd service entry for the dashboard.";
    serviceModal.show();
}

function wireServiceRows() {
    document.querySelectorAll(".edit-service-button").forEach((button) => {
        button.addEventListener("click", () => {
            const row = button.closest("[data-service-name]");
            openServiceModal(servicesCache.find((service) => service.name === row.dataset.serviceName) || null);
        });
    });

    document.querySelectorAll(".service-action-button").forEach((button) => {
        button.addEventListener("click", async () => {
            const row = button.closest("[data-service-name]");
            try {
                const payload = await postParams(`/api/services/${encodeURIComponent(row.dataset.serviceName)}/action`, {
                    action: button.dataset.action
                });
                setServiceOutput(`${row.dataset.serviceName}: ${button.dataset.action}`, payload.output);
                showSuccessToast(`Service ${button.dataset.action} completed for ${row.dataset.serviceName}.`);
                await refreshServicesPage();
            } catch (error) {
                setServiceOutput(`${row.dataset.serviceName}: ${button.dataset.action}`, error.message);
                showErrorToast(error.message);
            }
        });
    });
}

export async function initServicesPage() {
    const form = document.getElementById("serviceEditorForm");
    if (!form) {
        return;
    }

    serviceModal = bootstrap.Modal.getOrCreateInstance(document.getElementById("serviceEditorModal"));
    const message = document.getElementById("serviceEditorMessage");

    document.getElementById("openCreateServiceModalButton").addEventListener("click", () => openServiceModal());
    document.getElementById("refreshServicesButton").addEventListener("click", refreshServicesPage);

    form.addEventListener("submit", async (event) => {
        event.preventDefault();
        message.textContent = "";
        const original = form.querySelector('[name="original_name"]').value;
        try {
            if (original) {
                await postParams(`/api/services/${encodeURIComponent(original)}`, {
                    name: form.querySelector('[name="name"]').value,
                    unit: form.querySelector('[name="unit"]').value,
                    description: form.querySelector('[name="description"]').value
                });
                showSuccessToast(`Saved ${original}.`);
            } else {
                await postForm("/api/services", form);
                showSuccessToast("Service added.");
            }
            serviceModal.hide();
            await refreshServicesPage();
        } catch (error) {
            message.textContent = error.message;
            message.className = "small text-danger";
            showErrorToast(error.message);
        }
    });

    await refreshServicesPage();
}
