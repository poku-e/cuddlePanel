#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cuddle {

struct DeployRequest {
    std::string domain;
    std::string port;
    std::string user;
    std::string project_root;
    std::string service_desc;
    std::string email;
    std::string site_name;
    std::string upstream_host;
    bool skip_dns_check = false;
    bool skip_certbot = false;
    bool force_overwrite_site = false;
    bool skip_service_start = false;
};

struct DeployResult {
    bool ok = false;
    std::string output;
};

std::optional<DeployRequest> deploy_request_from_form(const std::map<std::string, std::string>& form);
bool valid_deploy_request(const DeployRequest& request, std::string* error_message = nullptr);
std::vector<std::string> build_deploy_args(const DeployRequest& request);
std::string deploy_script_path();
DeployResult run_deploy_site(const DeployRequest& request);

}
