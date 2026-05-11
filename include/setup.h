#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cuddle {

struct FirstRunConfig {
    std::string port;
    bool secure_cookies = false;
    std::string deploy_systemd_unit_dir;
    std::string deploy_allowed_roots;
    std::string systemctl_bin;
    std::string certbot_bin;
    std::string python3_bin;
    std::string npm_bin;
    std::string node_bin;
    std::string go_bin;
    std::string curl_bin;
    std::string cloudflare_zone_id;
    std::string cloudflare_api_token;
    std::string nginx_available_dir;
    std::string nginx_enabled_dir;
    std::string nginx_bin;
    std::string nginx_reload_service;
    std::string system_allowed_roots;
    std::string terminal_shell;
    std::string terminal_run_as_user;
    std::string terminal_run_as_group;
    std::string terminal_workdir;
    std::string terminal_max_sessions_per_user;
    std::string terminal_idle_timeout_seconds;
    std::string terminal_max_session_seconds;
    std::string codex_bin;
    std::string codex_workdir;
    std::string codex_model;
    std::string codex_timeout_seconds;
};

struct DependencyStatus {
    std::string name;
    std::string path;
    bool present = false;
    bool required = false;
    std::string details;
};

FirstRunConfig current_first_run_config();
std::optional<FirstRunConfig> first_run_config_from_form(const std::map<std::string, std::string>& form,
                                                         std::string* error_message);
bool write_first_run_env_file(const FirstRunConfig& config,
                              const std::string& path,
                              std::string* error_message = nullptr);
void apply_first_run_config(const FirstRunConfig& config);
std::vector<DependencyStatus> first_run_dependency_status(const FirstRunConfig& config);

}
