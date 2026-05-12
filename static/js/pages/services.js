import {postParams, requestJson} from "../core/api.js";
import {escapeHtml} from "../core/dom.js";
import {showErrorToast, showSuccessToast} from "../core/toast.js";

let servicesCache = [];

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

async function runServiceDetailAction(unit, action) {
    const payload = await postParams(`/api/services/${encodeURIComponent(unit)}/action`, {action});
    setDetailText("serviceDetailRuntimeMeta", `${unit}: ${action}`);
    setDetailText("serviceDetailRuntimeOutput", payload.output || "No runtime output.");
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
        } catch (error) {
            showErrorToast(error.message);
            throw error;
        }
    };

    document.getElementById("serviceDetailBackButton").addEventListener("click", () => {
        window.location.hash = "page=services";
    });
    document.getElementById("serviceDetailRefreshButton").addEventListener("click", refresh);
    document.querySelectorAll(".service-detail-action-button").forEach((button) => {
        button.disabled = !canManageDetail();
        button.addEventListener("click", async () => {
            try {
                await runServiceDetailAction(unit, button.dataset.action);
            } catch (error) {
                setDetailText("serviceDetailRuntimeMeta", `${unit}: ${button.dataset.action}`);
                setDetailText("serviceDetailRuntimeOutput", error.message);
                showErrorToast(error.message);
            }
        });
    });

    await refresh();
}
