import {postForm, requestJson} from "./core/api.js";
import {showErrorToast} from "./core/toast.js";

export function bindAuthForm(formId, url, nextPath) {
    const form = document.getElementById(formId);
    if (!form) {
        return;
    }
    const message = document.getElementById("message");
    form.addEventListener("submit", async (event) => {
        event.preventDefault();
        message.textContent = "";
        try {
            const payload = await postForm(url, form);
            window.location.assign(payload.next || nextPath);
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
            window.location.assign(payload.next || "/dashboard");
        } catch (error) {
            message.textContent = error.message;
            showErrorToast(error.message);
        }
    });
}
