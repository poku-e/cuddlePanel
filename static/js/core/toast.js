let toastRegion = null;

function ensureToastRegion() {
    if (toastRegion) {
        return toastRegion;
    }
    toastRegion = document.createElement("div");
    toastRegion.id = "toastRegion";
    toastRegion.className = "cp-toast-region";
    toastRegion.setAttribute("aria-live", "polite");
    toastRegion.setAttribute("aria-atomic", "true");
    document.body.appendChild(toastRegion);
    return toastRegion;
}

function createToast(type, message, title) {
    const toast = document.createElement("section");
    toast.className = `cp-toast cp-toast-${type}`;
    toast.setAttribute("role", type === "error" ? "alert" : "status");

    const body = document.createElement("div");
    body.className = "cp-toast-body";

    const heading = document.createElement("div");
    heading.className = "cp-toast-title";
    heading.textContent = title;

    const text = document.createElement("div");
    text.className = "cp-toast-message";
    text.textContent = message;

    const close = document.createElement("button");
    close.type = "button";
    close.className = "cp-toast-close";
    close.setAttribute("aria-label", "Dismiss notification");
    close.textContent = "x";

    close.addEventListener("click", () => {
        toast.remove();
    });

    body.appendChild(heading);
    body.appendChild(text);
    toast.appendChild(body);
    toast.appendChild(close);
    return toast;
}

export function initToasts() {
    ensureToastRegion();
}

export function showToast({type = "info", title = "Notice", message = "", duration = 4200}) {
    if (!message) {
        return;
    }
    const region = ensureToastRegion();
    const toast = createToast(type, message, title);
    region.appendChild(toast);
    window.setTimeout(() => {
        toast.classList.add("is-leaving");
        window.setTimeout(() => toast.remove(), 180);
    }, duration);
}

export function showSuccessToast(message, title = "Success") {
    showToast({type: "success", title, message});
}

export function showErrorToast(message, title = "Error") {
    showToast({type: "error", title, message, duration: 5200});
}
