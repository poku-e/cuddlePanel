import {bindAuthForm, bootTotpSetup} from "./js/auth.js";
import {bootDashboard} from "./js/dashboard.js";
import {initToasts} from "./js/core/toast.js";

function bootCurrentView() {
    const view = document.body.dataset.view;
    if (view === "login") {
        bindAuthForm("loginForm", "/api/login", "/dashboard");
        return;
    }
    if (view === "onboarding") {
        bindAuthForm("onboardingForm", "/api/onboarding", "/login");
        return;
    }
    if (view === "totp-setup") {
        bootTotpSetup();
        return;
    }
    if (view === "dashboard") {
        bootDashboard();
    }
}

initToasts();
bootCurrentView();
