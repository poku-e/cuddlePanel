#pragma once

#include <optional>
#include <string>
#include <vector>

namespace cuddle {

struct Fail2banActionResult {
    bool ok = false;
    std::string output;
};

struct Fail2banJailSummary {
    std::string name;
    bool running = false;
    int currently_failed = 0;
    int currently_banned = 0;
};

struct Fail2banJailDetail {
    Fail2banJailSummary summary;
    std::vector<std::string> banned_ips;
    std::vector<std::string> ignored_ips;
};

class Fail2banStore {
public:
    Fail2banStore(std::string client_bin, std::string log_path);

    std::vector<Fail2banJailSummary> list_jails() const;
    std::optional<Fail2banJailDetail> jail_detail(const std::string& jail) const;

    Fail2banActionResult jail_action(const std::string& jail, const std::string& action) const;
    Fail2banActionResult global_action(const std::string& action) const;
    Fail2banActionResult ban_ip(const std::string& jail, const std::string& ip) const;
    Fail2banActionResult unban_ip(const std::string& jail, const std::string& ip) const;
    Fail2banActionResult whitelist_action(const std::string& jail,
                                          const std::string& action,
                                          const std::string& ip) const;

    std::vector<std::string> recent_logs(size_t max_lines) const;

private:
    Fail2banActionResult run_client(const std::vector<std::string>& args) const;

    std::string client_bin_;
    std::string log_path_;
};

bool valid_fail2ban_jail(const std::string& jail);
bool valid_fail2ban_ip(const std::string& ip_or_cidr);

}
