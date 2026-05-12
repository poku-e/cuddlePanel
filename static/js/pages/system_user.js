import {postForm, postParams, requestJson} from "../core/api.js";
import {escapeHtml} from "../core/dom.js";
import {showErrorToast, showSuccessToast} from "../core/toast.js";

function canManage() {
    return document.getElementById("systemUserPageState")?.dataset.canManage === "1";
}

function currentUsername(fallbackUsername) {
    return document.getElementById("systemUserPageState")?.dataset.username || fallbackUsername;
}

function systemStateSummary(user) {
    return [
        user.system_account ? "system account" : "login account",
        user.login_user ? "interactive shell enabled" : "login disabled",
        user.locked ? "locked" : "unlocked",
        user.in_sudo ? "sudo access granted" : "sudo access not granted",
        user.password_change_required ? "password change required" : "password change not required",
        user.expires_on ? `expires ${user.expires_on}` : "no account expiration"
    ].join(", ");
}

function setText(id, value) {
    const element = document.getElementById(id);
    if (element) {
        element.textContent = value;
    }
}

function fillProfileForm(user) {
    document.getElementById("systemUserProfileComment").value = user.comment || "";
    document.getElementById("systemUserProfileShell").value = user.shell || "";
    document.getElementById("systemUserProfileHome").value = user.home || "";
    document.getElementById("systemUserProfileMoveHome").checked = false;
    document.getElementById("systemUserProfilePrimaryGroup").value = user.primary_group || "";
    document.getElementById("systemUserProfileSecondaryGroups").value = (user.secondary_groups || []).join(",");
}

function fillSecurityForm(user) {
    document.getElementById("systemUserSecurityPassword").value = "";
    document.getElementById("systemUserSecurityForcePasswordChange").checked = !!user.password_change_required;
    document.getElementById("systemUserSecurityExpiresOn").value = user.expires_on || "";
    document.getElementById("systemUserSecurityClearExpiration").checked = !user.expires_on;
    document.getElementById("systemUserSecurityExpiresOn").disabled =
        document.getElementById("systemUserSecurityClearExpiration").checked || !canManage();
}

function applyRootRestrictions(user) {
    const restricted = user.username === "root";
    const profileElements = [
        "systemUserProfileComment",
        "systemUserProfileShell",
        "systemUserProfileHome",
        "systemUserProfileMoveHome",
        "systemUserProfilePrimaryGroup",
        "systemUserProfileSecondaryGroups"
    ];
    const securityElements = [
        "systemUserSecurityPassword",
        "systemUserSecurityForcePasswordChange",
        "systemUserSecurityExpiresOn",
        "systemUserSecurityClearExpiration"
    ];
    for (const id of profileElements) {
        document.getElementById(id).disabled = restricted || !canManage();
    }
    for (const id of securityElements) {
        document.getElementById(id).disabled = restricted || !canManage();
    }
    document.querySelector('#systemUserProfileForm button[type="submit"]').disabled = restricted || !canManage();
    document.querySelector('#systemUserSecurityForm button[type="submit"]').disabled = restricted || !canManage();
    document.getElementById("systemUserOpenDeleteModalButton").disabled = restricted || !canManage();
}

function fillPrivileges(user) {
    const sudoButton = document.getElementById("systemUserToggleSudoButton");
    sudoButton.dataset.action = user.in_sudo ? "revoke-sudo" : "grant-sudo";
    sudoButton.textContent = user.in_sudo ? "Revoke sudo" : "Grant sudo";
    sudoButton.disabled = !canManage() || user.username === "root";

    const privilegesSudoButton = document.getElementById("systemUserPrivilegesSudoButton");
    privilegesSudoButton.dataset.action = sudoButton.dataset.action;
    privilegesSudoButton.textContent = sudoButton.textContent;
    privilegesSudoButton.disabled = sudoButton.disabled;

    const lockButton = document.getElementById("systemUserToggleLockButton");
    lockButton.dataset.action = user.locked ? "unlock" : "lock";
    lockButton.textContent = user.locked ? "Unlock account" : "Lock account";
    lockButton.disabled = !canManage() || user.username === "root";
}

function fillFilesForm(user) {
    const pathField = document.getElementById("systemUserFilesPath");
    if (!pathField.dataset.operatorEdited) {
        pathField.value = user.home || "";
    }
    document.getElementById("systemUserDeleteUsername").value = user.username;
}

function renderOverview(user, allowedRoots) {
    setText("systemUserOverviewUsername", user.username);
    setText("systemUserOverviewUidGid", `${user.uid} / ${user.gid}`);
    setText("systemUserOverviewComment", user.comment || "No comment set");
    setText("systemUserOverviewShell", user.shell || "No shell set");
    setText("systemUserOverviewHome", user.home || "No home directory set");
    setText("systemUserOverviewPrimaryGroup", user.primary_group || "Unknown");
    setText("systemUserOverviewSecondaryGroups", (user.secondary_groups || []).length
        ? user.secondary_groups.join(", ")
        : "No supplementary groups");
    setText("systemUserOverviewState", systemStateSummary(user));
    setText("systemUserOverviewAllowedRoots", allowedRoots.join(", "));
}

function renderMembershipChips(id, groups, emptyLabel) {
    const host = document.getElementById(id);
    if (!host) {
        return;
    }
    if (!groups.length) {
        host.innerHTML = `<span class="system-user-membership-chip is-empty">${escapeHtml(emptyLabel)}</span>`;
        return;
    }
    host.innerHTML = groups.map((group) => `<span class="system-user-membership-chip">${escapeHtml(group)}</span>`).join("");
}

function renderAuditEvents(events) {
    const host = document.getElementById("systemUserAuditHost");
    if (!events.length) {
        host.innerHTML = '<tr><td colspan="4">No panel audit history for this account yet.</td></tr>';
        return;
    }
    host.innerHTML = events.map((event) => `
        <tr>
            <td class="small"><code>${escapeHtml(event.timestamp)}</code></td>
            <td>${escapeHtml(event.actor)}</td>
            <td>${escapeHtml(event.action)}</td>
            <td class="small">${escapeHtml(event.detail)}</td>
        </tr>
    `).join("");
}

function renderLogfiles(files) {
    const host = document.getElementById("systemUserLogfilesHost");
    const previewTitle = document.getElementById("systemUserLogfilesPreviewTitle");
    const previewMeta = document.getElementById("systemUserLogfilesPreviewMeta");
    const previewContent = document.getElementById("systemUserLogfilesPreviewContent");
    if (!files.length) {
        host.innerHTML = '<tr><td colspan="2">No supported shell history files were found for this account.</td></tr>';
        previewTitle.textContent = "No logfile selected";
        previewMeta.textContent = "A preview will appear here.";
        previewContent.textContent = "No logfile selected.";
        return;
    }
    host.innerHTML = files.map((file, index) => `
        <tr class="system-user-logfiles-row${index === 0 ? " active" : ""}" data-logfile-index="${index}" tabindex="0">
            <td>
                <div class="fw-semibold">${escapeHtml(file.label)}</div>
                <div class="small text-secondary"><code>${escapeHtml(file.name)}</code></div>
            </td>
            <td class="small text-secondary">${file.truncated ? "Tail" : "Full"}</td>
        </tr>
    `).join("");

    const selectLogfile = (index) => {
        const file = files[index];
        if (!file) {
            return;
        }
        for (const row of host.querySelectorAll(".system-user-logfiles-row")) {
            row.classList.toggle("active", row.dataset.logfileIndex === String(index));
        }
        previewTitle.textContent = file.label;
        previewMeta.textContent = `${file.name} • ${file.truncated ? "showing the most recent 128 KB" : "showing full file"}`;
        previewContent.textContent = file.content || "File is empty.";
    };

    for (const row of host.querySelectorAll(".system-user-logfiles-row")) {
        row.addEventListener("click", () => {
            selectLogfile(Number(row.dataset.logfileIndex));
        });
        row.addEventListener("keydown", (event) => {
            if (event.key === "Enter" || event.key === " ") {
                event.preventDefault();
                selectLogfile(Number(row.dataset.logfileIndex));
            }
        });
    }
    selectLogfile(0);
}

function syncFilesActionFields() {
    const actionField = document.getElementById("systemUserFilesAction");
    const ownerField = document.getElementById("systemUserFilesOwner");
    const groupField = document.getElementById("systemUserFilesGroup");
    const modeField = document.getElementById("systemUserFilesMode");
    const isChown = actionField.value === "chown";
    ownerField.toggleAttribute("required", isChown);
    ownerField.toggleAttribute("disabled", !isChown || !canManage());
    groupField.toggleAttribute("disabled", !isChown || !canManage());
    modeField.toggleAttribute("required", !isChown);
    modeField.toggleAttribute("disabled", isChown || !canManage());
}

async function loadUserDetails(username) {
    let payload;
    try {
        payload = await requestJson(`/api/system/users/${encodeURIComponent(username)}`);
    } catch (error) {
        throw new Error(`Unable to load account details: ${error.message}`);
    }
    const user = payload.user;
    renderOverview(user, payload.allowedRoots || []);
    fillProfileForm(user);
    fillSecurityForm(user);
    fillPrivileges(user);
    fillFilesForm(user);
    applyRootRestrictions(user);
    const secondaryGroups = user.secondary_groups || [];
    setText("systemUserSecurityState", systemStateSummary(user));
    setText("systemUserPrivilegesSudoLabel", user.in_sudo ? "Granted" : "Not granted");
    setText("systemUserPrivilegesSudoDetail", user.in_sudo
        ? "This account is currently a member of the sudo group."
        : "This account does not currently inherit sudo through group membership.");
    setText("systemUserPrivilegesAccountType", user.system_account ? "System account" : "Login account");
    setText("systemUserPrivilegesLoginState", user.login_user
        ? `Interactive shell enabled${user.locked ? ", but currently locked" : ""}.`
        : "Interactive shell access is disabled for this account.");
    setText("systemUserPrivilegesPrimaryGroup", user.primary_group || "Unknown");
    setText("systemUserPrivilegesSecondaryGroupCount", `${secondaryGroups.length}`);
    setText("systemUserPrivilegesState", secondaryGroups.length
        ? `${secondaryGroups.length} supplementary group grant${secondaryGroups.length === 1 ? "" : "s"} currently apply.`
        : "No supplementary group grants currently apply.");
    setText("systemUserPrivilegesActionHint", user.username === "root"
        ? "The root account can be inspected here, but sudo changes stay disabled in the panel."
        : canManage()
            ? "Use the action above to grant or revoke sudo group membership for this account."
            : "You have view access to this page, but changing privilege membership requires system:manage.");
    renderMembershipChips("systemUserPrivilegesPrimaryGroupChips",
        user.primary_group ? [user.primary_group] : [],
        "No primary group reported");
    renderMembershipChips("systemUserPrivilegesGroups", secondaryGroups, "No supplementary groups");
    setText("systemUserFilesHomePath", user.home || "No home directory set");
    return user;
}

async function loadAudit(username) {
    let payload;
    try {
        payload = await requestJson(`/api/system/users/${encodeURIComponent(username)}/audit`);
    } catch (error) {
        throw new Error(`Unable to load audit history: ${error.message}`);
    }
    renderAuditEvents(payload.events || []);
}

async function loadAuthorizedKeys(username) {
    const message = document.getElementById("systemUserSshMessage");
    const contentField = document.getElementById("systemUserAuthorizedKeysContent");
    message.textContent = "Loading authorized_keys...";
    try {
        const payload = await requestJson(`/api/system/users/${encodeURIComponent(username)}/authorized-keys`);
        contentField.value = payload.content || "";
        message.textContent = payload.output;
    } catch (error) {
        contentField.value = "";
        message.textContent = `Unable to load SSH keys: ${error.message}`;
    }
}

async function loadLogfiles(username) {
    const message = document.getElementById("systemUserLogfilesMessage");
    message.textContent = "Loading user logfiles...";
    try {
        const payload = await requestJson(`/api/system/users/${encodeURIComponent(username)}/logfiles`);
        renderLogfiles(payload.files || []);
        message.textContent = payload.output || "User logfiles loaded.";
    } catch (error) {
        renderLogfiles([]);
        message.textContent = `Unable to load user logfiles: ${error.message}`;
    }
}

export async function initSystemUserPage(pageUsername) {
    const state = document.getElementById("systemUserPageState");
    if (!state) {
        return;
    }
    const username = currentUsername(pageUsername);
    const deleteModal = bootstrap.Modal.getOrCreateInstance(document.getElementById("systemUserDeleteModal"));
    const profileForm = document.getElementById("systemUserProfileForm");
    const securityForm = document.getElementById("systemUserSecurityForm");
    const sshForm = document.getElementById("systemUserSshForm");
    const filesForm = document.getElementById("systemUserFilesForm");
    const deleteForm = document.getElementById("systemUserDeleteForm");
    const pathField = document.getElementById("systemUserFilesPath");
    const clearExpirationField = document.getElementById("systemUserSecurityClearExpiration");

    const refreshAll = async () => {
        try {
            const user = await loadUserDetails(username);
            try {
                await loadAudit(username);
            } catch (error) {
                renderAuditEvents([]);
                showErrorToast(error.message);
            }
            if (user.login_user && !user.system_account) {
                await loadLogfiles(username);
            } else {
                renderLogfiles([]);
                setText("systemUserLogfilesMessage", "Logfiles are available only for login-enabled non-system users.");
            }
            if (user.login_user && canManage()) {
                await loadAuthorizedKeys(username);
            } else {
                document.getElementById("systemUserAuthorizedKeysContent").value = "";
                setText("systemUserSshMessage", user.login_user
                    ? "SSH keys require manage access."
                    : "SSH keys are available only for login-enabled users.");
            }
        } catch (error) {
            showErrorToast(error.message);
            throw error;
        }
    };

    document.getElementById("systemUserBackButton").addEventListener("click", () => {
        window.location.hash = "page=system";
    });
    document.getElementById("systemUserRefreshButton").addEventListener("click", refreshAll);
    document.getElementById("systemUserOpenDeleteModalButton").addEventListener("click", () => {
        document.getElementById("systemUserDeleteHome").checked = false;
        setText("systemUserDeleteMessage", `Confirm deletion for ${username}.`);
        deleteModal.show();
    });
    document.getElementById("systemUserUseHomePathButton").addEventListener("click", () => {
        pathField.dataset.operatorEdited = "";
        pathField.value = document.getElementById("systemUserOverviewHome").textContent;
    });
    document.getElementById("systemUserReloadKeysButton").addEventListener("click", async () => {
        await loadAuthorizedKeys(username);
    });
    document.getElementById("systemUserToggleSudoButton").addEventListener("click", async (event) => {
        try {
            const payload = await postParams(`/api/system/users/${encodeURIComponent(username)}/action`, {
                action: event.currentTarget.dataset.action
            });
            showSuccessToast(payload.output || "Privilege updated.");
            await refreshAll();
        } catch (error) {
            showErrorToast(error.message);
        }
    });
    document.getElementById("systemUserPrivilegesSudoButton").addEventListener("click", async (event) => {
        try {
            const payload = await postParams(`/api/system/users/${encodeURIComponent(username)}/action`, {
                action: event.currentTarget.dataset.action
            });
            showSuccessToast(payload.output || "Privilege updated.");
            await refreshAll();
        } catch (error) {
            showErrorToast(error.message);
        }
    });
    document.getElementById("systemUserToggleLockButton").addEventListener("click", async (event) => {
        try {
            const payload = await postParams(`/api/system/users/${encodeURIComponent(username)}/action`, {
                action: event.currentTarget.dataset.action
            });
            showSuccessToast(payload.output || "Account state updated.");
            await refreshAll();
        } catch (error) {
            showErrorToast(error.message);
        }
    });

    profileForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        const message = document.getElementById("systemUserProfileMessage");
        try {
            const payload = await postForm(`/api/system/users/${encodeURIComponent(username)}/edit`, profileForm);
            message.textContent = payload.output;
            showSuccessToast(payload.output || "Profile updated.");
            await refreshAll();
        } catch (error) {
            message.textContent = error.message;
            showErrorToast(error.message);
        }
    });

    securityForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        const message = document.getElementById("systemUserSecurityMessage");
        try {
            const payload = await postForm(`/api/system/users/${encodeURIComponent(username)}/security`, securityForm);
            message.textContent = payload.output;
            showSuccessToast(payload.output || "Security settings updated.");
            await refreshAll();
        } catch (error) {
            message.textContent = error.message;
            showErrorToast(error.message);
        }
    });

    sshForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        const message = document.getElementById("systemUserSshMessage");
        try {
            const payload = await postForm(`/api/system/users/${encodeURIComponent(username)}/authorized-keys`, sshForm);
            message.textContent = payload.output;
            showSuccessToast(payload.output || "authorized_keys saved.");
            await loadAudit(username);
        } catch (error) {
            message.textContent = error.message;
            showErrorToast(error.message);
        }
    });

    filesForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        const message = document.getElementById("systemUserFilesMessage");
        const output = document.getElementById("systemUserFilesOutput");
        output.textContent = "Running path action...";
        try {
            const payload = await postForm("/api/system/path-action", filesForm);
            message.textContent = "Path action completed.";
            output.textContent = payload.output;
            showSuccessToast(payload.output || "Path action completed.");
            await loadAudit(username);
        } catch (error) {
            message.textContent = "Path action failed.";
            output.textContent = error.message;
            showErrorToast(error.message);
        }
    });

    deleteForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        const message = document.getElementById("systemUserDeleteMessage");
        try {
            const payload = await postParams(`/api/system/users/${encodeURIComponent(username)}/action`, {
                action: "delete",
                delete_home: document.getElementById("systemUserDeleteHome").checked ? "on" : ""
            });
            message.textContent = payload.output;
            showSuccessToast(payload.output || "Account deleted.");
            deleteModal.hide();
            window.location.hash = "page=system";
        } catch (error) {
            message.textContent = error.message;
            showErrorToast(error.message);
        }
    });

    pathField.addEventListener("input", () => {
        pathField.dataset.operatorEdited = "1";
    });
    document.getElementById("systemUserFilesAction").addEventListener("change", syncFilesActionFields);
    clearExpirationField.addEventListener("change", () => {
        document.getElementById("systemUserSecurityExpiresOn").disabled =
            clearExpirationField.checked || !canManage();
    });

    syncFilesActionFields();
    await refreshAll();
}
