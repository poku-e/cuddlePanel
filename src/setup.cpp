#include "setup.h"

#include "deploy_runner.h"
#include "codex_runner.h"
#include "nginx_store.h"
#include "system_admin.h"
#include "terminal_manager.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace cuddle {
namespace {

std::string env_or_default(const char* name, const std::string& fallback) {
    const char* configured = std::getenv(name);
    return configured && *configured ? configured : fallback;
}

bool truthy(const std::string& value) {
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

bool valid_basic_token(const std::string& value, std::size_t max_length) {
    if (value.empty() || value.size() > max_length) {
        return false;
    }
    for (unsigned char c : value) {
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-' || c == '@')) {
            return false;
        }
    }
    return true;
}

bool valid_codex_model_token(const std::string& value) {
    if (value.empty() || value.size() > 128) {
        return value.empty();
    }
    for (unsigned char c : value) {
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-' || c == ':' || c == '/')) {
            return false;
        }
    }
    return true;
}

bool valid_absolute_path_field(const std::string& value, std::size_t max_length) {
    return !value.empty() &&
           value.size() <= max_length &&
           value.front() == '/' &&
           value.find('\0') == std::string::npos;
}

bool validate_numeric_string(const std::string& value,
                             std::uint32_t min_value,
                             std::uint32_t max_value,
                             std::string* error_message,
                             const std::string& label) {
    if (value.empty()) {
        if (error_message) {
            *error_message = label + " is required";
        }
        return false;
    }
    for (unsigned char c : value) {
        if (!std::isdigit(c)) {
            if (error_message) {
                *error_message = "invalid " + label;
            }
            return false;
        }
    }
    const unsigned long parsed = std::strtoul(value.c_str(), nullptr, 10);
    if (parsed < min_value || parsed > max_value) {
        if (error_message) {
            *error_message = "invalid " + label;
        }
        return false;
    }
    return true;
}

bool validate_first_run_config(const FirstRunConfig& config, std::string* error_message) {
    if (!validate_numeric_string(config.port, 1, 65535, error_message, "port")) {
        return false;
    }
    if (!valid_absolute_path_field(config.deploy_site_bin, 512)) {
        if (error_message) {
            *error_message = "invalid deploy helper path";
        }
        return false;
    }
    if (!valid_absolute_path_field(config.nginx_available_dir, 512)) {
        if (error_message) {
            *error_message = "invalid nginx available directory";
        }
        return false;
    }
    if (!valid_absolute_path_field(config.nginx_enabled_dir, 512)) {
        if (error_message) {
            *error_message = "invalid nginx enabled directory";
        }
        return false;
    }
    if (!valid_absolute_path_field(config.nginx_bin, 512)) {
        if (error_message) {
            *error_message = "invalid nginx binary path";
        }
        return false;
    }
    if (!valid_basic_token(config.nginx_reload_service, 128)) {
        if (error_message) {
            *error_message = "invalid nginx reload service";
        }
        return false;
    }
    if (config.system_allowed_roots.empty() || config.system_allowed_roots.size() > 1024) {
        if (error_message) {
            *error_message = "invalid allowed system roots";
        }
        return false;
    }
    std::stringstream roots(config.system_allowed_roots);
    std::string root;
    bool has_root = false;
    while (std::getline(roots, root, ',')) {
        if (root.empty() || !valid_absolute_path_field(root, 512)) {
            if (error_message) {
                *error_message = "invalid allowed system roots";
            }
            return false;
        }
        has_root = true;
    }
    if (!has_root) {
        if (error_message) {
            *error_message = "invalid allowed system roots";
        }
        return false;
    }
    if (!valid_absolute_path_field(config.terminal_shell, 512)) {
        if (error_message) {
            *error_message = "invalid terminal shell";
        }
        return false;
    }
    if (!valid_basic_token(config.terminal_run_as_user, 64)) {
        if (error_message) {
            *error_message = "invalid terminal run user";
        }
        return false;
    }
    if (!valid_basic_token(config.terminal_run_as_group, 64)) {
        if (error_message) {
            *error_message = "invalid terminal run group";
        }
        return false;
    }
    if (!valid_absolute_path_field(config.terminal_workdir, 512)) {
        if (error_message) {
            *error_message = "invalid terminal working directory";
        }
        return false;
    }
    if (!validate_numeric_string(config.terminal_max_sessions_per_user, 1, 100, error_message, "terminal max sessions")) {
        return false;
    }
    if (!validate_numeric_string(config.terminal_idle_timeout_seconds, 1, 86400, error_message, "terminal idle timeout")) {
        return false;
    }
    if (!validate_numeric_string(config.terminal_max_session_seconds, 1, 604800, error_message, "terminal max runtime")) {
        return false;
    }
    if (!valid_absolute_path_field(config.codex_bin, 512)) {
        if (error_message) {
            *error_message = "invalid codex binary path";
        }
        return false;
    }
    if (!valid_absolute_path_field(config.codex_workdir, 512)) {
        if (error_message) {
            *error_message = "invalid codex working directory";
        }
        return false;
    }
    if (!valid_codex_model_token(config.codex_model)) {
        if (error_message) {
            *error_message = "invalid codex model";
        }
        return false;
    }
    if (!validate_numeric_string(config.codex_timeout_seconds, 10, 3600, error_message, "codex timeout")) {
        return false;
    }
    return true;
}

std::string env_line(const std::string& key, const std::string& value) {
    return key + "=" + value + "\n";
}

DependencyStatus command_dependency(const std::string& name,
                                   const std::string& path,
                                   bool required) {
    DependencyStatus dependency;
    dependency.name = name;
    dependency.path = path;
    dependency.required = required;
    dependency.present = valid_absolute_path_field(path, 512) && access(path.c_str(), X_OK) == 0;
    dependency.details = dependency.present ? "available" : "missing or not executable";
    return dependency;
}

}

FirstRunConfig current_first_run_config() {
    FirstRunConfig config;
    config.port = env_or_default("CUDDLEPANEL_PORT", "8080");
    config.secure_cookies = std::getenv("CUDDLEPANEL_SECURE_COOKIES") != nullptr;
    config.deploy_site_bin = deploy_script_path();
    config.nginx_available_dir = env_or_default("CUDDLEPANEL_NGINX_AVAILABLE_DIR", "/etc/nginx/sites-available");
    config.nginx_enabled_dir = env_or_default("CUDDLEPANEL_NGINX_ENABLED_DIR", "/etc/nginx/sites-enabled");
    config.nginx_bin = env_or_default("CUDDLEPANEL_NGINX_BIN", "/usr/sbin/nginx");
    config.nginx_reload_service = env_or_default("CUDDLEPANEL_NGINX_RELOAD_SERVICE", "nginx");
    config.system_allowed_roots = env_or_default("CUDDLEPANEL_SYSTEM_ALLOWED_ROOTS", "/home,/srv,/var/www");
    const auto terminal = terminal_runtime_policy();
    config.terminal_shell = terminal.shell_path;
    config.terminal_run_as_user = terminal.run_as_user;
    config.terminal_run_as_group = terminal.run_as_group;
    config.terminal_workdir = terminal.working_directory;
    config.terminal_max_sessions_per_user = std::to_string(terminal.max_sessions_per_user);
    config.terminal_idle_timeout_seconds = std::to_string(terminal.idle_timeout_seconds);
    config.terminal_max_session_seconds = std::to_string(terminal.max_session_seconds);
    const auto codex = codex_runtime_config();
    config.codex_bin = codex.binary_path;
    config.codex_workdir = codex.working_directory;
    config.codex_model = codex.model;
    config.codex_timeout_seconds = std::to_string(codex.timeout_seconds);
    return config;
}

std::optional<FirstRunConfig> first_run_config_from_form(const std::map<std::string, std::string>& form,
                                                         std::string* error_message) {
    auto get = [&form](const std::string& key) -> std::string {
        auto found = form.find(key);
        return found == form.end() ? "" : found->second;
    };

    FirstRunConfig config;
    config.port = get("port");
    config.secure_cookies = truthy(get("secure_cookies"));
    config.deploy_site_bin = get("deploy_site_bin");
    config.nginx_available_dir = get("nginx_available_dir");
    config.nginx_enabled_dir = get("nginx_enabled_dir");
    config.nginx_bin = get("nginx_bin");
    config.nginx_reload_service = get("nginx_reload_service");
    config.system_allowed_roots = get("system_allowed_roots");
    config.terminal_shell = get("terminal_shell");
    config.terminal_run_as_user = get("terminal_run_as_user");
    config.terminal_run_as_group = get("terminal_run_as_group");
    config.terminal_workdir = get("terminal_workdir");
    config.terminal_max_sessions_per_user = get("terminal_max_sessions_per_user");
    config.terminal_idle_timeout_seconds = get("terminal_idle_timeout_seconds");
    config.terminal_max_session_seconds = get("terminal_max_session_seconds");
    config.codex_bin = get("codex_bin");
    config.codex_workdir = get("codex_workdir");
    config.codex_model = get("codex_model");
    config.codex_timeout_seconds = get("codex_timeout_seconds");

    if (!validate_first_run_config(config, error_message)) {
        return std::nullopt;
    }
    return config;
}

bool write_first_run_env_file(const FirstRunConfig& config,
                              const std::string& path,
                              std::string* error_message) {
    if (!validate_first_run_config(config, error_message)) {
        return false;
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file.good()) {
        if (error_message) {
            *error_message = "unable to write .env";
        }
        return false;
    }

    file << "# cuddlePanel runtime environment\n";
    file << "# This file is auto-loaded by the server from the current working directory when present.\n";
    file << "# Existing shell-exported environment variables take precedence over values here.\n\n";
    file << "# Network and cookies\n";
    file << env_line("CUDDLEPANEL_PORT", config.port);
    file << env_line("CUDDLEPANEL_SECURE_COOKIES", config.secure_cookies ? "1" : "");
    file << "\n# Deploy helper\n";
    file << env_line("CUDDLEPANEL_DEPLOY_SITE_BIN", config.deploy_site_bin);
    file << "\n# Nginx management\n";
    file << env_line("CUDDLEPANEL_NGINX_AVAILABLE_DIR", config.nginx_available_dir);
    file << env_line("CUDDLEPANEL_NGINX_ENABLED_DIR", config.nginx_enabled_dir);
    file << env_line("CUDDLEPANEL_NGINX_BIN", config.nginx_bin);
    file << env_line("CUDDLEPANEL_NGINX_RELOAD_SERVICE", config.nginx_reload_service);
    file << "\n# Constrained roots for chown/chmod actions\n";
    file << env_line("CUDDLEPANEL_SYSTEM_ALLOWED_ROOTS", config.system_allowed_roots);
    file << "\n# Terminal runtime policy\n";
    file << env_line("CUDDLEPANEL_TERMINAL_SHELL", config.terminal_shell);
    file << env_line("CUDDLEPANEL_TERMINAL_RUN_AS_USER", config.terminal_run_as_user);
    file << env_line("CUDDLEPANEL_TERMINAL_RUN_AS_GROUP", config.terminal_run_as_group);
    file << env_line("CUDDLEPANEL_TERMINAL_WORKDIR", config.terminal_workdir);
    file << env_line("CUDDLEPANEL_TERMINAL_MAX_SESSIONS_PER_USER", config.terminal_max_sessions_per_user);
    file << env_line("CUDDLEPANEL_TERMINAL_IDLE_TIMEOUT_SECONDS", config.terminal_idle_timeout_seconds);
    file << env_line("CUDDLEPANEL_TERMINAL_MAX_SESSION_SECONDS", config.terminal_max_session_seconds);
    file << "\n# Codex runner\n";
    file << env_line("CUDDLEPANEL_CODEX_BIN", config.codex_bin);
    file << env_line("CUDDLEPANEL_CODEX_WORKDIR", config.codex_workdir);
    file << env_line("CUDDLEPANEL_CODEX_MODEL", config.codex_model);
    file << env_line("CUDDLEPANEL_CODEX_TIMEOUT_SECONDS", config.codex_timeout_seconds);
    file.close();
    if (!file) {
        if (error_message) {
            *error_message = "unable to write .env";
        }
        return false;
    }
    return true;
}

void apply_first_run_config(const FirstRunConfig& config) {
    setenv("CUDDLEPANEL_PORT", config.port.c_str(), 1);
    if (config.secure_cookies) {
        setenv("CUDDLEPANEL_SECURE_COOKIES", "1", 1);
    } else {
        unsetenv("CUDDLEPANEL_SECURE_COOKIES");
    }
    setenv("CUDDLEPANEL_DEPLOY_SITE_BIN", config.deploy_site_bin.c_str(), 1);
    setenv("CUDDLEPANEL_NGINX_AVAILABLE_DIR", config.nginx_available_dir.c_str(), 1);
    setenv("CUDDLEPANEL_NGINX_ENABLED_DIR", config.nginx_enabled_dir.c_str(), 1);
    setenv("CUDDLEPANEL_NGINX_BIN", config.nginx_bin.c_str(), 1);
    setenv("CUDDLEPANEL_NGINX_RELOAD_SERVICE", config.nginx_reload_service.c_str(), 1);
    setenv("CUDDLEPANEL_SYSTEM_ALLOWED_ROOTS", config.system_allowed_roots.c_str(), 1);
    setenv("CUDDLEPANEL_TERMINAL_SHELL", config.terminal_shell.c_str(), 1);
    setenv("CUDDLEPANEL_TERMINAL_RUN_AS_USER", config.terminal_run_as_user.c_str(), 1);
    setenv("CUDDLEPANEL_TERMINAL_RUN_AS_GROUP", config.terminal_run_as_group.c_str(), 1);
    setenv("CUDDLEPANEL_TERMINAL_WORKDIR", config.terminal_workdir.c_str(), 1);
    setenv("CUDDLEPANEL_TERMINAL_MAX_SESSIONS_PER_USER", config.terminal_max_sessions_per_user.c_str(), 1);
    setenv("CUDDLEPANEL_TERMINAL_IDLE_TIMEOUT_SECONDS", config.terminal_idle_timeout_seconds.c_str(), 1);
    setenv("CUDDLEPANEL_TERMINAL_MAX_SESSION_SECONDS", config.terminal_max_session_seconds.c_str(), 1);
    setenv("CUDDLEPANEL_CODEX_BIN", config.codex_bin.c_str(), 1);
    setenv("CUDDLEPANEL_CODEX_WORKDIR", config.codex_workdir.c_str(), 1);
    setenv("CUDDLEPANEL_CODEX_MODEL", config.codex_model.c_str(), 1);
    setenv("CUDDLEPANEL_CODEX_TIMEOUT_SECONDS", config.codex_timeout_seconds.c_str(), 1);
}

std::vector<DependencyStatus> first_run_dependency_status(const FirstRunConfig& config) {
    std::vector<DependencyStatus> dependencies;
    dependencies.push_back(command_dependency("Terminal shell", config.terminal_shell, true));
    dependencies.push_back(command_dependency("Deploy helper", config.deploy_site_bin, false));
    dependencies.push_back(command_dependency("Nginx binary", config.nginx_bin, false));
    dependencies.push_back(command_dependency("useradd", env_or_default("CUDDLEPANEL_USERADD_BIN", "/usr/sbin/useradd"), false));
    dependencies.push_back(command_dependency("passwd", env_or_default("CUDDLEPANEL_PASSWD_BIN", "/usr/bin/passwd"), false));
    dependencies.push_back(command_dependency("usermod", env_or_default("CUDDLEPANEL_USERMOD_BIN", "/usr/sbin/usermod"), false));
    dependencies.push_back(command_dependency("gpasswd", env_or_default("CUDDLEPANEL_GPASSWD_BIN", "/usr/bin/gpasswd"), false));
    dependencies.push_back(command_dependency("chown", env_or_default("CUDDLEPANEL_CHOWN_BIN", "/bin/chown"), false));
    dependencies.push_back(command_dependency("chmod", env_or_default("CUDDLEPANEL_CHMOD_BIN", "/bin/chmod"), false));
    dependencies.push_back(command_dependency("Codex CLI", config.codex_bin, false));

    DependencyStatus systemctl;
    systemctl.name = "systemctl";
    systemctl.path = "/bin/systemctl";
    systemctl.required = false;
    systemctl.present = access(systemctl.path.c_str(), X_OK) == 0 || access("/usr/bin/systemctl", X_OK) == 0;
    systemctl.details = systemctl.present ? "available" : "missing or not executable";
    dependencies.push_back(systemctl);
    return dependencies;
}

}
