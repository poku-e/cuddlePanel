import {postForm, requestJson} from "../core/api.js";
import {escapeHtml} from "../core/dom.js";
import {showErrorToast, showSuccessToast} from "../core/toast.js";

let createModal = null;
let pathModal = null;

function canManage() {
    return document.getElementById("systemPageState")?.dataset.canManage === "1";
}

function visibleSystemUsers(users) {
    return [...users]
        .filter((user) => user.login_user)
        .sort((left, right) => {
            if (left.system_account !== right.system_account) {
                return left.system_account ? 1 : -1;
            }
            if (left.uid !== right.uid) {
                return left.uid - right.uid;
            }
            return left.username.localeCompare(right.username);
        });
}

function systemUserRowMarkup(user) {
    const stateBits = [
        user.system_account ? "system" : "login",
        user.in_sudo ? "sudo" : "no sudo",
        user.locked ? "locked" : "unlocked"
    ];
    return `
        <tr data-system-username="${escapeHtml(user.username)}">
            <td>
                <button class="btn btn-link p-0 system-manage-link" type="button">${escapeHtml(user.username)}</button>
            </td>
            <td><code>${user.uid}</code> / <code>${user.gid}</code></td>
            <td><code>${escapeHtml(user.shell)}</code></td>
            <td class="small">${escapeHtml(user.home)}</td>
            <td class="small">${escapeHtml(stateBits.join(", "))}</td>
            <td class="text-end">
                <button class="btn btn-outline-primary btn-sm system-manage-button" type="button">Manage</button>
            </td>
        </tr>
    `;
}

function openSystemUserPage(username) {
    window.location.hash = `page=${encodeURIComponent(`system-user:${username}`)}`;
}

function renderSystemUsers(payload) {
    const usersForTable = visibleSystemUsers(payload.users);
    const rootsText = payload.allowedRoots.join(", ");
    document.getElementById("systemAllowedRoots").textContent = `Allowed path roots: ${rootsText}`;
    document.getElementById("systemAllowedRootsInline").textContent = rootsText;

    const host = document.getElementById("systemUsersHost");
    if (!usersForTable.length) {
        host.innerHTML = '<tr><td colspan="6">No login-enabled host accounts found.</td></tr>';
        return;
    }
    host.innerHTML = usersForTable.map(systemUserRowMarkup).join("");
}

function wireSystemRows() {
    document.querySelectorAll(".system-manage-link, .system-manage-button").forEach((button) => {
        button.addEventListener("click", () => {
            const row = button.closest("[data-system-username]");
            openSystemUserPage(row.dataset.systemUsername);
        });
    });
}

async function refreshSystemPage() {
    const message = document.getElementById("systemUsersMessage");
    try {
        const payload = await requestJson("/api/system/users");
        renderSystemUsers(payload);
        wireSystemRows();
        message.textContent = "";
    } catch (error) {
        message.textContent = error.message;
    }
}

export async function initSystemPage() {
    const createForm = document.getElementById("systemCreateUserForm");
    if (!createForm) {
        return;
    }

    createModal = bootstrap.Modal.getOrCreateInstance(document.getElementById("systemCreateUserModal"));
    pathModal = bootstrap.Modal.getOrCreateInstance(document.getElementById("systemPathModal"));

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
    document.getElementById("refreshSystemUsersButton").addEventListener("click", refreshSystemPage);

    createForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        createMessage.textContent = "";
        try {
            const payload = await postForm("/api/system/users", createForm);
            createMessage.textContent = payload.output;
            createMessage.className = "small";
            showSuccessToast(payload.output || "System account created.");
            createModal.hide();
            await refreshSystemPage();
        } catch (error) {
            createMessage.textContent = error.message;
            createMessage.className = "small";
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
            pathMessage.className = "small";
            document.getElementById("systemPathSummaryMessage").textContent = "Path action completed successfully.";
            showSuccessToast("Path action completed.");
            pathModal.hide();
        } catch (error) {
            pathOutput.textContent = error.message;
            pathMessage.textContent = "Path action failed.";
            pathMessage.className = "small";
            document.getElementById("systemPathSummaryMessage").textContent = error.message;
            showErrorToast(error.message);
        }
    });

    usernameField.addEventListener("input", syncHomeField);
    systemAccountField.addEventListener("change", syncHomeField);
    actionField.addEventListener("change", syncPathFields);

    syncHomeField();
    syncPathFields();
    await refreshSystemPage();
}
