#include "fail2ban_store.h"

#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

namespace cuddle {
namespace {

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

std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, delimiter)) {
        parts.push_back(item);
    }
    return parts;
}

Fail2banActionResult capture_command(const std::vector<std::string>& args) {
    std::array<char, 256> buffer{};
    std::string output;
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        return {false, "unable to execute fail2ban command"};
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return {false, "unable to execute fail2ban command"};
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

std::vector<std::string> parse_jail_names(const std::string& status_output) {
    std::stringstream stream(status_output);
    std::string line;
    while (std::getline(stream, line)) {
        const auto pos = line.find("Jail list:");
        if (pos == std::string::npos) {
            continue;
        }
        const std::string tail = trim_copy(line.substr(pos + std::string("Jail list:").size()));
        std::vector<std::string> names;
        for (const auto& part : split(tail, ',')) {
            const std::string jail = trim_copy(part);
            if (!jail.empty() && valid_fail2ban_jail(jail)) {
                names.push_back(jail);
            }
        }
        return names;
    }
    return {};
}

std::optional<int> parse_int_after(const std::string& line, const std::string& key) {
    const auto pos = line.find(key);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    const std::string raw = trim_copy(line.substr(pos + key.size()));
    if (raw.empty()) {
        return std::nullopt;
    }
    try {
        return std::stoi(raw);
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<std::string> parse_ip_list(const std::string& line, const std::string& key) {
    const auto pos = line.find(key);
    if (pos == std::string::npos) {
        return {};
    }
    const std::string raw = trim_copy(line.substr(pos + key.size()));
    if (raw.empty()) {
        return {};
    }

    std::vector<std::string> ips;
    std::stringstream stream(raw);
    std::string token;
    while (stream >> token) {
        if (valid_fail2ban_ip(token)) {
            ips.push_back(token);
        }
    }
    return ips;
}

Fail2banJailSummary parse_jail_summary(const std::string& jail_name,
                                       const Fail2banActionResult& status_result) {
    Fail2banJailSummary summary;
    summary.name = jail_name;
    summary.running = status_result.ok;

    std::stringstream stream(status_result.output);
    std::string line;
    while (std::getline(stream, line)) {
        if (const auto value = parse_int_after(line, "Currently failed:")) {
            summary.currently_failed = *value;
        }
        if (const auto value = parse_int_after(line, "Currently banned:")) {
            summary.currently_banned = *value;
        }
    }
    return summary;
}

std::vector<std::string> parse_ignore_ips(const std::string& output) {
    std::vector<std::string> ips;
    std::stringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        const std::string trimmed = trim_copy(line);
        if (trimmed.empty()) {
            continue;
        }
        std::stringstream token_stream(trimmed);
        std::string token;
        while (token_stream >> token) {
            if (valid_fail2ban_ip(token)) {
                ips.push_back(token);
            }
        }
    }
    return ips;
}

} // anonymous namespace

Fail2banStore::Fail2banStore(std::string client_bin, std::string log_path)
    : client_bin_(std::move(client_bin)),
      log_path_(std::move(log_path)) {}

std::vector<Fail2banJailSummary> Fail2banStore::list_jails() const {
    const auto status = run_client({"status"});
    if (!status.ok) {
        return {};
    }

    std::vector<Fail2banJailSummary> jails;
    for (const auto& jail : parse_jail_names(status.output)) {
        const auto jail_status = run_client({"status", jail});
        jails.push_back(parse_jail_summary(jail, jail_status));
    }
    return jails;
}

std::optional<Fail2banJailDetail> Fail2banStore::jail_detail(const std::string& jail) const {
    if (!valid_fail2ban_jail(jail)) {
        return std::nullopt;
    }

    const auto status = run_client({"status", jail});
    if (!status.ok) {
        return std::nullopt;
    }

    Fail2banJailDetail detail;
    detail.summary = parse_jail_summary(jail, status);

    std::stringstream stream(status.output);
    std::string line;
    while (std::getline(stream, line)) {
        const auto parsed_ips = parse_ip_list(line, "Banned IP list:");
        if (!parsed_ips.empty()) {
            detail.banned_ips = parsed_ips;
        }
    }

    const auto ignore = run_client({"get", jail, "ignoreip"});
    if (ignore.ok) {
        detail.ignored_ips = parse_ignore_ips(ignore.output);
    }

    return detail;
}

Fail2banActionResult Fail2banStore::jail_action(const std::string& jail, const std::string& action) const {
    if (!valid_fail2ban_jail(jail)) {
        return {false, "invalid jail"};
    }
    if (action != "start" && action != "stop" && action != "reload") {
        return {false, "invalid jail action"};
    }
    if (action == "reload") {
        return run_client({"reload", jail});
    }
    return run_client({action, jail});
}

Fail2banActionResult Fail2banStore::global_action(const std::string& action) const {
    if (action != "reload" && action != "restart") {
        return {false, "invalid fail2ban action"};
    }
    return run_client({action});
}

Fail2banActionResult Fail2banStore::ban_ip(const std::string& jail, const std::string& ip) const {
    if (!valid_fail2ban_jail(jail) || !valid_fail2ban_ip(ip)) {
        return {false, "invalid jail or ip"};
    }
    return run_client({"set", jail, "banip", ip});
}

Fail2banActionResult Fail2banStore::unban_ip(const std::string& jail, const std::string& ip) const {
    if (!valid_fail2ban_jail(jail) || !valid_fail2ban_ip(ip)) {
        return {false, "invalid jail or ip"};
    }
    return run_client({"set", jail, "unbanip", ip});
}

Fail2banActionResult Fail2banStore::whitelist_action(const std::string& jail,
                                                     const std::string& action,
                                                     const std::string& ip) const {
    if (!valid_fail2ban_jail(jail) || !valid_fail2ban_ip(ip)) {
        return {false, "invalid jail or ip"};
    }
    if (action == "add") {
        return run_client({"set", jail, "addignoreip", ip});
    }
    if (action == "remove") {
        return run_client({"set", jail, "delignoreip", ip});
    }
    return {false, "invalid whitelist action"};
}

std::vector<std::string> Fail2banStore::recent_logs(size_t max_lines) const {
    const size_t capped_lines = std::min<size_t>(max_lines, 500);
    std::ifstream file(log_path_);
    if (!file.good()) {
        return {};
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    if (lines.size() <= capped_lines) {
        return lines;
    }
    return std::vector<std::string>(lines.end() - static_cast<std::ptrdiff_t>(capped_lines), lines.end());
}

Fail2banActionResult Fail2banStore::run_client(const std::vector<std::string>& args) const {
    std::vector<std::string> command;
    command.reserve(args.size() + 1);
    command.push_back(client_bin_);
    command.insert(command.end(), args.begin(), args.end());
    return capture_command(command);
}

bool valid_fail2ban_jail(const std::string& jail) {
    static const std::regex pattern("^[A-Za-z0-9_-]{1,64}$");
    return std::regex_match(jail, pattern);
}

bool valid_fail2ban_ip(const std::string& ip_or_cidr) {
    std::string ip = ip_or_cidr;
    int mask = -1;

    const auto slash_pos = ip_or_cidr.find('/');
    if (slash_pos != std::string::npos) {
        ip = ip_or_cidr.substr(0, slash_pos);
        const std::string mask_text = ip_or_cidr.substr(slash_pos + 1);
        if (mask_text.empty()) {
            return false;
        }
        try {
            mask = std::stoi(mask_text);
        } catch (...) {
            return false;
        }
    }

    in_addr addr4{};
    if (inet_pton(AF_INET, ip.c_str(), &addr4) == 1) {
        return mask < 0 || (mask >= 0 && mask <= 32);
    }

    in6_addr addr6{};
    if (inet_pton(AF_INET6, ip.c_str(), &addr6) == 1) {
        return mask < 0 || (mask >= 0 && mask <= 128);
    }

    return false;
}

} // namespace cuddle
