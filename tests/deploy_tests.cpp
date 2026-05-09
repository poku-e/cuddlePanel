#include "deploy_runner.h"

#include <cassert>
#include <cstdlib>
#include <iostream>

int main() {
    cuddle::DeployRequest request;
    request.domain = "example.com";
    request.port = "8080";
    request.user = "example";
    request.project_root = "/home/example/app";
    request.service_desc = "Example service";
    request.email = "admin@example.com";
    request.site_name = "example";
    request.upstream_host = "127.0.0.1";
    request.skip_dns_check = true;
    request.skip_certbot = false;
    request.force_overwrite_site = true;
    request.skip_service_start = false;

    std::string error;
    assert(cuddle::valid_deploy_request(request, &error));

    auto args = cuddle::build_deploy_args(request);
    assert(args.size() >= 12);
    assert(args[1] == "--non-interactive");

    request.email.clear();
    request.skip_certbot = false;
    assert(!cuddle::valid_deploy_request(request, &error));
    request.skip_certbot = true;
    assert(cuddle::valid_deploy_request(request, &error));

    setenv("CUDDLEPANEL_DEPLOY_SITE_BIN", "/bin/echo", 1);
    request.email = "admin@example.com";
    request.skip_certbot = false;
    const auto result = cuddle::run_deploy_site(request);
    assert(result.ok);
    assert(result.output.find("--domain") != std::string::npos);

    std::cout << "deploy tests passed" << std::endl;
    return 0;
}
