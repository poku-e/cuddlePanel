#include "deploy_runner.h"

#include <array>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace cuddle {
namespace {

bool truthy(const std::string& value) {
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

bool valid_basic_token(const std::string& value, size_t max_length) {
    if (value.empty() || value.size() > max_length) {
        return false;
    }
    for (unsigned char c : value) {
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

DeployResult capture_command(const std::vector<std::string>& args) {
    std::array<char, 256> buffer{};
    std::string output;
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        return {false, "unable to execute command"};
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return {false, "unable to execute command"};
    }

    if (pid == 0) {
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv(argv[0], argv.data());
        _exit(127);
    }

    close(pipe_fds[1]);
    ssize_t read_count = 0;
    while ((read_count = read(pipe_fds[0], buffer.data(), buffer.size())) > 0) {
        output.append(buffer.data(), static_cast<size_t>(read_count));
    }
    close(pipe_fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (output.empty()) {
        output = ok ? "command completed successfully" : "command failed";
    }
    return {ok, output};
}

}

std::optional<DeployRequest> deploy_request_from_form(const std::map<std::string, std::string>& form) {
    DeployRequest request;
    auto get = [&form](const std::string& key) -> std::string {
        auto found = form.find(key);
        return found == form.end() ? "" : found->second;
    };

    request.domain = get("domain");
    request.port = get("port");
    request.user = get("user");
    request.project_root = get("project_root");
    request.service_desc = get("service_desc");
    request.email = get("email");
    request.site_name = get("site_name");
    request.upstream_host = get("upstream_host");
    request.skip_dns_check = truthy(get("skip_dns_check"));
    request.skip_certbot = truthy(get("skip_certbot"));
    request.force_overwrite_site = truthy(get("force_overwrite_site"));
    request.skip_service_start = truthy(get("skip_service_start"));
    return request;
}

bool valid_deploy_request(const DeployRequest& request, std::string* error_message) {
    auto fail = [error_message](const std::string& message) {
        if (error_message) {
            *error_message = message;
        }
        return false;
    };

    if (!valid_basic_token(request.domain, 255)) {
        return fail("invalid domain");
    }
    if (request.port.empty() || request.port.size() > 5) {
        return fail("invalid port");
    }
    for (unsigned char c : request.port) {
        if (!std::isdigit(c)) {
            return fail("invalid port");
        }
    }
    const int port = std::stoi(request.port);
    if (port < 1 || port > 65535) {
        return fail("invalid port");
    }
    if (!valid_basic_token(request.user, 64)) {
        return fail("invalid user");
    }
    if (request.project_root.empty() || request.project_root.size() > 512 ||
        request.project_root.front() != '/' || request.project_root.find('\0') != std::string::npos) {
        return fail("invalid project root");
    }
    if (request.service_desc.empty() || request.service_desc.size() > 200 ||
        request.service_desc.find('\0') != std::string::npos) {
        return fail("invalid service description");
    }
    if (!request.skip_certbot && (request.email.empty() || request.email.size() > 255)) {
        return fail("email is required unless skip certbot is enabled");
    }
    if (!request.email.empty()) {
        if (request.email.find('@') == std::string::npos || request.email.find('\0') != std::string::npos) {
            return fail("invalid email");
        }
    }
    if (!request.site_name.empty() && !valid_basic_token(request.site_name, 64)) {
        return fail("invalid site name");
    }
    if (request.upstream_host.empty() || request.upstream_host.size() > 255 ||
        request.upstream_host.find('\0') != std::string::npos) {
        return fail("invalid upstream host");
    }
    return true;
}

std::vector<std::string> build_deploy_args(const DeployRequest& request) {
    std::vector<std::string> args = {
        deploy_script_path(),
        "--non-interactive",
        "--domain", request.domain,
        "--port", request.port,
        "--user", request.user,
        "--project-root", request.project_root,
        "--service-desc", request.service_desc,
        "--upstream-host", request.upstream_host
    };

    if (!request.email.empty()) {
        args.insert(args.end(), {"--email", request.email});
    }
    if (!request.site_name.empty()) {
        args.insert(args.end(), {"--site-name", request.site_name});
    }
    if (request.skip_dns_check) {
        args.push_back("--skip-dns-check");
    }
    if (request.skip_certbot) {
        args.push_back("--skip-certbot");
    }
    if (request.force_overwrite_site) {
        args.push_back("--force-overwrite-site");
    }
    if (request.skip_service_start) {
        args.push_back("--skip-service-start");
    }
    return args;
}

std::string deploy_script_path() {
    const char* configured = std::getenv("CUDDLEPANEL_DEPLOY_SITE_BIN");
    return configured && *configured ? configured : "/usr/local/sbin/deploy-site";
}

DeployResult run_deploy_site(const DeployRequest& request) {
    std::string error;
    if (!valid_deploy_request(request, &error)) {
        return {false, error};
    }
    return capture_command(build_deploy_args(request));
}

}
