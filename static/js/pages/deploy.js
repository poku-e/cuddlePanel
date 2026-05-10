import {postForm} from "../core/api.js";
import {showErrorToast, showSuccessToast} from "../core/toast.js";

export function initDeployPage() {
    const form = document.getElementById("deployRunForm");
    if (!form) {
        return;
    }
    const message = document.getElementById("deployRunMessage");
    const output = document.getElementById("deployOutput");
    const stackField = form.querySelector('[name="stack"]');
    const cloudflareEnabledField = form.querySelector('[name="enable_cloudflare_dns"]');
    const publicIpField = form.querySelector('[name="public_ip"]');
    const emailField = form.querySelector('[name="email"]');
    const skipCertbotField = form.querySelector('[name="skip_certbot"]');
    const canManage = document.getElementById("deployPageState")?.dataset.canManage === "1";

    const switchDeployTab = (name) => {
        document.querySelectorAll("[data-deploy-tab]").forEach((button) => {
            const active = button.dataset.deployTab === name;
            button.classList.toggle("active", active);
            button.setAttribute("aria-selected", active ? "true" : "false");
        });
        document.querySelectorAll("[data-deploy-panel]").forEach((panel) => {
            const active = panel.dataset.deployPanel === name;
            panel.classList.toggle("active", active);
            panel.hidden = !active;
        });
    };

    const applyStackDefaults = () => {
        const stack = stackField.value;
        form.querySelectorAll("[data-stack-only]").forEach((section) => {
            const active = section.dataset.stackOnly === stack;
            section.style.display = active ? "grid" : "none";
            section.querySelectorAll("input").forEach((field) => {
                if (field.name === "node_entry") {
                    field.toggleAttribute("required", active);
                } else if (field.name === "go_package" || field.name === "go_binary_path") {
                    field.toggleAttribute("required", active);
                } else if (field.name === "streamlit_entry") {
                    field.toggleAttribute("required", active);
                } else if (field.name === "python_module" || field.name === "python_backend_dir" || field.name === "vite_root" || field.name === "vite_dist_dir") {
                    field.toggleAttribute("required", active);
                } else {
                    field.removeAttribute("required");
                }
            });
        });
    };

    const syncEmailRequirement = () => {
        emailField.toggleAttribute("required", !skipCertbotField.checked);
    };

    const syncCloudflareRequirement = () => {
        const enabled = cloudflareEnabledField.checked;
        publicIpField.toggleAttribute("required", enabled);
        form.querySelectorAll(".deploy-cloudflare-fields input, .deploy-cloudflare-fields input[type='checkbox']").forEach((field) => {
            field.toggleAttribute("disabled", !canManage || !enabled);
        });
        const cloudflareFields = document.querySelector(".deploy-cloudflare-fields");
        if (cloudflareFields) {
            cloudflareFields.hidden = !enabled;
        }
    };

    form.addEventListener("submit", async (event) => {
        event.preventDefault();
        message.textContent = "";
        output.textContent = "Running native deploy...";
        output.scrollIntoView({behavior: "smooth", block: "start"});
        try {
            const payload = await postForm("/api/deploy/run", form);
            output.textContent = payload.output;
            message.textContent = "Deploy completed.";
            message.className = "small text-success";
            showSuccessToast("Deploy completed.");
            output.scrollIntoView({behavior: "smooth", block: "start"});
        } catch (error) {
            output.textContent = error.message;
            message.textContent = "Deploy failed.";
            message.className = "small text-danger";
            showErrorToast(error.message);
            output.scrollIntoView({behavior: "smooth", block: "start"});
        }
    });

    document.querySelectorAll("[data-deploy-tab]").forEach((button) => {
        button.addEventListener("click", () => switchDeployTab(button.dataset.deployTab));
    });
    skipCertbotField.addEventListener("change", syncEmailRequirement);
    cloudflareEnabledField.addEventListener("change", syncCloudflareRequirement);
    stackField.addEventListener("change", applyStackDefaults);
    applyStackDefaults();
    syncEmailRequirement();
    syncCloudflareRequirement();
    switchDeployTab("app");
}
