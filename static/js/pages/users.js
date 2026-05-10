import {postForm, postParams, requestJson} from "../core/api.js";
import {escapeHtml} from "../core/dom.js";
import {showErrorToast, showSuccessToast} from "../core/toast.js";

const pageKeys = [
    "dashboard",
    "users",
    "services",
    "system",
    "nginx",
    "terminal",
    "codex",
    "deploy"
];

let usersCache = [];
let selectedUser = null;
let createModal = null;
let permissionsModal = null;

function canManage() {
    return document.getElementById("usersPageState")?.dataset.canManage === "1";
}

function permissionValue(user, page) {
    return user.permissions[page] || "none";
}

function permissionSummary(user) {
    const entries = pageKeys
        .map((page) => `${page}:${permissionValue(user, page)}`)
        .filter((entry) => !entry.endsWith(":none"));
    return entries.length ? entries.join(", ") : "No page access";
}

function roleBadge(role) {
    if (role === "superuser") {
        return '<span class="badge text-bg-dark">superuser</span>';
    }
    if (role === "admin") {
        return '<span class="badge text-bg-primary">admin</span>';
    }
    return '<span class="badge text-bg-secondary">operator</span>';
}

function renderUsers(users) {
    const host = document.getElementById("usersTableHost");
    if (!users.length) {
        host.innerHTML = '<tr><td colspan="4" class="text-secondary">No users found.</td></tr>';
        return;
    }

    host.innerHTML = users.map((user) => `
        <tr data-username="${escapeHtml(user.username)}">
            <td class="fw-semibold">${escapeHtml(user.username)}</td>
            <td>${roleBadge(user.role)}</td>
            <td class="small text-secondary">${escapeHtml(permissionSummary(user))}</td>
            <td class="text-end">
                <div class="btn-group btn-group-sm">
                    <button class="btn btn-outline-primary edit-user-button" type="button" ${(user.role === "superuser" || !canManage()) ? "disabled" : ""}>Edit access</button>
                    <button class="btn btn-outline-danger delete-user-button" type="button" ${(user.role === "superuser" || !canManage()) ? "disabled" : ""}>Delete</button>
                </div>
            </td>
        </tr>
    `).join("");
}

function renderPermissionsTable(user) {
    const body = document.getElementById("userPermissionsTableBody");
    body.innerHTML = pageKeys.map((page) => `
        <tr>
            <td class="text-capitalize">${escapeHtml(page)}</td>
            <td>
                <select class="cp-input cp-select" data-permission-page="${page}" ${(user.role === "superuser" || !canManage()) ? "disabled" : ""}>
                    <option value="none" ${permissionValue(user, page) === "none" ? "selected" : ""}>None</option>
                    <option value="view" ${permissionValue(user, page) === "view" ? "selected" : ""}>View</option>
                    <option value="manage" ${permissionValue(user, page) === "manage" ? "selected" : ""}>Manage</option>
                </select>
            </td>
        </tr>
    `).join("");
}

function collectPermissions() {
    return Array.from(document.querySelectorAll("[data-permission-page]"))
        .map((field) => `${field.dataset.permissionPage}:${field.value}`)
        .filter((entry) => !entry.endsWith(":none"))
        .join(",");
}

async function refreshUsersPage() {
    const message = document.getElementById("usersMessage");
    try {
        const payload = await requestJson("/api/users");
        usersCache = payload.users;
        renderUsers(payload.users);
        wireUserRows();
        message.textContent = "";
    } catch (error) {
        message.textContent = error.message;
    }
}

function openPermissionsModal(username) {
    selectedUser = usersCache.find((user) => user.username === username) || null;
    if (!selectedUser) {
        showErrorToast("User not found.");
        return;
    }
    document.getElementById("userPermissionsModalTitle").textContent = `Edit access: ${selectedUser.username}`;
    document.getElementById("userPermissionsModalMeta").textContent = `Role: ${selectedUser.role}`;
    document.getElementById("userPermissionsMessage").textContent = "";
    renderPermissionsTable(selectedUser);
    permissionsModal.show();
}

function wireUserRows() {
    document.querySelectorAll(".edit-user-button").forEach((button) => {
        button.addEventListener("click", () => {
            const row = button.closest("[data-username]");
            openPermissionsModal(row.dataset.username);
        });
    });

    document.querySelectorAll(".delete-user-button").forEach((button) => {
        button.addEventListener("click", async () => {
            const row = button.closest("[data-username]");
            if (!window.confirm(`Delete user ${row.dataset.username}? This cannot be undone from the dashboard.`)) {
                return;
            }
            try {
                await requestJson(`/api/users/${encodeURIComponent(row.dataset.username)}`, {method: "DELETE"});
                showSuccessToast(`Deleted ${row.dataset.username}.`);
                await refreshUsersPage();
            } catch (error) {
                showErrorToast(error.message);
            }
        });
    });
}

export async function initUsersPage() {
    const createForm = document.getElementById("userCreateForm");
    if (!createForm) {
        return;
    }

    createModal = bootstrap.Modal.getOrCreateInstance(document.getElementById("userCreateModal"));
    permissionsModal = bootstrap.Modal.getOrCreateInstance(document.getElementById("userPermissionsModal"));

    const createMessage = document.getElementById("userCreateMessage");
    const permissionsMessage = document.getElementById("userPermissionsMessage");

    document.getElementById("openCreateUserModalButton").addEventListener("click", () => {
        createMessage.textContent = "";
        createForm.reset();
        createModal.show();
    });

    document.getElementById("refreshUsersButton").addEventListener("click", refreshUsersPage);

    createForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        createMessage.textContent = "";
        try {
            await postForm("/api/users", createForm);
            createMessage.textContent = "User created.";
            createMessage.className = "small text-success";
            showSuccessToast("User created.");
            createModal.hide();
            await refreshUsersPage();
        } catch (error) {
            createMessage.textContent = error.message;
            createMessage.className = "small text-danger";
            showErrorToast(error.message);
        }
    });

    document.getElementById("saveUserPermissionsButton").addEventListener("click", async () => {
        if (!selectedUser) {
            return;
        }
        permissionsMessage.textContent = "";
        try {
            await postParams(`/api/users/${encodeURIComponent(selectedUser.username)}/permissions`, {
                permissions: collectPermissions()
            });
            permissionsMessage.textContent = "Permissions saved.";
            permissionsMessage.className = "small text-success";
            showSuccessToast(`Permissions saved for ${selectedUser.username}.`);
            permissionsModal.hide();
            await refreshUsersPage();
        } catch (error) {
            permissionsMessage.textContent = error.message;
            permissionsMessage.className = "small text-danger";
            showErrorToast(error.message);
        }
    });

    await refreshUsersPage();
}
