import {postForm} from "../core/api.js";
import {showErrorToast, showSuccessToast} from "../core/toast.js";

export function initDeployPage() {
    const form = document.getElementById("deployRunForm");
    if (!form) {
        return;
    }
    const message = document.getElementById("deployRunMessage");
    const output = document.getElementById("deployOutput");

    form.addEventListener("submit", async (event) => {
        event.preventDefault();
        message.textContent = "";
        output.textContent = "Running deploy helper...";
        try {
            const payload = await postForm("/api/deploy/run", form);
            output.textContent = payload.output;
            message.textContent = "Deploy completed.";
            message.className = "small text-success";
            showSuccessToast("Deploy completed.");
        } catch (error) {
            output.textContent = error.message;
            message.textContent = "Deploy failed.";
            message.className = "small text-danger";
            showErrorToast(error.message);
        }
    });

    const emailField = form.querySelector('[name="email"]');
    const skipCertbotField = form.querySelector('[name="skip_certbot"]');
    const syncEmailRequirement = () => {
        emailField.toggleAttribute("required", !skipCertbotField.checked);
    };
    skipCertbotField.addEventListener("change", syncEmailRequirement);
    syncEmailRequirement();
}
