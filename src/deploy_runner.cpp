#include "deploy_runner.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace cuddle {
namespace {

struct CommandResult {
    bool ok = false;
    int exit_code = -1;
    std::string output;
};

struct CloudflareRecord {
    std::string id;
    bool found = false;
};

std::string env_or_default(const char* name, const std::string& fallback) {
    const char* configured = std::getenv(name);
    return configured && *configured ? configured : fallback;
}

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

bool valid_hostname_like(const std::string& value, size_t max_length) {
    if (value.empty() || value.size() > max_length) {
        return false;
    }
    for (unsigned char c : value) {
        if (!(std::isalnum(c) || c == '.' || c == '-' || c == ':')) {
            return false;
        }
    }
    return true;
}

bool valid_absolute_path(const std::string& value, size_t max_length = 512) {
    return !value.empty() &&
           value.size() <= max_length &&
           value.front() == '/' &&
           value.find('\0') == std::string::npos;
}

bool valid_relative_path(const std::string& value, size_t max_length = 256) {
    if (value.empty() || value.size() > max_length || value.find('\0') != std::string::npos) {
        return false;
    }
    if (value.front() == '/' || value.find("..") != std::string::npos) {
        return false;
    }
    return true;
}

bool valid_python_module(const std::string& value) {
    if (value.empty() || value.size() > 200 || value.find('\0') != std::string::npos) {
        return false;
    }
    for (unsigned char c : value) {
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == ':')) {
            return false;
        }
    }
    return true;
}

bool valid_ipv4(const std::string& value) {
    std::stringstream stream(value);
    std::string part;
    int count = 0;
    while (std::getline(stream, part, '.')) {
        if (part.empty() || part.size() > 3) {
            return false;
        }
        for (unsigned char c : part) {
            if (!std::isdigit(c)) {
                return false;
            }
        }
        const int octet = std::stoi(part);
        if (octet < 0 || octet > 255) {
            return false;
        }
        ++count;
    }
    return count == 4;
}

std::string json_escape_local(const std::string& value) {
    std::string out;
    for (char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string trim_copy(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::filesystem::path resolve_child_path(const std::string& base, const std::string& child) {
    if (child.empty()) {
        return std::filesystem::path(base);
    }
    return std::filesystem::path(base) / child;
}

CommandResult run_command(const std::vector<std::string>& args, const std::string& cwd = {}) {
    CommandResult result;
    if (args.empty()) {
        result.output = "unable to execute empty command";
        return result;
    }

    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        result.output = "unable to execute command";
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        result.output = "unable to execute command";
        return result;
    }

    if (pid == 0) {
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
            _exit(126);
        }
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
    std::array<char, 4096> buffer{};
    ssize_t read_count = 0;
    while ((read_count = read(pipe_fds[0], buffer.data(), buffer.size())) > 0) {
        result.output.append(buffer.data(), static_cast<size_t>(read_count));
    }
    close(pipe_fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    result.ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (result.output.empty()) {
        result.output = result.ok ? "command completed successfully" : "command failed";
    }
    return result;
}

bool ensure_parent_dir(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return !ec;
}

bool write_text_file(const std::filesystem::path& path, const std::string& content, bool overwrite) {
    if (!overwrite && std::filesystem::exists(path)) {
        return false;
    }
    if (!ensure_parent_dir(path)) {
        return false;
    }
    std::ofstream file(path, std::ios::trunc);
    if (!file.good()) {
        return false;
    }
    file << content;
    file.close();
    return static_cast<bool>(file);
}

std::string service_unit_dir() {
    return env_or_default("CUDDLEPANEL_DEPLOY_SYSTEMD_DIR", "/etc/systemd/system");
}

std::string systemctl_bin() {
    return env_or_default("CUDDLEPANEL_SYSTEMCTL_BIN", "/bin/systemctl");
}

std::string certbot_bin() {
    return env_or_default("CUDDLEPANEL_CERTBOT_BIN", "/usr/bin/certbot");
}

std::string python3_bin() {
    return env_or_default("CUDDLEPANEL_PYTHON3_BIN", "/usr/bin/python3");
}

std::string npm_bin() {
    return env_or_default("CUDDLEPANEL_NPM_BIN", "/usr/bin/npm");
}

std::string node_bin() {
    return env_or_default("CUDDLEPANEL_NODE_BIN", "/usr/bin/node");
}

std::string go_bin() {
    return env_or_default("CUDDLEPANEL_GO_BIN", "/usr/bin/go");
}

std::string curl_bin() {
    return env_or_default("CUDDLEPANEL_CURL_BIN", "/usr/bin/curl");
}

std::string nginx_bin() {
    return env_or_default("CUDDLEPANEL_NGINX_BIN", "/usr/sbin/nginx");
}

std::string nginx_available_dir() {
    return env_or_default("CUDDLEPANEL_NGINX_AVAILABLE_DIR", "/etc/nginx/sites-available");
}

std::string nginx_enabled_dir() {
    return env_or_default("CUDDLEPANEL_NGINX_ENABLED_DIR", "/etc/nginx/sites-enabled");
}

std::string nginx_reload_service() {
    return env_or_default("CUDDLEPANEL_NGINX_RELOAD_SERVICE", "nginx");
}

std::string default_cloudflare_zone_id() {
    return env_or_default("CUDDLEPANEL_CLOUDFLARE_ZONE_ID", "");
}

std::string default_cloudflare_api_token() {
    return env_or_default("CUDDLEPANEL_CLOUDFLARE_API_TOKEN", "");
}

std::string deploy_service_name(const DeployRequest& request) {
    std::string name = request.site_name.empty() ? request.domain : request.site_name;
    std::replace(name.begin(), name.end(), '.', '-');
    return name;
}

DeployStack deploy_stack_from_string_impl(const std::string& value) {
    if (value == "streamlit") {
        return DeployStack::Streamlit;
    }
    if (value == "python_vite") {
        return DeployStack::PythonVite;
    }
    if (value == "nodejs") {
        return DeployStack::NodeJs;
    }
    return DeployStack::Go;
}

std::string stack_value(DeployStack stack) {
    switch (stack) {
        case DeployStack::NodeJs: return "nodejs";
        case DeployStack::Go: return "golang";
        case DeployStack::Streamlit: return "streamlit";
        case DeployStack::PythonVite: return "python_vite";
    }
    return "golang";
}

std::string build_unit_content(const DeployRequest& request) {
    const auto stack = deploy_stack_from_string_impl(request.stack);
    const std::string service_name = deploy_service_name(request);
    const std::filesystem::path project_root(request.project_root);
    std::ostringstream out;
    out << "[Unit]\n"
        << "Description=" << request.service_desc << "\n"
        << "After=network.target\n\n"
        << "[Service]\n"
        << "Type=simple\n"
        << "User=" << request.user << "\n"
        << "Group=" << request.user << "\n"
        << "WorkingDirectory=" << project_root.string() << "\n"
        << "Environment=PORT=" << request.port << "\n"
        << "Environment=HOST=127.0.0.1\n";

    if (stack == DeployStack::NodeJs) {
        out << "ExecStart=" << node_bin() << " " << (project_root / request.node_entry).string() << "\n";
    } else if (stack == DeployStack::Go) {
        out << "ExecStart=" << (project_root / request.go_binary_path).string() << "\n";
    } else if (stack == DeployStack::Streamlit) {
        out << "ExecStart=" << (project_root / ".venv/bin/streamlit").string()
            << " run " << (project_root / request.streamlit_entry).string()
            << " --server.port " << request.port
            << " --server.address 127.0.0.1\n";
    } else if (stack == DeployStack::PythonVite) {
        const auto backend_dir = resolve_child_path(request.project_root, request.python_backend_dir);
        out << "ExecStart=" << (project_root / ".venv/bin/gunicorn").string()
            << " --chdir " << backend_dir.string()
            << " " << request.python_module
            << " --bind 127.0.0.1:" << request.port << "\n";
    }

    out << "Restart=always\n"
        << "RestartSec=5\n"
        << "StandardOutput=journal\n"
        << "StandardError=journal\n\n"
        << "[Install]\n"
        << "WantedBy=multi-user.target\n";
    (void)service_name;
    return out.str();
}

std::string build_nginx_config(const DeployRequest& request) {
    const auto stack = deploy_stack_from_string_impl(request.stack);
    std::ostringstream out;
    out << "server {\n"
        << "    listen 80;\n"
        << "    server_name " << request.domain << ";\n\n";

    if (stack == DeployStack::PythonVite) {
        const auto vite_root = resolve_child_path(request.project_root, request.vite_root);
        const auto vite_dist = resolve_child_path(vite_root.string(), request.vite_dist_dir);
        out << "    root " << vite_dist.string() << ";\n"
            << "    index index.html;\n\n"
            << "    location /api/ {\n"
            << "        proxy_pass http://" << request.upstream_host << ":" << request.port << "/;\n"
            << "        proxy_http_version 1.1;\n"
            << "        proxy_set_header Host $host;\n"
            << "        proxy_set_header X-Real-IP $remote_addr;\n"
            << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n"
            << "        proxy_set_header X-Forwarded-Proto $scheme;\n"
            << "    }\n\n"
            << "    location / {\n"
            << "        try_files $uri $uri/ /index.html;\n"
            << "    }\n";
    } else {
        out << "    location / {\n"
            << "        proxy_pass http://" << request.upstream_host << ":" << request.port << ";\n"
            << "        proxy_http_version 1.1;\n"
            << "        proxy_set_header Host $host;\n"
            << "        proxy_set_header X-Real-IP $remote_addr;\n"
            << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n"
            << "        proxy_set_header X-Forwarded-Proto $scheme;\n"
            << "        proxy_set_header Upgrade $http_upgrade;\n"
            << "        proxy_set_header Connection \"upgrade\";\n"
            << "    }\n";
    }
    out << "}\n";
    return out.str();
}

bool append_result(std::ostringstream& out, const std::string& title, const CommandResult& result) {
    out << "\n[" << title << "]\n" << result.output;
    if (!result.output.empty() && result.output.back() != '\n') {
        out << '\n';
    }
    return result.ok;
}

CommandResult build_stack(const DeployRequest& request) {
    const auto stack = deploy_stack_from_string_impl(request.stack);
    const std::filesystem::path project_root(request.project_root);

    if (stack == DeployStack::NodeJs) {
        if (request.install_dependencies) {
            auto install = run_command({npm_bin(), "ci"}, project_root.string());
            if (!install.ok) {
                return install;
            }
            if (request.run_build) {
                return run_command({npm_bin(), "run", "build"}, project_root.string());
            }
            return install;
        }
        if (request.run_build) {
            return run_command({npm_bin(), "run", "build"}, project_root.string());
        }
        return {true, 0, "nodejs build steps skipped"};
    }

    if (stack == DeployStack::Go) {
        if (!request.run_build) {
            return {true, 0, "golang build skipped"};
        }
        return run_command({go_bin(), "build", "-o", (project_root / request.go_binary_path).string(), request.go_package},
                           project_root.string());
    }

    if (stack == DeployStack::Streamlit) {
        auto venv = run_command({python3_bin(), "-m", "venv", (project_root / ".venv").string()}, project_root.string());
        if (!venv.ok) {
            return venv;
        }
        if (request.install_dependencies) {
            return run_command({(project_root / ".venv/bin/pip").string(), "install", "-r", (project_root / "requirements.txt").string()},
                               project_root.string());
        }
        return venv;
    }

    auto venv = run_command({python3_bin(), "-m", "venv", (project_root / ".venv").string()}, project_root.string());
    if (!venv.ok) {
        return venv;
    }
    if (request.install_dependencies) {
        auto pip = run_command({(project_root / ".venv/bin/pip").string(), "install", "-r", (project_root / "requirements.txt").string()},
                               project_root.string());
        if (!pip.ok) {
            return pip;
        }
        auto npm_install = run_command({npm_bin(), "ci"}, resolve_child_path(request.project_root, request.vite_root).string());
        if (!npm_install.ok) {
            return npm_install;
        }
    }
    if (request.run_build) {
        return run_command({npm_bin(), "run", "build"}, resolve_child_path(request.project_root, request.vite_root).string());
    }
    return venv;
}

CommandResult configure_nginx(const DeployRequest& request) {
    const std::string filename = request.site_name.empty() ? deploy_service_name(request) : request.site_name;
    const auto available = std::filesystem::path(nginx_available_dir()) / filename;
    const auto enabled = std::filesystem::path(nginx_enabled_dir()) / filename;

    if (!write_text_file(available, build_nginx_config(request), request.force_overwrite_site)) {
        return {false, 1, "unable to write nginx site config"};
    }

    std::error_code ec;
    if (std::filesystem::exists(enabled, ec)) {
        std::filesystem::remove(enabled, ec);
    }
    std::filesystem::create_symlink(available, enabled, ec);
    if (ec) {
        return {false, 1, "unable to enable nginx site"};
    }

    auto test = run_command({nginx_bin(), "-t"});
    if (!test.ok) {
        return test;
    }
    auto reload = run_command({systemctl_bin(), "reload", nginx_reload_service()});
    if (!reload.ok) {
        return reload;
    }
    return {true, 0, "nginx site written, enabled, tested, and reloaded"};
}

CommandResult configure_systemd_service(const DeployRequest& request) {
    const auto unit_path = std::filesystem::path(service_unit_dir()) / (deploy_service_name(request) + ".service");
    if (!write_text_file(unit_path, build_unit_content(request), true)) {
        return {false, 1, "unable to write systemd service unit"};
    }

    auto reload = run_command({systemctl_bin(), "daemon-reload"});
    if (!reload.ok) {
        return reload;
    }
    auto enable = run_command({systemctl_bin(), "enable", deploy_service_name(request) + ".service"});
    if (!enable.ok) {
        return enable;
    }
    if (!request.skip_service_start) {
        auto restart = run_command({systemctl_bin(), "restart", deploy_service_name(request) + ".service"});
        if (!restart.ok) {
            return restart;
        }
        return {true, 0, "systemd unit written, enabled, and restarted"};
    }
    return {true, 0, "systemd unit written and enabled"};
}

CommandResult run_certbot(const DeployRequest& request) {
    if (request.skip_certbot) {
        return {true, 0, "certbot skipped"};
    }
    return run_command({certbot_bin(),
                        "--nginx",
                        "--non-interactive",
                        "--agree-tos",
                        "--redirect",
                        "-m", request.email,
                        "-d", request.domain});
}

std::string write_cloudflare_curl_config(const std::string& token) {
    std::string pattern = "/tmp/cuddlepanel-cloudflare-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const int fd = mkstemp(buffer.data());
    if (fd < 0) {
        return {};
    }
    close(fd);
    std::ofstream file(buffer.data(), std::ios::trunc);
    if (!file.good()) {
        std::remove(buffer.data());
        return {};
    }
    file << "header = \"Authorization: Bearer " << token << "\"\n";
    file << "header = \"Content-Type: application/json\"\n";
    file << "user-agent = \"cuddlePanel/1.0\"\n";
    file << "max-time = 30\n";
    file.close();
    chmod(buffer.data(), S_IRUSR | S_IWUSR);
    return std::string(buffer.data());
}

CommandResult curl_json_request(const std::string& method,
                                const std::string& url,
                                const std::string& bearer_token,
                                const std::string& body) {
    const std::string config_path = write_cloudflare_curl_config(bearer_token);
    if (config_path.empty()) {
        return {false, 1, "unable to prepare Cloudflare request"};
    }
    std::vector<std::string> args = {curl_bin(), "--silent", "--show-error", "--config", config_path, "--request", method, url};
    if (!body.empty()) {
        args.insert(args.end(), {"--data", body});
    }
    auto result = run_command(args);
    std::remove(config_path.c_str());
    return result;
}

CloudflareRecord find_existing_cloudflare_record(const std::string& zone_id,
                                                 const std::string& token,
                                                 const std::string& domain) {
    CloudflareRecord record;
    const std::string config_path = write_cloudflare_curl_config(token);
    if (config_path.empty()) {
        return record;
    }
    const std::string url = "https://api.cloudflare.com/client/v4/zones/" + zone_id + "/dns_records?type=A&name=" + domain;
    const auto result = run_command({curl_bin(), "--silent", "--show-error", "--config", config_path, url});
    std::remove(config_path.c_str());
    if (!result.ok) {
        return record;
    }

    const std::string marker = "\"id\":\"";
    const auto result_pos = result.output.find("\"result\":[");
    if (result_pos == std::string::npos) {
        return record;
    }
    const auto id_pos = result.output.find(marker, result_pos);
    if (id_pos == std::string::npos) {
        return record;
    }
    const auto id_end = result.output.find('"', id_pos + marker.size());
    if (id_end == std::string::npos) {
        return record;
    }
    record.found = true;
    record.id = result.output.substr(id_pos + marker.size(), id_end - (id_pos + marker.size()));
    return record;
}

CommandResult ensure_cloudflare_dns(const DeployRequest& request) {
    if (!request.enable_cloudflare_dns) {
        return {true, 0, "cloudflare DNS skipped"};
    }
    const std::string zone_id = request.cloudflare_zone_id.empty() ? default_cloudflare_zone_id() : request.cloudflare_zone_id;
    const std::string token = request.cloudflare_api_token.empty() ? default_cloudflare_api_token() : request.cloudflare_api_token;
    if (zone_id.empty() || token.empty()) {
        return {false, 1, "cloudflare zone id and API token are required when DNS creation is enabled"};
    }

    const auto record = find_existing_cloudflare_record(zone_id, token, request.domain);
    const std::string body = "{"
                             "\"type\":\"A\","
                             "\"name\":\"" + json_escape_local(request.domain) + "\","
                             "\"content\":\"" + json_escape_local(request.public_ip) + "\","
                             "\"ttl\":1,"
                             "\"proxied\":" + std::string(request.cloudflare_proxied ? "true" : "false") +
                             "}";
    if (record.found) {
        return curl_json_request("PATCH",
                                 "https://api.cloudflare.com/client/v4/zones/" + zone_id + "/dns_records/" + record.id,
                                 token,
                                 body);
    }
    return curl_json_request("POST",
                             "https://api.cloudflare.com/client/v4/zones/" + zone_id + "/dns_records",
                             token,
                             body);
}

}

std::optional<DeployRequest> deploy_request_from_form(const std::map<std::string, std::string>& form) {
    DeployRequest request;
    auto get = [&form](const std::string& key) -> std::string {
        auto found = form.find(key);
        return found == form.end() ? "" : trim_copy(found->second);
    };

    request.domain = get("domain");
    request.port = get("port");
    request.user = get("user");
    request.project_root = get("project_root");
    request.service_desc = get("service_desc");
    request.email = get("email");
    request.site_name = get("site_name");
    request.upstream_host = get("upstream_host");
    request.stack = get("stack");
    request.node_entry = get("node_entry");
    request.go_package = get("go_package");
    request.go_binary_path = get("go_binary_path");
    request.streamlit_entry = get("streamlit_entry");
    request.python_module = get("python_module");
    request.python_backend_dir = get("python_backend_dir");
    request.vite_root = get("vite_root");
    request.vite_dist_dir = get("vite_dist_dir");
    request.public_ip = get("public_ip");
    request.cloudflare_zone_id = get("cloudflare_zone_id");
    request.cloudflare_api_token = get("cloudflare_api_token");
    request.install_dependencies = truthy(get("install_dependencies"));
    request.run_build = truthy(get("run_build"));
    request.enable_cloudflare_dns = truthy(get("enable_cloudflare_dns"));
    request.cloudflare_proxied = truthy(get("cloudflare_proxied"));
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
    if (!valid_absolute_path(request.project_root)) {
        return fail("invalid project root");
    }
    if (!std::filesystem::is_directory(request.project_root)) {
        return fail("project root does not exist");
    }
    if (request.service_desc.empty() || request.service_desc.size() > 200 || request.service_desc.find('\0') != std::string::npos) {
        return fail("invalid service description");
    }
    if (!request.skip_certbot && (request.email.empty() || request.email.size() > 255 || request.email.find('@') == std::string::npos)) {
        return fail("email is required unless skip certbot is enabled");
    }
    if (!request.site_name.empty() && !valid_basic_token(request.site_name, 64)) {
        return fail("invalid site name");
    }
    if (!valid_hostname_like(request.upstream_host, 255)) {
        return fail("invalid upstream host");
    }

    const auto stack = deploy_stack_from_string_impl(request.stack);
    if (stack == DeployStack::NodeJs) {
        if (!valid_relative_path(request.node_entry)) {
            return fail("invalid node entry");
        }
    } else if (stack == DeployStack::Go) {
        if (!valid_relative_path(request.go_binary_path)) {
            return fail("invalid go binary path");
        }
        if (request.go_package.empty() || request.go_package.size() > 200 || request.go_package.find('\0') != std::string::npos) {
            return fail("invalid go package");
        }
    } else if (stack == DeployStack::Streamlit) {
        if (!valid_relative_path(request.streamlit_entry)) {
            return fail("invalid streamlit entry");
        }
    } else if (stack == DeployStack::PythonVite) {
        if (!valid_python_module(request.python_module)) {
            return fail("invalid python module");
        }
        if (!valid_relative_path(request.python_backend_dir)) {
            return fail("invalid python backend directory");
        }
        if (!valid_relative_path(request.vite_root)) {
            return fail("invalid vite root");
        }
        if (!valid_relative_path(request.vite_dist_dir)) {
            return fail("invalid vite dist directory");
        }
    }

    if (request.enable_cloudflare_dns) {
        if (!valid_ipv4(request.public_ip)) {
            return fail("invalid public IP");
        }
        if (!request.cloudflare_zone_id.empty() && !valid_basic_token(request.cloudflare_zone_id, 64)) {
            return fail("invalid cloudflare zone id");
        }
        if (!request.cloudflare_api_token.empty() && request.cloudflare_api_token.size() > 255) {
            return fail("invalid cloudflare api token");
        }
    }
    return true;
}

DeployStack deploy_stack_from_string(const std::string& value) {
    return deploy_stack_from_string_impl(value);
}

std::string deploy_stack_to_string(DeployStack stack) {
    return stack_value(stack);
}

DeployResult run_deploy_site(const DeployRequest& request) {
    std::string error;
    if (!valid_deploy_request(request, &error)) {
        return {false, error};
    }

    std::ostringstream output;

    const auto build = build_stack(request);
    if (!append_result(output, "Build", build)) {
        return {false, output.str()};
    }

    const auto service = configure_systemd_service(request);
    if (!append_result(output, "Systemd", service)) {
        return {false, output.str()};
    }

    const auto nginx = configure_nginx(request);
    if (!append_result(output, "Nginx", nginx)) {
        return {false, output.str()};
    }

    const auto certbot = run_certbot(request);
    if (!append_result(output, "TLS", certbot)) {
        return {false, output.str()};
    }

    const auto dns = ensure_cloudflare_dns(request);
    if (!append_result(output, "Cloudflare", dns)) {
        return {false, output.str()};
    }

    return {true, output.str()};
}

}
