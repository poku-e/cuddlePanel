#include "deploy_runner.h"

#include <cassert>
#include <cstdlib>
#include <iostream>

int main() {
    cuddle::DeployRequest request;
    request.domain = "example.com";
    request.port = "8080";
    request.user = "example";
    request.project_root = "/tmp";
    request.service_desc = "Example service";
    request.email = "admin@example.com";
    request.site_name = "example";
    request.upstream_host = "127.0.0.1";
    request.stack = "golang";
    request.go_package = ".";
    request.go_binary_path = "bin/server";
    request.skip_certbot = false;
    request.force_overwrite_site = true;
    request.skip_service_start = false;

    std::string error;
    assert(cuddle::valid_deploy_request(request, &error));

    request.email.clear();
    request.skip_certbot = false;
    assert(!cuddle::valid_deploy_request(request, &error));
    request.skip_certbot = true;
    assert(cuddle::valid_deploy_request(request, &error));

    request.email = "admin@example.com";
    request.skip_certbot = false;
    request.run_build = false;
    request.skip_service_start = true;
    setenv("CUDDLEPANEL_DEPLOY_SYSTEMD_DIR", "/tmp/cuddlepanel-systemd-tests", 1);
    setenv("CUDDLEPANEL_NGINX_AVAILABLE_DIR", "/tmp/cuddlepanel-nginx-available-tests", 1);
    setenv("CUDDLEPANEL_NGINX_ENABLED_DIR", "/tmp/cuddlepanel-nginx-enabled-tests", 1);
    setenv("CUDDLEPANEL_SYSTEMCTL_BIN", "/bin/echo", 1);
    setenv("CUDDLEPANEL_NGINX_BIN", "/bin/echo", 1);
    const auto result = cuddle::run_deploy_site(request);
    assert(result.ok);
    assert(result.output.find("[Systemd]") != std::string::npos);
    assert(result.output.find("[Nginx]") != std::string::npos);

    request.stack = "streamlit";
    request.streamlit_entry = "app.py";
    assert(cuddle::valid_deploy_request(request, &error));

    request.stack = "nodejs";
    request.node_entry = "server.js";
    assert(cuddle::valid_deploy_request(request, &error));

    request.stack = "python_vite";
    request.python_module = "app:app";
    request.python_backend_dir = "backend";
    request.vite_root = "frontend";
    request.vite_dist_dir = "dist";
    assert(cuddle::valid_deploy_request(request, &error));

    std::cout << "deploy tests passed" << std::endl;
    return 0;
}
