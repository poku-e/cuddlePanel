import {postForm, postParams, requestJson} from "../core/api.js";
import {escapeHtml} from "../core/dom.js";
import {showErrorToast, showSuccessToast} from "../core/toast.js";

let systemUsers = [];
let selectedAuthorizedKeysUser = "";
let createModal = null;
let pathModal = null;
let keysModal = null;

function canManage() {
    return document.getElementById("systemPageState")?.dataset.canManage === "1";
}

function systemUserRowMarkup(user) {
    const stateBits = [
        user.system_account ? "system" : "login",
        user.in_sudo ? "sudo" : "no sudo",
        user.locked ? "locked" : "unlocked"
    ];
    return `
        <tr data-system-username="${escapeHtml(user.username)}">
            <td class="fw-semibold">${escapeHtml(user.username)}</td>
            <td><code>${user.uid}</code> / <code>${user.gid}</code></td>
            <td><code>${escapeHtml(user.shell)}</code></td>
            <td class="small text-secondary">${escapeHtml(user.home)}</td>
            <td class="small text-secondary">${escapeHtml(stateBits.join(", "))}</td>
            <td class="text-end">
                <div class="btn-toolbar justify-content-end gap-1">
                    <div class="btn-group btn-group-sm">
                        <button class="btn btn-outline-secondary system-action-button" data-action="${user.locked ? "unlock" : "lock"}" type="button" ${canManage() ? "" : "disabled"}>${user.locked ? "Unlock" : "Lock"}</button>
                        <button class="btn btn-outline-warning system-action-button" data-action="${user.in_sudo ? "revoke-sudo" : "grant-sudo"}" type="button" ${(canManage() && user.username !== "root") ? "" : "disabled"}>${user.in_sudo ? "Revoke sudo" : "Grant sudo"}</button>
                    </div>
                    <button class="btn btn-outline-primary btn-sm system-keys-button" type="button" ${(canManage() && user.login_user) ? "" : "disabled"}>Keys</button>
                </div>
            </td>
        </tr>
    `;
}

function setSelectedAuthorizedKeysUser(username) {
    selectedAuthorizedKeysUser = username;
    document.getElementById("authorizedKeysSelectedUserLabel").textContent = username || "No login user selected";
}

function renderSystemUsers(payload) {
    systemUsers = payload.users;
    const rootsText = payload.allowedRoots.join(", ");
    document.getElementById("systemAllowedRoots").textContent = `Allowed path roots: ${rootsText}`;
    document.getElementById("systemAllowedRootsInline").textContent = rootsText;

    const host = document.getElementById("systemUsersHost");
    if (!payload.users.length) {
        host.innerHTML = '<tr><td colspan="6" class="text-secondary">No host accounts found.</td></tr>';
        return;
    }
    host.innerHTML = payload.users.map(systemUserRowMarkup).join("");
}

async function refreshSystemPage() {
    const message = document.getElementById("systemUsersMessage");
    try {
        const payload = await requestJson("/api/system/users");
        renderSystemUsers(payload);
        wireSystemRows();
        message.textContent = "";
        if (selectedAuthorizedKeysUser && !systemUsers.some((user) => user.username === selectedAuthorizedKeysUser && user.login_user)) {
            setSelectedAuthorizedKeysUser("");
            document.getElementById("authorizedKeysSummaryMessage").textContent = "Choose a login user from Accounts.";
            document.getElementById("authorizedKeysUsername").value = "";
            document.getElementById("authorizedKeysContent").value = "";
        }
    } catch (error) {
        message.textContent = error.message;
    }
}

async function loadAuthorizedKeys(username) {
    const usernameField = document.getElementById("authorizedKeysUsername");
    const contentField = document.getElementById("authorizedKeysContent");
    const message = document.getElementById("authorizedKeysMessage");
    setSelectedAuthorizedKeysUser(username);
    usernameField.value = username;
    contentField.value = "";
    message.textContent = "Loading authorized_keys...";
    message.className = "small text-secondary";
    document.getElementById("authorizedKeysSummaryMessage").textContent = `Loading keys for ${username}...`;
    try {
        const payload = await requestJson(`/api/system/users/${encodeURIComponent(username)}/authorized-keys`);
        contentField.value = payload.content || "";
        message.textContent = payload.output;
        message.className = "small text-success";
        document.getElementById("authorizedKeysSummaryMessage").textContent = payload.output;
        showSuccessToast(`Loaded authorized_keys for ${username}.`);
        keysModal.show();
    } catch (error) {
        message.textContent = error.message;
        message.className = "small text-danger";
        document.getElementById("authorizedKeysSummaryMessage").textContent = error.message;
        showErrorToast(error.message);
    }
}

function wireSystemRows() {
    document.querySelectorAll(".system-action-button").forEach((button) => {
        button.addEventListener("click", async () => {
            const row = button.closest("[data-system-username]");
            try {
                const payload = await postParams(`/api/system/users/${encodeURIComponent(row.dataset.systemUsername)}/action`, {
                    action: button.dataset.action
                });
                showSuccessToast(payload.output || "System user action completed.");
                await refreshSystemPage();
            } catch (error) {
                showErrorToast(error.message);
            }
        });
    });

    document.querySelectorAll(".system-keys-button").forEach((button) => {
        button.addEventListener("click", async () => {
            const row = button.closest("[data-system-username]");
            bootstrap.Tab.getOrCreateInstance(document.getElementById("system-files-tab")).show();
            await loadAuthorizedKeys(row.dataset.systemUsername);
        });
    });
}

export async function initSystemPage() {
    const createForm = document.getElementById("systemCreateUserForm");
    if (!createForm) {
        return;
    }

    createModal = bootstrap.Modal.getOrCreateInstance(document.getElementById("systemCreateUserModal"));
    pathModal = bootstrap.Modal.getOrCreateInstance(document.getElementById("systemPathModal"));
    keysModal = bootstrap.Modal.getOrCreateInstance(document.getElementById("systemAuthorizedKeysModal"));

    const createMessage = document.getElementById("systemCreateMessage");
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
        ownerField.toggleAttribute("disabled", !isChown || !canManage());
        groupField.toggleAttribute("disabled", !isChown || !canManage());
        modeField.toggleAttribute("required", !isChown);
        modeField.toggleAttribute("disabled", isChown || !canManage());
    };

    const syncHomeField = () => {
        const base = systemAccountField.checked ? "/var/lib/" : "/home/";
        homeField.value = usernameField.value.trim() ? `${base}${usernameField.value.trim()}` : base;
    };

    document.getElementById("openCreateSystemUserModalButton").addEventListener("click", () => {
        createMessage.textContent = "";
        createForm.reset();
        homeField.value = "/home/";
        createModal.show();
    });
    document.getElementById("openSystemPathModalButton").addEventListener("click", () => pathModal.show());
    document.getElementById("openSystemPathModalButtonSecondary").addEventListener("click", () => pathModal.show());
    document.getElementById("openAuthorizedKeysModalButton").addEventListener("click", () => {
        if (!selectedAuthorizedKeysUser) {
            showErrorToast("Choose a login user from Accounts first.");
            return;
        }
        keysModal.show();
    });
    document.getElementById("refreshSystemUsersButton").addEventListener("click", refreshSystemPage);

    createForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        createMessage.textContent = "";
        try {
            const payload = await postForm("/api/system/users", createForm);
            createMessage.textContent = payload.output;
            createMessage.className = "small text-success";
            showSuccessToast(payload.output || "System account created.");
            createModal.hide();
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
            document.getElementById("systemPathSummaryMessage").textContent = "Path action completed successfully.";
            showSuccessToast("Path action completed.");
            pathModal.hide();
        } catch (error) {
            pathOutput.textContent = error.message;
            pathMessage.textContent = "Path action failed.";
            pathMessage.className = "small text-danger";
            document.getElementById("systemPathSummaryMessage").textContent = error.message;
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
            document.getElementById("authorizedKeysSummaryMessage").textContent = payload.output || `Saved keys for ${keysUsernameField.value}.`;
            showSuccessToast(payload.output || "authorized_keys saved.");
        } catch (error) {
            keysMessage.textContent = error.message;
            keysMessage.className = "small text-danger";
            document.getElementById("authorizedKeysSummaryMessage").textContent = error.message;
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
    actionField.addEventListener("change", syncPathFields);

    setSelectedAuthorizedKeysUser("");
    syncHomeField();
    syncPathFields();
    await refreshSystemPage();
}
