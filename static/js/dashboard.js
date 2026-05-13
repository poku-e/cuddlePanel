import {loadPartial} from "./core/dom.js";
import {showErrorToast} from "./core/toast.js";
import {initUsersPage} from "./pages/users.js";
import {initServiceDetailPage, initServicesPage} from "./pages/services.js";
import {initSystemPage} from "./pages/system.js";
import {initSystemUserPage} from "./pages/system_user.js";
import {initNginxPage} from "./pages/nginx.js";
import {initFail2banPage} from "./pages/fail2ban.js";
import {initDeployPage} from "./pages/deploy.js";
import {initCodexPage} from "./pages/codex.js";
import {initTerminalPage, setTerminalReloader} from "./pages/terminal.js";
import {initDashboardPage} from "./pages/dashboard_home.js";

const pageInitializers = new Map([
    ["dashboard", initDashboardPage],
    ["users", initUsersPage],
    ["services", initServicesPage],
    ["system", initSystemPage],
    ["nginx", initNginxPage],
    ["fail2ban", initFail2banPage],
    ["codex", initCodexPage],
    ["deploy", initDeployPage],
    ["terminal", initTerminalPage]
]);

const dashboardPageStorageKey = "cuddlepanel.activePage";
let suppressNextHashLoad = false;
let activeDashboardLoads = 0;

function setDashboardLoading(loading, message = "Loading page...") {
    const overlay = document.getElementById("dashboardLoadingOverlay");
    const messageHost = document.getElementById("dashboardLoadingMessage");
    if (!overlay || !messageHost) {
        return;
    }
    if (loading) {
        activeDashboardLoads += 1;
        messageHost.textContent = message;
        overlay.hidden = false;
        overlay.setAttribute("aria-hidden", "false");
        return;
    }
    activeDashboardLoads = Math.max(0, activeDashboardLoads - 1);
    if (activeDashboardLoads > 0) {
        return;
    }
    overlay.hidden = true;
    overlay.setAttribute("aria-hidden", "true");
}

function validSystemUsername(username) {
    return /^[A-Za-z_][A-Za-z0-9._-]{0,63}$/.test(username);
}

function systemUserPageKey(username) {
    return `system-user:${username}`;
}

function systemUserFromPage(page) {
    if (!page.startsWith("system-user:")) {
        return null;
    }
    const username = page.slice("system-user:".length);
    return validSystemUsername(username) ? username : null;
}

function servicePageKey(unit) {
    return `service:${unit}`;
}

function serviceUnitFromPage(page) {
    if (!page.startsWith("service:")) {
        return null;
    }
    const unit = page.slice("service:".length);
    return /^[A-Za-z0-9._@-]{1,128}\.service$/.test(unit) ? unit : null;
}

function normalizePage(page) {
    if (typeof page !== "string" || !page) {
        return "dashboard";
    }
    const systemUsername = systemUserFromPage(page);
    if (systemUsername) {
        return systemUserPageKey(systemUsername);
    }
    const serviceUnit = serviceUnitFromPage(page);
    if (serviceUnit) {
        return servicePageKey(serviceUnit);
    }
    return pageInitializers.has(page) || page === "dashboard" ? page : "dashboard";
}

function currentPageFromLocation() {
    const hash = window.location.hash.startsWith("#") ? window.location.hash.slice(1) : "";
    if (hash.startsWith("page=")) {
        return normalizePage(decodeURIComponent(hash.slice(5)));
    }
    return null;
}

function persistCurrentPage(page) {
    const normalized = normalizePage(page);
    window.localStorage.setItem(dashboardPageStorageKey, normalized);
    const nextHash = `page=${encodeURIComponent(normalized)}`;
    if (window.location.hash.slice(1) !== nextHash) {
        suppressNextHashLoad = true;
        window.location.hash = nextHash;
    }
}

function extractFailureMessage(rawText, fallbackMessage) {
    const trimmed = rawText.trim();
    if (!trimmed) {
        return fallbackMessage;
    }
    try {
        const payload = JSON.parse(trimmed);
        return payload.error || payload.output || fallbackMessage;
    } catch {
        return fallbackMessage;
    }
}

export async function loadPage(page) {
    const normalizedPage = normalizePage(page);
    const systemUsername = systemUserFromPage(normalizedPage);
    const serviceUnit = serviceUnitFromPage(normalizedPage);
    const content = document.getElementById("content");
    setDashboardLoading(true, "Loading page...");
    try {
        content.innerHTML = "";
        let response;
        let failureMessage = "Unable to load this page.";
        if (systemUsername) {
            const candidateUrls = [
                `/api/system/users/${encodeURIComponent(systemUsername)}/page`,
                `/api/page/system-user/${encodeURIComponent(systemUsername)}`
            ];
            for (const url of candidateUrls) {
                response = await fetch(url);
                if (response.ok || response.status === 401) {
                    break;
                }
                try {
                    const payload = await response.clone().json();
                    failureMessage = payload.error || payload.output || failureMessage;
                } catch {
                    const text = await response.clone().text();
                    failureMessage = extractFailureMessage(text, failureMessage);
                }
            }
        } else if (serviceUnit) {
            const candidateUrls = [
                `/api/services/${encodeURIComponent(serviceUnit)}/page`,
                `/api/page/service/${encodeURIComponent(serviceUnit)}`
            ];
            for (const url of candidateUrls) {
                response = await fetch(url);
                if (response.ok || response.status === 401) {
                    break;
                }
                try {
                    const payload = await response.clone().json();
                    failureMessage = payload.error || payload.output || failureMessage;
                } catch {
                    const text = await response.clone().text();
                    failureMessage = extractFailureMessage(text, failureMessage);
                }
            }
        } else {
            response = await fetch(`/api/page/${encodeURIComponent(normalizedPage)}`);
            if (!response.ok && response.status !== 401) {
                try {
                    const payload = await response.clone().json();
                    failureMessage = payload.error || payload.output || failureMessage;
                } catch {
                    const text = await response.clone().text();
                    failureMessage = extractFailureMessage(text, failureMessage);
                }
            }
        }
        if (response.status === 401) {
            window.location.assign("/login");
            return;
        }
        if (!response.ok && response.status === 404) {
            if (serviceUnit) {
                showErrorToast(`Service ${serviceUnit} was not found. Opening the Services list instead.`);
                await loadPage("services");
                return;
            }
            if (systemUsername) {
                showErrorToast(`System user ${systemUsername} was not found. Opening the System page instead.`);
                await loadPage("system");
                return;
            }
        }
        content.innerHTML = response.ok
            ? await response.text()
            : `<div class="alert alert-danger">${failureMessage}</div>`;
        if (!response.ok) {
            showErrorToast(failureMessage);
        }

        if (response.ok) {
            try {
                if (systemUsername) {
                    await initSystemUserPage(systemUsername);
                } else if (serviceUnit) {
                    await initServiceDetailPage(serviceUnit);
                } else {
                    const initializer = pageInitializers.get(normalizedPage);
                    if (initializer) {
                        await initializer();
                    }
                }
            } catch (error) {
                content.innerHTML = `<div class="alert alert-danger">${error.message || "Unable to initialize this page."}</div>`;
                showErrorToast(error.message || "Unable to initialize this page.");
                return;
            }
            persistCurrentPage(normalizedPage);
        }

        document.querySelectorAll("[data-page]").forEach((button) => {
            const activePage = systemUsername ? "system" : (serviceUnit ? "services" : normalizedPage);
            button.classList.toggle("active", button.dataset.page === activePage);
        });

        document.getElementById("sidebar").classList.remove("open");
    } finally {
        setDashboardLoading(false);
    }
}

export async function bootDashboard() {
    setDashboardLoading(true, "Loading dashboard...");
    try {
        await Promise.all([
            loadPartial("sidebar", "/partials/sidebar"),
            loadPartial("header", "/partials/header"),
            loadPartial("footer", "/partials/footer")
        ]);

        setTerminalReloader(loadPage);

        document.querySelectorAll("[data-page]").forEach((button) => {
            button.addEventListener("click", () => loadPage(button.dataset.page));
        });

        document.getElementById("logoutButton").addEventListener("click", async () => {
            await fetch("/api/logout", {method: "POST"});
            window.location.assign("/login");
        });

        document.getElementById("menuToggle").addEventListener("click", () => {
            document.getElementById("sidebar").classList.toggle("open");
        });

        window.addEventListener("hashchange", async () => {
            if (suppressNextHashLoad) {
                suppressNextHashLoad = false;
                return;
            }
            const page = currentPageFromLocation();
            if (page) {
                await loadPage(page);
            }
        });

        const initialPage = currentPageFromLocation() ||
            normalizePage(window.localStorage.getItem(dashboardPageStorageKey) || "dashboard");
        await loadPage(initialPage);
    } finally {
        setDashboardLoading(false);
    }
}
