import {loadPartial} from "./core/dom.js";
import {showErrorToast} from "./core/toast.js";
import {initUsersPage} from "./pages/users.js";
import {initServicesPage} from "./pages/services.js";
import {initSystemPage} from "./pages/system.js";
import {initSystemUserPage} from "./pages/system_user.js";
import {initNginxPage} from "./pages/nginx.js";
import {initDeployPage} from "./pages/deploy.js";
import {initCodexPage} from "./pages/codex.js";
import {initTerminalPage, setTerminalReloader} from "./pages/terminal.js";

const pageInitializers = new Map([
    ["users", initUsersPage],
    ["services", initServicesPage],
    ["system", initSystemPage],
    ["nginx", initNginxPage],
    ["codex", initCodexPage],
    ["deploy", initDeployPage],
    ["terminal", initTerminalPage]
]);

const dashboardPageStorageKey = "cuddlepanel.activePage";
let suppressNextHashLoad = false;

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

function normalizePage(page) {
    if (typeof page !== "string" || !page) {
        return "dashboard";
    }
    const systemUsername = systemUserFromPage(page);
    if (systemUsername) {
        return systemUserPageKey(systemUsername);
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

export async function loadPage(page) {
    const normalizedPage = normalizePage(page);
    const systemUsername = systemUserFromPage(normalizedPage);
    const content = document.getElementById("content");
    content.innerHTML = "<div>Loading...</div>";
    const response = await fetch(systemUsername
        ? `/api/system/users/${encodeURIComponent(systemUsername)}/page`
        : `/api/page/${encodeURIComponent(normalizedPage)}`);
    if (response.status === 401) {
        window.location.assign("/login");
        return;
    }
    content.innerHTML = response.ok
        ? await response.text()
        : "<div class=\"alert alert-danger\">Unable to load this page.</div>";
    if (!response.ok) {
        showErrorToast("Unable to load this page.");
    }

    if (response.ok) {
        if (systemUsername) {
            await initSystemUserPage(systemUsername);
        } else {
            const initializer = pageInitializers.get(normalizedPage);
            if (initializer) {
                await initializer();
            }
        }
        persistCurrentPage(normalizedPage);
    }

    document.querySelectorAll("[data-page]").forEach((button) => {
        const activePage = systemUsername ? "system" : normalizedPage;
        button.classList.toggle("active", button.dataset.page === activePage);
    });

    document.getElementById("sidebar").classList.remove("open");
}

export async function bootDashboard() {
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
}
