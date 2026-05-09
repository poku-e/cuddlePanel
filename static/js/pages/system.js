import {postForm, postParams, requestJson} from "../core/api.js";
import {escapeHtml} from "../core/dom.js";
import {showErrorToast, showSuccessToast} from "../core/toast.js";

function systemUserCardMarkup(user) {
    const canManage = document.getElementById("systemPageState")?.dataset.canManage === "1";
    return `
        <article class="system-card" data-system-username="${user.username}">
            <div class="service-card-head">
                <div>
                    <h3>${escapeHtml(user.username)}</h3>
                    <div class="system-meta">UID ${user.uid} | GID ${user.gid} | ${user.system_account ? "system" : "login"} account</div>
                </div>
                <div class="user-actions">
                    <button class="btn btn-outline-secondary btn-sm system-action-button" data-action="${user.locked ? "unlock" : "lock"}" type="button" ${canManage ? "" : "disabled"}>${user.locked ? "Unlock" : "Lock"}</button>
                    <button class="btn btn-outline-warning btn-sm system-action-button" data-action="${user.in_sudo ? "revoke-sudo" : "grant-sudo"}" type="button" ${(canManage && user.username !== "root") ? "" : "disabled"}>${user.in_sudo ? "Revoke sudo" : "Grant sudo"}</button>
                    <button class="btn btn-outline-primary btn-sm system-keys-button" type="button" ${(canManage && user.login_user) ? "" : "disabled"}>Edit keys</button>
                </div>
            </div>
            <div class="system-details-grid">
                <div><strong>Home</strong><div class="small text-secondary">${escapeHtml(user.home)}</div></div>
                <div><strong>Shell</strong><div class="small text-secondary">${escapeHtml(user.shell)}</div></div>
                <div><strong>Sudo</strong><div class="small text-secondary">${user.in_sudo ? "Yes" : "No"}</div></div>
                <div><strong>Locked</strong><div class="small text-secondary">${user.locked ? "Yes" : "No"}</div></div>
            </div>
            <div class="small system-status"></div>
        </article>
    `;
}

function renderSystemUsers(payload) {
    const meta = document.getElementById("systemAllowedRoots");
    meta.textContent = `Allowed path roots: ${payload.allowedRoots.join(", ")}`;

    const host = document.getElementById("systemUsersHost");
    if (!payload.users.length) {
        host.innerHTML = "<div class=\"text-secondary\">No host accounts found.</div>";
        return;
    }
    host.innerHTML = payload.users.map(systemUserCardMarkup).join("");
}

async function refreshSystemPage() {
    const message = document.getElementById("systemUsersMessage");
    try {
        const payload = await requestJson("/api/system/users");
        renderSystemUsers(payload);
        wireSystemCards();
        message.textContent = "";
    } catch (error) {
        message.textContent = error.message;
    }
}

async function loadAuthorizedKeys(username) {
    const usernameField = document.getElementById("authorizedKeysUsername");
    const contentField = document.getElementById("authorizedKeysContent");
    const message = document.getElementById("authorizedKeysMessage");
    usernameField.value = username;
    contentField.value = "";
    message.textContent = "Loading authorized_keys...";
    message.className = "small text-secondary";
    try {
        const payload = await requestJson(`/api/system/users/${encodeURIComponent(username)}/authorized-keys`);
        contentField.value = payload.content || "";
        message.textContent = payload.output;
        message.className = "small text-success";
        showSuccessToast(`Loaded authorized_keys for ${username}.`);
    } catch (error) {
        message.textContent = error.message;
        message.className = "small text-danger";
        showErrorToast(error.message);
    }
}

function wireSystemCards() {
    document.querySelectorAll(".system-action-button").forEach((button) => {
        button.addEventListener("click", async () => {
            const card = button.closest("[data-system-username]");
            const status = card.querySelector(".system-status");
            status.textContent = "";
            try {
                const payload = await postParams(`/api/system/users/${encodeURIComponent(card.dataset.systemUsername)}/action`, {
                    action: button.dataset.action
                });
                status.textContent = payload.output;
                status.className = "small system-status text-success";
                showSuccessToast(payload.output || "System user action completed.");
                await refreshSystemPage();
            } catch (error) {
                status.textContent = error.message;
                status.className = "small system-status text-danger";
                showErrorToast(error.message);
            }
        });
    });

    document.querySelectorAll(".system-keys-button").forEach((button) => {
        button.addEventListener("click", async () => {
            const card = button.closest("[data-system-username]");
            await loadAuthorizedKeys(card.dataset.systemUsername);
        });
    });
}

export async function initSystemPage() {
    const createForm = document.getElementById("systemCreateUserForm");
    if (!createForm) {
        return;
    }
    const canManage = document.getElementById("systemPageState")?.dataset.canManage === "1";
    const createMessage = document.getElementById("systemCreateMessage");
    const refreshButton = document.getElementById("refreshSystemUsersButton");
    const pathForm = document.getElementById("systemPathActionForm");
    const pathMessage = document.getElementById("systemPathMessage");
    const pathOutput = document.getElementById("systemPathOutput");
    const actionField = pathForm.querySelector('[name="action"]');
    const ownerField = pathForm.querySelector('[name="owner"]');
    const groupField = pathForm.querySelector('[name="group"]');
    const modeField = pathForm.querySelector('[name="mode"]');
    const usernameField = createForm.querySelector('[name="username"]');
    const homeField = createForm.querySelector('[name="home"]');
    const systemAccountField = createForm.querySelector('[name="system_account"]');
    const keysForm = document.getElementById("systemAuthorizedKeysForm");
    const keysUsernameField = document.getElementById("authorizedKeysUsername");
    const keysMessage = document.getElementById("authorizedKeysMessage");
    const loadKeysButton = document.getElementById("loadAuthorizedKeysButton");

    const syncPathFields = () => {
        const isChown = actionField.value === "chown";
        ownerField.toggleAttribute("required", isChown);
        ownerField.toggleAttribute("disabled", !isChown || !canManage);
        groupField.toggleAttribute("disabled", !isChown || !canManage);
        modeField.toggleAttribute("required", !isChown);
        modeField.toggleAttribute("disabled", isChown || !canManage);
    };

    const syncHomeField = () => {
        const base = systemAccountField.checked ? "/var/lib/" : "/home/";
        if (!usernameField.value.trim()) {
            homeField.value = base;
            return;
        }
        homeField.value = `${base}${usernameField.value.trim()}`;
    };

    createForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        createMessage.textContent = "";
        try {
            const payload = await postForm("/api/system/users", createForm);
            createMessage.textContent = payload.output;
            createMessage.className = "small text-success";
            showSuccessToast(payload.output || "System account created.");
            createForm.reset();
            homeField.value = "/home/";
            await refreshSystemPage();
        } catch (error) {
            createMessage.textContent = error.message;
            createMessage.className = "small text-danger";
            showErrorToast(error.message);
        }
    });

    pathForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        pathMessage.textContent = "";
        pathOutput.textContent = "Running path action...";
        try {
            const payload = await postForm("/api/system/path-action", pathForm);
            pathOutput.textContent = payload.output;
            pathMessage.textContent = "Path action completed.";
            pathMessage.className = "small text-success";
            showSuccessToast("Path action completed.");
        } catch (error) {
            pathOutput.textContent = error.message;
            pathMessage.textContent = "Path action failed.";
            pathMessage.className = "small text-danger";
            showErrorToast(error.message);
        }
    });

    keysForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        if (!keysUsernameField.value) {
            keysMessage.textContent = "Choose a login user first.";
            keysMessage.className = "small text-danger";
            return;
        }
        keysMessage.textContent = "";
        try {
            const payload = await postForm(`/api/system/users/${encodeURIComponent(keysUsernameField.value)}/authorized-keys`, keysForm);
            keysMessage.textContent = payload.output;
            keysMessage.className = "small text-success";
            showSuccessToast(payload.output || "authorized_keys saved.");
        } catch (error) {
            keysMessage.textContent = error.message;
            keysMessage.className = "small text-danger";
            showErrorToast(error.message);
        }
    });

    loadKeysButton.addEventListener("click", async () => {
        if (!keysUsernameField.value) {
            keysMessage.textContent = "Choose a login user first.";
            keysMessage.className = "small text-danger";
            showErrorToast("Choose a login user first.");
            return;
        }
        await loadAuthorizedKeys(keysUsernameField.value);
    });

    usernameField.addEventListener("input", syncHomeField);
    systemAccountField.addEventListener("change", syncHomeField);
    refreshButton.addEventListener("click", refreshSystemPage);
    actionField.addEventListener("change", syncPathFields);
    syncHomeField();
    syncPathFields();
    await refreshSystemPage();
}
