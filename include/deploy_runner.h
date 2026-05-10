#pragma once

#include <map>
#include <optional>
#include <string>

namespace cuddle {

enum class DeployStack {
    NodeJs,
    Go,
    Streamlit,
    PythonVite,
};

struct DeployRequest {
    std::string domain;
    std::string port;
    std::string user;
    std::string project_root;
    std::string service_desc;
    std::string email;
    std::string site_name;
    std::string upstream_host;
    std::string stack;
    std::string node_entry;
    std::string go_package;
    std::string go_binary_path;
    std::string streamlit_entry;
    std::string python_module;
    std::string python_backend_dir;
    std::string vite_root;
    std::string vite_dist_dir;
    std::string public_ip;
    std::string cloudflare_zone_id;
    std::string cloudflare_api_token;
    bool install_dependencies = false;
    bool run_build = false;
    bool enable_cloudflare_dns = false;
    bool cloudflare_proxied = false;
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
DeployStack deploy_stack_from_string(const std::string& value);
std::string deploy_stack_to_string(DeployStack stack);
DeployResult run_deploy_site(const DeployRequest& request);

}
