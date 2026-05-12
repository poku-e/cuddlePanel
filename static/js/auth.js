import {postForm, requestJson} from "./core/api.js";
import {escapeHtml} from "./core/dom.js";
import {showErrorToast, showSuccessToast} from "./core/toast.js";

function currentDashboardHash() {
    return window.location.hash.startsWith("#page=") ? window.location.hash : "";
}

function scrubLoginUrl() {
    if (document.body?.dataset.view !== "login") {
        return;
    }
    const url = new URL(window.location.href);
    let changed = false;
    ["username", "password"].forEach((key) => {
        if (url.searchParams.has(key)) {
            url.searchParams.delete(key);
            changed = true;
        }
    });
    if (!changed) {
        return;
    }
    const next = `${url.pathname}${url.search}${currentDashboardHash()}`;
    window.history.replaceState({}, "", next);
}

function postAuthDestination(nextPath) {
    const hash = currentDashboardHash();
    if (!hash) {
        return nextPath;
    }
    if (nextPath === "/dashboard") {
        return `/dashboard${hash}`;
    }
    if (nextPath === "/2fa/setup") {
        try {
            window.sessionStorage.setItem("cp_post_auth_hash", hash);
        } catch (error) {
            // Best-effort only; login flow still works without preserved page state.
        }
    }
    return nextPath;
}

export function bindAuthForm(formId, url, nextPath) {
    const form = document.getElementById(formId);
    if (!form) {
        return;
    }
    scrubLoginUrl();
    const message = document.getElementById("message");
    form.addEventListener("submit", async (event) => {
        event.preventDefault();
        message.textContent = "";
        try {
            const payload = await postForm(url, form);
            window.location.assign(postAuthDestination(payload.next || nextPath));
        } catch (error) {
            message.textContent = error.message;
            showErrorToast(error.message);
        }
    });
}

function dependencyMarkup(dependency) {
    return `
        <article class="setup-dependency ${dependency.present ? "is-present" : "is-missing"}">
            <div class="setup-dependency-name">${escapeHtml(dependency.name)}</div>
            <div class="small text-secondary">${escapeHtml(dependency.path || dependency.details)}</div>
            <div class="small ${dependency.present ? "text-success" : dependency.required ? "text-danger" : "text-warning"}">${escapeHtml(dependency.details)}</div>
        </article>
    `;
}

export async function bootOnboardingSetup() {
    const form = document.getElementById("onboardingForm");
    const message = document.getElementById("message");
    const dependencyList = document.getElementById("setupDependencyList");
    const dependencySummary = document.getElementById("setupDependencySummary");
    if (!form || !message || !dependencyList || !dependencySummary) {
        return;
    }

    try {
        const payload = await requestJson("/api/setup/status");
        for (const [key, value] of Object.entries(payload.config)) {
            const field = form.querySelector(`[name="${key}"]`);
            if (!field) {
                continue;
            }
            if (field.tagName === "SELECT") {
                field.value = value ? "1" : "0";
            } else {
                field.value = value;
            }
        }
        dependencyList.innerHTML = payload.dependencies.map(dependencyMarkup).join("");
        const missingRequired = payload.dependencies.filter((dependency) => dependency.required && !dependency.present).length;
        const missingOptional = payload.dependencies.filter((dependency) => !dependency.required && !dependency.present).length;
        dependencySummary.textContent = missingRequired > 0
            ? `${missingRequired} required dependency issues`
            : missingOptional > 0
                ? `${missingOptional} optional dependency warnings`
                : "All checked dependencies are available";
    } catch (error) {
        dependencySummary.textContent = "Unable to load setup status";
        message.textContent = error.message;
        showErrorToast(error.message);
    }

    form.addEventListener("submit", async (event) => {
        event.preventDefault();
        message.textContent = "";
        try {
            const payload = await postForm("/api/onboarding", form);
            showSuccessToast("First-run setup completed.");
            window.location.assign(payload.next || "/2fa/setup");
        } catch (error) {
            message.textContent = error.message;
            showErrorToast(error.message);
        }
    });
}

export async function bootTotpSetup() {
    const secret = document.getElementById("totpSecret");
    const uri = document.getElementById("totpUri");
    const form = document.getElementById("totpSetupForm");
    const message = document.getElementById("message");
    if (!secret || !uri || !form || !message) {
        return;
    }

    try {
        const payload = await requestJson("/api/2fa/setup-info");
        secret.textContent = payload.secret;
        uri.textContent = payload.uri;
    } catch (error) {
        message.textContent = error.message;
        showErrorToast(error.message);
        return;
    }

    form.addEventListener("submit", async (event) => {
        event.preventDefault();
        message.textContent = "";
        try {
            const payload = await postForm("/api/2fa/confirm", form);
            let next = payload.next || "/dashboard";
            if (next === "/dashboard") {
                try {
                    const pendingHash = window.sessionStorage.getItem("cp_post_auth_hash") || "";
                    if (pendingHash.startsWith("#page=")) {
                        next = `/dashboard${pendingHash}`;
                    }
                    window.sessionStorage.removeItem("cp_post_auth_hash");
                } catch (error) {
                    // Ignore storage failures and continue with the safe default destination.
                }
            }
            window.location.assign(next);
        } catch (error) {
            message.textContent = error.message;
            showErrorToast(error.message);
        }
    });
}
