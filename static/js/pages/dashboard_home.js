import {postParams, requestJson} from "../core/api.js";
import {showErrorToast, showSuccessToast} from "../core/toast.js";

function severityLabel(severity) {
    if (severity === "red") {
        return "Red";
    }
    if (severity === "yellow") {
        return "Yellow";
    }
    return "Green";
}

function renderHealthCards(items) {
    const host = document.getElementById("dashboardHealthCards");
    const template = document.getElementById("dashboardHealthCardTemplate");
    const noAttention = document.getElementById("dashboardNoAttention");
    host.innerHTML = "";

    if (!items.length) {
        noAttention.hidden = false;
        return;
    }
    noAttention.hidden = true;

    for (const item of items) {
        const fragment = template.content.cloneNode(true);
        const card = fragment.querySelector(".cp-health-card");
        const title = fragment.querySelector("h3");
        const detail = fragment.querySelector("p");
        const badge = fragment.querySelector(".cp-health-card-badge");
        const autoFixButton = fragment.querySelector('[data-action="auto-fix"]');
        const guidedRepairButton = fragment.querySelector('[data-action="guided-repair"]');

        card.dataset.severity = item.severity;
        card.dataset.id = item.id;
        title.textContent = item.title;
        detail.textContent = item.detail;
        badge.textContent = severityLabel(item.severity);

        autoFixButton.disabled = !item.canAutoFix;
        guidedRepairButton.disabled = false;
        host.appendChild(fragment);
    }
}

async function loadDashboardHealth() {
    const message = document.getElementById("dashboardHealthMessage");
    try {
        const payload = await requestJson("/api/dashboard/health");
        renderHealthCards(payload.items || []);
        message.textContent = "";
    } catch (error) {
        message.textContent = error.message || "Unable to load health status.";
        showErrorToast(error.message || "Unable to load health status.");
    }
}

async function runAutoFix(button) {
    button.disabled = true;
    try {
        const payload = await postParams("/api/dashboard/health/nginx/auto-fix", {});
        showSuccessToast(payload.output || "Nginx auto-fix completed.");
        await loadDashboardHealth();
    } catch (error) {
        showErrorToast(error.message || "Auto-fix failed.");
    } finally {
        button.disabled = false;
    }
}

function openGuidedRepair() {
    window.location.hash = `page=${encodeURIComponent("nginx")}`;
}

function wireActions() {
    const host = document.getElementById("dashboardHealthCards");
    host.addEventListener("click", async (event) => {
        const button = event.target.closest("button[data-action]");
        if (!button) {
            return;
        }
        if (button.dataset.action === "auto-fix") {
            await runAutoFix(button);
            return;
        }
        if (button.dataset.action === "guided-repair") {
            openGuidedRepair();
        }
    });
}

export async function initDashboardPage() {
    const refreshButton = document.getElementById("refreshDashboardHealthButton");
    if (!refreshButton) {
        return;
    }
    refreshButton.addEventListener("click", loadDashboardHealth);
    wireActions();
    await loadDashboardHealth();
}