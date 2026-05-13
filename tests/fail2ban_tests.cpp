#include "fail2ban_store.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    namespace fs = std::filesystem;

    assert(cuddle::valid_fail2ban_jail("sshd"));
    assert(cuddle::valid_fail2ban_jail("nginx-http-auth"));
    assert(!cuddle::valid_fail2ban_jail("../sshd"));

    assert(cuddle::valid_fail2ban_ip("203.0.113.7"));
    assert(cuddle::valid_fail2ban_ip("2001:db8::42"));
    assert(cuddle::valid_fail2ban_ip("198.51.100.0/24"));
    assert(!cuddle::valid_fail2ban_ip("not-an-ip"));
    assert(!cuddle::valid_fail2ban_ip("203.0.113.7/99"));

    fs::remove_all("tmp-fail2ban-data");
    fs::create_directories("tmp-fail2ban-data");

    const fs::path fake_client = fs::path("tmp-fail2ban-data") / "fake-fail2ban-client.sh";
    const fs::path command_log = fs::path("tmp-fail2ban-data") / "command.log";
    const fs::path log_file = fs::path("tmp-fail2ban-data") / "fail2ban.log";

    {
        std::ofstream script(fake_client);
        script << "#!/bin/sh\n";
        script << "printf '%s\\n' \"$*\" >> \"" << command_log.string() << "\"\n";
        script << "if [ \"$1\" = \"status\" ] && [ $# -eq 1 ]; then\n";
        script << "  cat <<'EOF'\n";
        script << "Status\n";
        script << "|- Number of jail: 2\n";
        script << "`- Jail list: sshd, nginx-http-auth\n";
        script << "EOF\n";
        script << "  exit 0\n";
        script << "fi\n";
        script << "if [ \"$1\" = \"status\" ] && [ \"$2\" = \"sshd\" ]; then\n";
        script << "  cat <<'EOF'\n";
        script << "Status for the jail: sshd\n";
        script << "|- Filter\n";
        script << "|  |- Currently failed: 3\n";
        script << "`- Actions\n";
        script << "   |- Currently banned: 1\n";
        script << "   `- Banned IP list: 203.0.113.5\n";
        script << "EOF\n";
        script << "  exit 0\n";
        script << "fi\n";
        script << "if [ \"$1\" = \"status\" ] && [ \"$2\" = \"nginx-http-auth\" ]; then\n";
        script << "  cat <<'EOF'\n";
        script << "Status for the jail: nginx-http-auth\n";
        script << "|- Filter\n";
        script << "|  |- Currently failed: 0\n";
        script << "`- Actions\n";
        script << "   |- Currently banned: 0\n";
        script << "   `- Banned IP list:\n";
        script << "EOF\n";
        script << "  exit 0\n";
        script << "fi\n";
        script << "if [ \"$1\" = \"get\" ] && [ \"$2\" = \"sshd\" ] && [ \"$3\" = \"ignoreip\" ]; then\n";
        script << "  echo '127.0.0.1/8 ::1 198.51.100.42'\n";
        script << "  exit 0\n";
        script << "fi\n";
        script << "if [ \"$1\" = \"get\" ] && [ \"$2\" = \"nginx-http-auth\" ] && [ \"$3\" = \"ignoreip\" ]; then\n";
        script << "  echo '127.0.0.1/8'\n";
        script << "  exit 0\n";
        script << "fi\n";
        script << "echo 'ok'\n";
    }

    fs::permissions(fake_client,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read,
                    fs::perm_options::replace);

    {
        std::ofstream logs(log_file);
        logs << "line1\nline2\nline3\nline4\nline5\n";
    }

    cuddle::Fail2banStore store(fake_client.string(), log_file.string());

    const auto jails = store.list_jails();
    assert(jails.size() == 2);
    assert(jails[0].name == "sshd");
    assert(jails[0].currently_failed == 3);
    assert(jails[0].currently_banned == 1);

    const auto detail = store.jail_detail("sshd");
    assert(detail);
    assert(detail->banned_ips.size() == 1);
    assert(detail->banned_ips[0] == "203.0.113.5");
    assert(detail->ignored_ips.size() == 3);

    const auto ban_result = store.ban_ip("sshd", "198.51.100.8");
    assert(ban_result.ok);
    const auto unban_result = store.unban_ip("sshd", "198.51.100.8");
    assert(unban_result.ok);
    const auto add_ignore = store.whitelist_action("sshd", "add", "198.51.100.9");
    assert(add_ignore.ok);
    const auto remove_ignore = store.whitelist_action("sshd", "remove", "198.51.100.9");
    assert(remove_ignore.ok);
    const auto reload = store.global_action("reload");
    assert(reload.ok);
    const auto bad_action = store.global_action("bad");
    assert(!bad_action.ok);

    const auto tail = store.recent_logs(3);
    assert(tail.size() == 3);
    assert(tail[0] == "line3");
    assert(tail[2] == "line5");

    {
        std::ifstream command_file(command_log);
        std::string commands((std::istreambuf_iterator<char>(command_file)), std::istreambuf_iterator<char>());
        assert(commands.find("status") != std::string::npos);
        assert(commands.find("set sshd banip 198.51.100.8") != std::string::npos);
        assert(commands.find("set sshd delignoreip 198.51.100.9") != std::string::npos);
        assert(commands.find("reload") != std::string::npos);
    }

    fs::remove_all("tmp-fail2ban-data");
    std::cout << "fail2ban tests passed" << std::endl;
    return 0;
}
