import {postForm, postParams, requestJson} from "../core/api.js";
import {escapeHtml} from "../core/dom.js";
import {showErrorToast, showSuccessToast} from "../core/toast.js";

let currentConversationId = null;
let currentCursor = 0;
let pollTimer = null;
let latestConversations = [];

function stopPolling() {
    if (pollTimer) {
        clearTimeout(pollTimer);
        pollTimer = null;
    }
}

function pageState() {
    return document.getElementById("codexPageState");
}

function canManage() {
    return pageState()?.dataset.canManage === "1";
}

function renderProjects(projects) {
    const list = document.getElementById("codexProjectList");
    const select = document.querySelector('#codexConversationForm select[name="project_id"]');
    if (!list || !select) {
        return;
    }
    list.innerHTML = projects.length
        ? projects.map((project) => `
            <article class="codex-project-card">
                <div class="fw-semibold">${escapeHtml(project.name)}</div>
                <div class="small text-secondary"><code>${escapeHtml(project.root)}</code></div>
            </article>
        `).join("")
        : '<div class="text-secondary small">No projects added yet. New conversations can still run in maintenance mode.</div>';

    const selected = select.value;
    select.innerHTML = '<option value="">Server maintenance</option>' + projects.map((project) => `
        <option value="${escapeHtml(project.id)}">${escapeHtml(project.name)}</option>
    `).join("");
    select.value = projects.some((project) => project.id === selected) ? selected : "";
}

function renderConversations(conversations) {
    const list = document.getElementById("codexConversationList");
    if (!list) {
        return;
    }
    list.innerHTML = conversations.length
        ? conversations.map((conversation) => `
            <button class="codex-conversation-card ${conversation.id === currentConversationId ? "active" : ""}"
                    type="button"
                    data-conversation-id="${escapeHtml(conversation.id)}">
                <div class="fw-semibold">${escapeHtml(conversation.title)}</div>
                <div class="small text-secondary">${escapeHtml(conversation.maintenance_mode ? "Server maintenance" : conversation.project_name)}</div>
                <div class="small ${conversation.closed ? "text-danger" : "text-secondary"}">${conversation.closed ? "Closed" : escapeHtml(conversation.working_directory)}</div>
            </button>
        `).join("")
        : '<div class="text-secondary small">No conversations yet.</div>';

    list.querySelectorAll("[data-conversation-id]").forEach((button) => {
        button.addEventListener("click", () => {
            selectConversation(button.dataset.conversationId, conversations);
        });
    });
}

function setConversationUiEnabled(enabled, closed = false) {
    const form = document.getElementById("codexMessageForm");
    const closeButton = document.getElementById("codexCloseConversationButton");
    const exportButton = document.getElementById("codexExportTranscriptButton");
    if (!form || !closeButton || !exportButton) {
        return;
    }
    form.querySelectorAll("textarea, button").forEach((field) => {
        field.disabled = !enabled || closed || !canManage();
    });
    closeButton.disabled = !enabled || closed || !canManage();
    exportButton.disabled = !enabled;
}

function updateConversationHeader(conversation) {
    const title = document.getElementById("codexConversationTitle");
    const meta = document.getElementById("codexConversationMeta");
    if (!title || !meta) {
        return;
    }
    if (!conversation) {
        title.textContent = "Select a conversation";
        meta.textContent = "Project-scoped conversations keep Codex context per thread.";
        return;
    }
    title.textContent = conversation.title;
    const sessionLabel = conversation.codex_session_id
        ? `Session ${conversation.codex_session_id}`
        : "Session id pending";
    meta.textContent = conversation.maintenance_mode
        ? `Maintenance mode in ${conversation.working_directory} - ${sessionLabel}`
        : `${conversation.project_name} - ${conversation.working_directory} - ${sessionLabel}`;
}

async function refreshProjects() {
    const payload = await requestJson("/api/codex/projects");
    renderProjects(payload.projects);
}

async function refreshConversations() {
    const payload = await requestJson("/api/codex/conversations");
    latestConversations = payload.conversations;
    renderConversations(payload.conversations);
    if (currentConversationId && !payload.conversations.some((item) => item.id === currentConversationId)) {
        currentConversationId = null;
        currentCursor = 0;
        stopPolling();
        updateConversationHeader(null);
        document.getElementById("codexConversationOutput").textContent = "Conversation output will appear here.";
        document.getElementById("codexAuditHistory").textContent = "Conversation history will appear here.";
        setConversationUiEnabled(false);
    }
    return payload.conversations;
}

function formatAuditTimestamp(timestamp) {
    if (!timestamp) {
        return "";
    }
    return new Date(timestamp * 1000).toLocaleString();
}

async function refreshAuditHistory() {
    const history = document.getElementById("codexAuditHistory");
    if (!history) {
        return;
    }
    if (!currentConversationId) {
        history.textContent = "Conversation history will appear here.";
        return;
    }
    const payload = await requestJson(`/api/codex/conversations/${encodeURIComponent(currentConversationId)}/history`);
    history.innerHTML = payload.events.length
        ? payload.events.map((event) => `
            <div class="mb-2">
                <div class="fw-semibold">${escapeHtml(event.kind)}</div>
                <div>${escapeHtml(event.detail)}</div>
                <div class="text-secondary">${escapeHtml(formatAuditTimestamp(event.timestamp))}</div>
            </div>
        `).join("")
        : '<div class="text-secondary">No audit history yet.</div>';
}

async function pollConversation() {
    if (!currentConversationId) {
        return;
    }
    try {
        const payload = await postParams(`/api/codex/conversations/${encodeURIComponent(currentConversationId)}/read`, {
            cursor: String(currentCursor)
        });
        const output = document.getElementById("codexConversationOutput");
        if (payload.truncated) {
            output.textContent += "\n[older conversation output truncated]\n";
        }
        if (payload.data) {
            output.textContent += payload.data;
            output.scrollTop = output.scrollHeight;
        }
        currentCursor = payload.cursor;
        const status = document.getElementById("codexMessageStatus");
        if (payload.closed) {
            status.textContent = payload.exit_code >= 0
                ? `Conversation closed with exit code ${payload.exit_code}.`
                : "Conversation closed.";
            status.className = "small text-secondary";
            setConversationUiEnabled(true, true);
            const conversations = await refreshConversations();
            updateConversationHeader(conversations.find((item) => item.id === currentConversationId) || null);
            await refreshAuditHistory();
            return;
        }
    } catch (error) {
        showErrorToast(error.message);
        return;
    }
    pollTimer = setTimeout(pollConversation, 200);
}

function selectConversation(conversationId, conversations = []) {
    currentConversationId = conversationId;
    currentCursor = 0;
    stopPolling();
    const output = document.getElementById("codexConversationOutput");
    output.textContent = "";
    const conversation = conversations.find((item) => item.id === conversationId);
    updateConversationHeader(conversation);
    setConversationUiEnabled(Boolean(conversation), conversation?.closed);
    renderConversations(conversations);
    refreshAuditHistory().catch((error) => {
        showErrorToast(error.message);
    });
    pollConversation().catch((error) => {
        showErrorToast(error.message);
    });
}

export async function initCodexPage() {
    const projectForm = document.getElementById("codexProjectForm");
    const conversationForm = document.getElementById("codexConversationForm");
    const messageForm = document.getElementById("codexMessageForm");
    const closeButton = document.getElementById("codexCloseConversationButton");
    const exportButton = document.getElementById("codexExportTranscriptButton");
    if (!projectForm || !conversationForm || !messageForm || !closeButton || !exportButton) {
        return;
    }

    setConversationUiEnabled(false);
    await refreshProjects();
    const initialConversations = await refreshConversations();
    if (initialConversations.length) {
        selectConversation(initialConversations[0].id, initialConversations);
    }

    projectForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        const message = document.getElementById("codexProjectMessage");
        message.textContent = "";
        try {
            await postForm("/api/codex/projects", projectForm);
            projectForm.reset();
            await refreshProjects();
            message.textContent = "Project added.";
            message.className = "small text-success";
            showSuccessToast("Project added.");
        } catch (error) {
            message.textContent = error.message;
            message.className = "small text-danger";
            showErrorToast(error.message);
        }
    });

    conversationForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        const message = document.getElementById("codexConversationMessage");
        message.textContent = "";
        try {
            const payload = await postForm("/api/codex/conversations", conversationForm);
            conversationForm.reset();
            const conversations = await refreshConversations();
            selectConversation(payload.conversation_id, conversations);
            message.textContent = "Conversation started.";
            message.className = "small text-success";
            showSuccessToast("Conversation started.");
        } catch (error) {
            message.textContent = error.message;
            message.className = "small text-danger";
            showErrorToast(error.message);
        }
    });

    messageForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        if (!currentConversationId) {
            return;
        }
        const status = document.getElementById("codexMessageStatus");
        const field = messageForm.querySelector('[name="message"]');
        status.textContent = "";
        try {
            await postParams(`/api/codex/conversations/${encodeURIComponent(currentConversationId)}/send`, {
                message: field.value
            });
            field.value = "";
            showSuccessToast("Message sent to Codex.");
            await refreshAuditHistory();
        } catch (error) {
            status.textContent = error.message;
            status.className = "small text-danger";
            showErrorToast(error.message);
        }
    });

    closeButton.addEventListener("click", async () => {
        if (!currentConversationId) {
            return;
        }
        try {
            await postParams(`/api/codex/conversations/${encodeURIComponent(currentConversationId)}/close`, {});
            showSuccessToast("Conversation closed.");
            const conversations = await refreshConversations();
            const next = conversations.find((item) => !item.closed) || conversations[0];
            if (next) {
                selectConversation(next.id, conversations);
            } else {
                currentConversationId = null;
                stopPolling();
                updateConversationHeader(null);
                document.getElementById("codexConversationOutput").textContent = "Conversation output will appear here.";
                document.getElementById("codexAuditHistory").textContent = "Conversation history will appear here.";
                setConversationUiEnabled(false);
            }
        } catch (error) {
            showErrorToast(error.message);
        }
    });

    exportButton.addEventListener("click", async () => {
        if (!currentConversationId) {
            return;
        }
        try {
            const payload = await requestJson(`/api/codex/conversations/${encodeURIComponent(currentConversationId)}/transcript`);
            const selected = latestConversations.find((item) => item.id === currentConversationId);
            const fileName = `${selected?.title || "codex-conversation"}.log`.replace(/[^a-z0-9._-]+/gi, "-");
            const blob = new Blob([payload.transcript || ""], {type: "text/plain;charset=utf-8"});
            const url = URL.createObjectURL(blob);
            const link = document.createElement("a");
            link.href = url;
            link.download = fileName;
            document.body.appendChild(link);
            link.click();
            link.remove();
            URL.revokeObjectURL(url);
            showSuccessToast("Transcript exported.");
        } catch (error) {
            showErrorToast(error.message);
        }
    });
}
