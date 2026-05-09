import {postForm, postParams} from "../core/api.js";
import {showErrorToast, showSuccessToast} from "../core/toast.js";

let terminalInstance = null;
let terminalFitAddon = null;
let terminalSessionId = null;
let terminalCursor = 0;
let terminalPollTimer = null;
let terminalResizeHandler = null;
let reloadPage = null;

async function terminalPost(action, payload = {}) {
    if (!terminalSessionId) {
        throw new Error("terminal session not initialized");
    }
    return postParams(`/api/terminal/session/${encodeURIComponent(terminalSessionId)}/${action}`, payload);
}

function stopTerminalPolling() {
    if (terminalPollTimer) {
        clearTimeout(terminalPollTimer);
        terminalPollTimer = null;
    }
}

async function pollTerminal() {
    if (!terminalSessionId || !terminalInstance) {
        return;
    }
    try {
        const payload = await terminalPost("read", {cursor: String(terminalCursor)});
        if (payload.truncated) {
            terminalInstance.write("\r\n[terminal output truncated]\r\n");
        }
        if (payload.data) {
            terminalInstance.write(payload.data);
        }
        terminalCursor = payload.cursor;
        const status = document.getElementById("terminalStatus");
        if (payload.closed) {
            status.textContent = payload.exit_code >= 0
                ? `Terminal session closed with exit code ${payload.exit_code}.`
                : "Terminal session closed.";
            showSuccessToast(status.textContent);
            terminalSessionId = null;
            return;
        }
    } catch (error) {
        const status = document.getElementById("terminalStatus");
        status.textContent = error.message;
        showErrorToast(error.message);
        return;
    }
    terminalPollTimer = setTimeout(pollTerminal, 150);
}

async function createTerminalSession() {
    const host = document.getElementById("terminalHost");
    const status = document.getElementById("terminalStatus");
    if (!host || !window.Terminal || !window.FitAddon) {
        return;
    }

    stopTerminalPolling();
    terminalSessionId = null;
    terminalCursor = 0;
    if (terminalResizeHandler) {
        window.removeEventListener("resize", terminalResizeHandler);
        terminalResizeHandler = null;
    }
    host.innerHTML = "";

    terminalInstance = new window.Terminal({
        cursorBlink: true,
        fontFamily: '"SFMono-Regular", Consolas, "Liberation Mono", monospace',
        fontSize: 14,
        theme: {
            background: "#0f1724",
            foreground: "#e8edf5"
        }
    });
    terminalFitAddon = new window.FitAddon.FitAddon();
    terminalInstance.loadAddon(terminalFitAddon);
    terminalInstance.open(host);
    terminalFitAddon.fit();

    const createPayload = await postParams("/api/terminal/session", {
        rows: String(terminalInstance.rows || 24),
        cols: String(terminalInstance.cols || 80)
    });
    terminalSessionId = createPayload.session_id;
    status.textContent = "Terminal session connected.";
    showSuccessToast("Terminal session connected.");

    terminalInstance.onData((data) => {
        terminalPost("write", {data}).catch((error) => {
            status.textContent = error.message;
            showErrorToast(error.message);
        });
    });

    terminalResizeHandler = () => {
        if (!terminalInstance || !terminalFitAddon || !terminalSessionId) {
            return;
        }
        terminalFitAddon.fit();
        terminalPost("resize", {
            rows: String(terminalInstance.rows || 24),
            cols: String(terminalInstance.cols || 80)
        }).catch(() => {});
    };
    window.addEventListener("resize", terminalResizeHandler);

    const reconnectButton = document.getElementById("terminalReconnectButton");
    if (reconnectButton) {
        reconnectButton.onclick = async () => {
            if (terminalSessionId) {
                await terminalPost("close", {}).catch(() => {});
            }
            await createTerminalSession();
        };
    }

    const closeButton = document.getElementById("terminalCloseButton");
    if (closeButton) {
        closeButton.onclick = async () => {
            if (!terminalSessionId) {
                return;
            }
            await terminalPost("close", {}).catch(() => {});
            stopTerminalPolling();
            terminalSessionId = null;
            if (terminalResizeHandler) {
                window.removeEventListener("resize", terminalResizeHandler);
                terminalResizeHandler = null;
            }
            status.textContent = "Terminal session closed.";
            showSuccessToast("Terminal session closed.");
        };
    }

    pollTerminal();
}

function bindTerminalOtpPage() {
    const form = document.getElementById("terminalOtpForm");
    if (!form) {
        return false;
    }
    const message = document.getElementById("terminalOtpMessage");
    form.addEventListener("submit", async (event) => {
        event.preventDefault();
        message.textContent = "";
        try {
            await postForm("/api/2fa/verify-terminal", form);
            showSuccessToast("Terminal OTP verified.");
            await reloadPage("terminal");
        } catch (error) {
            message.textContent = error.message;
            message.className = "small text-danger";
            showErrorToast(error.message);
        }
    });
    return true;
}

export function setTerminalReloader(fn) {
    reloadPage = fn;
}

export function initTerminalPage() {
    if (bindTerminalOtpPage()) {
        return;
    }
    if (!document.getElementById("terminalHost")) {
        return;
    }
    createTerminalSession().catch((error) => {
        const status = document.getElementById("terminalStatus");
        if (status) {
            status.textContent = error.message;
        }
        showErrorToast(error.message);
    });
}
