#include "setup.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

int main() {
    std::map<std::string, std::string> form = {
        {"port", "18080"},
        {"secure_cookies", "1"},
        {"deploy_systemd_unit_dir", "/etc/systemd/system"},
        {"deploy_allowed_roots", "/home,/opt,/srv"},
        {"systemctl_bin", "/bin/systemctl"},
        {"certbot_bin", "/usr/bin/certbot"},
        {"python3_bin", "/usr/bin/python3"},
        {"npm_bin", "/usr/bin/npm"},
        {"node_bin", "/usr/bin/node"},
        {"go_bin", "/usr/bin/go"},
        {"curl_bin", "/usr/bin/curl"},
        {"cloudflare_zone_id", ""},
        {"cloudflare_api_token", ""},
        {"nginx_available_dir", "/etc/nginx/sites-available"},
        {"nginx_enabled_dir", "/etc/nginx/sites-enabled"},
        {"nginx_bin", "/usr/sbin/nginx"},
        {"nginx_reload_service", "nginx"},
        {"system_allowed_roots", "/home,/srv,/var/www"},
        {"terminal_shell", "/bin/bash"},
        {"terminal_run_as_user", "nobody"},
        {"terminal_run_as_group", "nogroup"},
        {"terminal_workdir", "/tmp"},
        {"terminal_max_sessions_per_user", "2"},
        {"terminal_idle_timeout_seconds", "900"},
        {"terminal_max_session_seconds", "7200"},
        {"codex_bin", "/usr/bin/codex"},
        {"codex_workdir", "/root/cuddlePanel"},
        {"codex_model", ""},
        {"codex_timeout_seconds", "180"}
    };

    std::string error;
    auto config = cuddle::first_run_config_from_form(form, &error);
    assert(config);
    assert(config->secure_cookies);
    assert(config->port == "18080");

    form["port"] = "99999";
    assert(!cuddle::first_run_config_from_form(form, &error));
    form["port"] = "18080";

    const std::filesystem::path path = "tmp-first-run.env";
    assert(cuddle::write_first_run_env_file(*config, path.string(), &error));

    std::ifstream file(path);
    assert(file.good());
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    assert(content.find("CUDDLEPANEL_PORT=18080") != std::string::npos);
    assert(content.find("CUDDLEPANEL_SECURE_COOKIES=1") != std::string::npos);
    assert(content.find("CUDDLEPANEL_DEPLOY_SYSTEMD_DIR=/etc/systemd/system") != std::string::npos);
    assert(content.find("CUDDLEPANEL_DEPLOY_ALLOWED_ROOTS=/home,/opt,/srv") != std::string::npos);
    assert(content.find("CUDDLEPANEL_CODEX_BIN=/usr/bin/codex") != std::string::npos);
    std::filesystem::remove(path);

    cuddle::apply_first_run_config(*config);
    const char* configured_port = std::getenv("CUDDLEPANEL_PORT");
    assert(configured_port && std::string(configured_port) == "18080");

    const auto dependencies = cuddle::first_run_dependency_status(*config);
    assert(!dependencies.empty());
    assert(dependencies.front().required);

    std::cout << "setup tests passed" << std::endl;
    return 0;
}
