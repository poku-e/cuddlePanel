import {loadPartial} from "./core/dom.js";
import {showErrorToast} from "./core/toast.js";
import {initUsersPage} from "./pages/users.js";
import {initServicesPage} from "./pages/services.js";
import {initSystemPage} from "./pages/system.js";
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

export async function loadPage(page) {
    const content = document.getElementById("content");
    content.innerHTML = "<div class=\"text-secondary\">Loading...</div>";
    const response = await fetch(`/api/page/${encodeURIComponent(page)}`);
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
        const initializer = pageInitializers.get(page);
        if (initializer) {
            await initializer();
        }
    }

    document.querySelectorAll("[data-page]").forEach((button) => {
        button.classList.toggle("active", button.dataset.page === page);
    });
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

    await loadPage("dashboard");
}
