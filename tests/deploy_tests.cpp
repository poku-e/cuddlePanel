#include "deploy_runner.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

std::string current_username() {
    passwd* pw = getpwuid(getuid());
    return pw && pw->pw_name ? pw->pw_name : "root";
}

}

int main() {
    cuddle::DeployRequest request;
    request.domain = "example.com";
    request.port = "8080";
    request.user = current_username();
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
    setenv("CUDDLEPANEL_DEPLOY_ALLOWED_ROOTS", "/tmp", 1);
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

    request.service_desc = "ok\nExecStartPre=/bin/sh";
    assert(!cuddle::valid_deploy_request(request, &error));
    request.service_desc = "Example service";

    request.go_package = ".\n/tmp";
    request.stack = "golang";
    assert(!cuddle::valid_deploy_request(request, &error));
    request.go_package = ".";

    request.stack = "nodejs";
    request.node_entry = "server.js\tExecStartPre";
    assert(!cuddle::valid_deploy_request(request, &error));
    request.node_entry = "server.js";

    request.stack = "python_vite";
    request.vite_root = "frontend\nother";
    assert(!cuddle::valid_deploy_request(request, &error));
    request.vite_root = "frontend";

    setenv("CUDDLEPANEL_DEPLOY_ALLOWED_ROOTS", "/srv", 1);
    assert(!cuddle::valid_deploy_request(request, &error));
    setenv("CUDDLEPANEL_DEPLOY_ALLOWED_ROOTS", "/tmp", 1);

    const auto symlink_root = std::filesystem::temp_directory_path() / "cuddlepanel-deploy-symlink-root";
    const auto outside_root = std::filesystem::temp_directory_path() / "cuddlepanel-deploy-symlink-outside";
    std::filesystem::remove_all(symlink_root);
    std::filesystem::remove_all(outside_root);
    std::filesystem::create_directories(symlink_root / "backend");
    std::filesystem::create_directories(outside_root);
    std::ofstream(symlink_root / "requirements.txt").close();
    std::filesystem::create_directory_symlink(outside_root, symlink_root / "frontend");

    request.project_root = symlink_root.string();
    request.stack = "python_vite";
    request.install_dependencies = true;
    request.run_build = false;
    setenv("CUDDLEPANEL_DEPLOY_ALLOWED_ROOTS", symlink_root.parent_path().string().c_str(), 1);
    const auto symlink_result = cuddle::run_deploy_site(request);
    assert(!symlink_result.ok);
    assert(symlink_result.output.find("outside project root") != std::string::npos);

    std::filesystem::remove_all(symlink_root);
    std::filesystem::remove_all(outside_root);

    std::cout << "deploy tests passed" << std::endl;
    return 0;
}
