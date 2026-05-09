import {postForm, postParams, requestJson} from "../core/api.js";
import {escapeHtml} from "../core/dom.js";
import {showErrorToast, showSuccessToast} from "../core/toast.js";

function permissionValue(user, page) {
    return user.permissions[page] || "none";
}

function permissionsMarkup(user) {
    const canManage = document.getElementById("usersPageState")?.dataset.canManage === "1";
    return [
        "dashboard",
        "users",
        "services",
        "system",
        "nginx",
        "terminal",
        "codex",
        "deploy"
    ].map((page) => `
        <label class="permission-field">
            <span>${escapeHtml(page)}</span>
            <select class="cp-input cp-select" data-page="${page}" ${(user.role === "superuser" || !canManage) ? "disabled" : ""}>
                <option value="none" ${permissionValue(user, page) === "none" ? "selected" : ""}>None</option>
                <option value="view" ${permissionValue(user, page) === "view" ? "selected" : ""}>View</option>
                <option value="manage" ${permissionValue(user, page) === "manage" ? "selected" : ""}>Manage</option>
            </select>
        </label>
    `).join("");
}

function renderUsers(users) {
    const host = document.getElementById("usersTableHost");
    const canManage = document.getElementById("usersPageState")?.dataset.canManage === "1";
    if (!users.length) {
        host.innerHTML = "<div class=\"text-secondary\">No users found.</div>";
        return;
    }

    host.innerHTML = users.map((user) => `
        <article class="user-card" data-username="${user.username}">
            <div class="user-card-head">
                <div>
                    <h3>${escapeHtml(user.username)}</h3>
                    <div class="user-role">${escapeHtml(user.role)}</div>
                </div>
                <div class="user-actions">
                    <button class="btn btn-outline-primary btn-sm save-user-button" type="button" ${(user.role === "superuser" || !canManage) ? "disabled" : ""}>Save access</button>
                    <button class="btn btn-outline-danger btn-sm delete-user-button" type="button" ${(user.role === "superuser" || !canManage) ? "disabled" : ""}>Delete</button>
                </div>
            </div>
            <div class="permissions-grid">${permissionsMarkup(user)}</div>
            <div class="small user-status"></div>
        </article>
    `).join("");
}

function collectPermissions(card) {
    return Array.from(card.querySelectorAll("[data-page]"))
        .map((field) => `${field.dataset.page}:${field.value}`)
        .filter((entry) => !entry.endsWith(":none"))
        .join(",");
}

async function refreshUsersPage() {
    const message = document.getElementById("usersMessage");
    try {
        const payload = await requestJson("/api/users");
        renderUsers(payload.users);
        wireUserCards();
        message.textContent = "";
    } catch (error) {
        message.textContent = error.message;
    }
}

function wireUserCards() {
    document.querySelectorAll(".save-user-button").forEach((button) => {
        button.addEventListener("click", async () => {
            const card = button.closest("[data-username]");
            const status = card.querySelector(".user-status");
            status.textContent = "";
            try {
                await postParams(`/api/users/${encodeURIComponent(card.dataset.username)}/permissions`, {
                    permissions: collectPermissions(card)
                });
                status.textContent = "Permissions saved.";
                status.className = "small user-status text-success";
                showSuccessToast(`Permissions saved for ${card.dataset.username}.`);
            } catch (error) {
                status.textContent = error.message;
                status.className = "small user-status text-danger";
                showErrorToast(error.message);
            }
        });
    });

    document.querySelectorAll(".delete-user-button").forEach((button) => {
        button.addEventListener("click", async () => {
            const card = button.closest("[data-username]");
            const status = card.querySelector(".user-status");
            status.textContent = "";
            try {
                await requestJson(`/api/users/${encodeURIComponent(card.dataset.username)}`, {
                    method: "DELETE"
                });
                showSuccessToast(`Deleted ${card.dataset.username}.`);
                await refreshUsersPage();
            } catch (error) {
                status.textContent = error.message;
                status.className = "small user-status text-danger";
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
    const createMessage = document.getElementById("userCreateMessage");
    const refreshButton = document.getElementById("refreshUsersButton");

    createForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        createMessage.textContent = "";
        try {
            await postForm("/api/users", createForm);
            createMessage.textContent = "User created.";
            createMessage.className = "small text-success";
            showSuccessToast("User created.");
            createForm.reset();
            await refreshUsersPage();
        } catch (error) {
            createMessage.textContent = error.message;
            createMessage.className = "small text-danger";
            showErrorToast(error.message);
        }
    });

    refreshButton.addEventListener("click", refreshUsersPage);
    await refreshUsersPage();
}
