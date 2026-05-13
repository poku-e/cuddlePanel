#include "system_admin.h"
#include "http.h"
#include "fail2ban_store.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace cuddle {
HttpResponse handle_system_users(const RequestContext& ctx, const std::string& id);
}

int main() {
    namespace fs = std::filesystem;
    const fs::path temp_root = fs::current_path() / "tmp-system-data";
    const fs::path home_root = temp_root / "home";
    const fs::path srv_root = temp_root / "srv";
    const fs::path alice_home = home_root / "alice";
    const fs::path app_root = srv_root / "app";
    const fs::path usermod_log = temp_root / "usermod.log";
    const fs::path usermod_script = temp_root / "fake-usermod.sh";
    const fs::path chpasswd_log = temp_root / "chpasswd.log";
    const fs::path chpasswd_script = temp_root / "fake-chpasswd.sh";
    const fs::path chage_log = temp_root / "chage.log";
    const fs::path chage_script = temp_root / "fake-chage.sh";
    const fs::path zip_log = temp_root / "zip.log";
    const fs::path zip_script = temp_root / "fake-zip.sh";
    const fs::path unzip_log = temp_root / "unzip.log";
    const fs::path unzip_script = temp_root / "fake-unzip.sh";

    fs::remove_all(temp_root);
    fs::create_directories(alice_home);
    fs::create_directories(app_root);

    {
        std::ofstream passwd(temp_root / "passwd");
        passwd << "root:x:0:0:root:/root:/bin/bash\n";
        passwd << "alice:x:1000:1000:Alice:" << alice_home.string() << ":/bin/bash\n";
        passwd << "daemon:x:1:1:daemon:" << (temp_root / "daemon-home").string() << ":/usr/sbin/nologin\n";
    }
    {
        std::ofstream group(temp_root / "group");
        group << "root:x:0:\n";
        group << "alice:x:1000:\n";
        group << "deploy:x:1001:alice\n";
        group << "sudo:x:27:alice\n";
        group << "www-data:x:33:alice\n";
    }
    {
        std::ofstream shadow(temp_root / "shadow");
        shadow << "root:!:20000:0:99999:7:::\n";
        shadow << "alice:$6$hash:0:0:99999:7::25000:\n";
        shadow << "daemon:!*:20000:0:99999:7:::\n";
    }

    {
        std::ofstream script(usermod_script);
        script << "#!/bin/sh\n";
        script << "printf '%s\\n' \"$@\" > \"" << usermod_log.string() << "\"\n";
    }
    {
        std::ofstream script(chpasswd_script);
        script << "#!/bin/sh\n";
        script << "cat > \"" << chpasswd_log.string() << "\"\n";
    }
    {
        std::ofstream script(chage_script);
        script << "#!/bin/sh\n";
        script << "printf '%s\\n' \"$@\" >> \"" << chage_log.string() << "\"\n";
    }
    {
        std::ofstream script(zip_script);
        script << "#!/bin/sh\n";
        script << "printf '%s\\n' \"$@\" > \"" << zip_log.string() << "\"\n";
        script << "touch \"$2\"\n";
    }
    {
        std::ofstream script(unzip_script);
        script << "#!/bin/sh\n";
        script << "printf '%s\\n' \"$@\" > \"" << unzip_log.string() << "\"\n";
        script << "mkdir -p \"$4\"\n";
        script << "touch \"$4/unzipped.txt\"\n";
    }
    fs::permissions(usermod_script,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read,
                    fs::perm_options::replace);
    fs::permissions(chpasswd_script,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read,
                    fs::perm_options::replace);
    fs::permissions(chage_script,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read,
                    fs::perm_options::replace);
    fs::permissions(zip_script,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read,
                    fs::perm_options::replace);
    fs::permissions(unzip_script,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read,
                    fs::perm_options::replace);

    setenv("CUDDLEPANEL_SYSTEM_ALLOWED_ROOTS",
           (home_root.string() + "," + srv_root.string()).c_str(),
           1);
    setenv("CUDDLEPANEL_USERADD_BIN", "/bin/true", 1);
    setenv("CUDDLEPANEL_PASSWD_BIN", "/bin/true", 1);
    setenv("CUDDLEPANEL_USERMOD_BIN", usermod_script.c_str(), 1);
    setenv("CUDDLEPANEL_CHPASSWD_BIN", chpasswd_script.c_str(), 1);
    setenv("CUDDLEPANEL_CHAGE_BIN", chage_script.c_str(), 1);
    setenv("CUDDLEPANEL_GPASSWD_BIN", "/bin/true", 1);
    setenv("CUDDLEPANEL_USERDEL_BIN", "/bin/true", 1);
    setenv("CUDDLEPANEL_CHOWN_BIN", "/bin/true", 1);
    setenv("CUDDLEPANEL_CHMOD_BIN", "/bin/true", 1);
    setenv("CUDDLEPANEL_ZIP_BIN", zip_script.c_str(), 1);
    setenv("CUDDLEPANEL_UNZIP_BIN", unzip_script.c_str(), 1);

    cuddle::SystemAdmin admin((temp_root / "passwd").string(),
                              (temp_root / "group").string(),
                              (temp_root / "shadow").string());
    const auto users = admin.users();
    assert(users.size() == 3);
    assert(users[0].username == "root");
    assert(users[0].locked);

    auto alice = admin.find_user("alice");
    assert(alice);
    assert(alice->in_sudo);
    assert(!alice->system_account);
    assert(alice->login_user);
    assert(alice->comment == "Alice");
    assert(alice->primary_group == "alice");
    assert(alice->secondary_groups.size() == 3);
    assert(alice->secondary_groups[0] == "deploy");
    assert(alice->secondary_groups[1] == "sudo");
    assert(alice->secondary_groups[2] == "www-data");
    assert(alice->password_change_required);
    assert(!alice->expires_on.empty());

    auto daemon = admin.find_user("daemon");
    assert(daemon);
    assert(daemon->system_account);
    assert(daemon->locked);
    assert(!daemon->login_user);

    {
        const std::filesystem::path user_db = temp_root / "users.db";
        cuddle::UserStore panel_users(user_db.string());
        const bool created = panel_users.create_superuser("paneladmin", "PanelAdmin123!");
        assert(created);

        cuddle::ServiceStore services((temp_root / "services.db").string());
        cuddle::NginxStore nginx((temp_root / "nginx.db").string(),
                                 (temp_root / "sites-available").string(),
                                 (temp_root / "sites-enabled").string());
        cuddle::Fail2banStore fail2ban("/usr/bin/fail2ban-client", (temp_root / "fail2ban.log").string());
        cuddle::TerminalManager terminal;
        cuddle::CodexProjectStore projects((temp_root / "codex-projects.db").string());
        cuddle::CodexConversationManager conversations(projects, (temp_root / "codex-conversations.db").string());
        cuddle::SessionStore sessions;
        cuddle::HttpRequest request;
        request.method = "GET";
        request.path = "/api/system/users";
        const cuddle::RequestContext ctx{
            request,
            "",
            std::optional<std::string>{"paneladmin"},
            panel_users,
            services,
            nginx,
            fail2ban,
            admin,
            terminal,
            projects,
            conversations,
            sessions
        };
        const auto response = cuddle::handle_system_users(ctx, "");
        assert(response.status == 200);
        const std::string expires_fragment =
            std::string("\"expires_on\":\"") + cuddle::json_escape(alice->expires_on) + "\"}";
        assert(response.body.find(expires_fragment) != std::string::npos);
        assert(response.body.find("\"allowedRoots\":[") != std::string::npos);
    }

    assert(cuddle::valid_system_username("deploy-user"));
    assert(!cuddle::valid_system_username("../bad"));
    assert(cuddle::valid_mode_string("755"));
    assert(cuddle::valid_mode_string("0644"));
    assert(!cuddle::valid_mode_string("888"));
    assert(cuddle::normalize_allowed_system_path(alice_home.string()));
    assert(!cuddle::normalize_allowed_system_path("/etc/passwd"));

    const auto create_result = admin.create_user("deploy", "/bin/bash", "/home/deploy", false);
    assert(create_result.ok);
    const auto update_result = admin.update_user("alice",
                                                 "/bin/zsh",
                                                 "/srv/alice",
                                                 true,
                                                 "Alice Admin",
                                                 "deploy",
                                                 {"sudo", "www-data", "deploy"});
    assert(update_result.ok);
    {
        std::ifstream log_file(usermod_log);
        std::string log((std::istreambuf_iterator<char>(log_file)), std::istreambuf_iterator<char>());
        assert(log.find("-s\n/bin/zsh\n") != std::string::npos);
        assert(log.find("-m\n-d\n/srv/alice\n") != std::string::npos);
        assert(log.find("-c\nAlice Admin\n") != std::string::npos);
        assert(log.find("-g\ndeploy\n") != std::string::npos);
        assert(log.find("-G\nsudo,www-data\n") != std::string::npos);
        assert(log.find("\nalice\n") != std::string::npos || log.rfind("alice\n") != std::string::npos);
    }
    const auto security_result = admin.update_user_security("alice",
                                                            "Sup3rSecure!",
                                                            true,
                                                            true,
                                                            false,
                                                            "2030-12-25");
    assert(security_result.ok);
    {
        std::ifstream log_file(chpasswd_log);
        std::string log((std::istreambuf_iterator<char>(log_file)), std::istreambuf_iterator<char>());
        assert(log == "alice:Sup3rSecure!\n");
    }
    {
        std::ifstream log_file(chage_log);
        std::string log((std::istreambuf_iterator<char>(log_file)), std::istreambuf_iterator<char>());
        assert(log.find("-d\n0\nalice\n") != std::string::npos);
        assert(log.find("-E\n2030-12-25\nalice\n") != std::string::npos);
    }
    const auto lock_result = admin.run_user_action("alice", "lock", false);
    assert(lock_result.ok);
    const auto sudo_result = admin.run_user_action("alice", "grant-sudo", false);
    assert(sudo_result.ok);
    const auto delete_result = admin.run_user_action("alice", "delete", true);
    assert(delete_result.ok);
    const auto root_reject = admin.run_user_action("root", "lock", false);
    assert(!root_reject.ok);
    const auto root_delete_reject = admin.run_user_action("root", "delete", true);
    assert(!root_delete_reject.ok);

    const auto chown_result = admin.run_path_action("chown",
                                                    alice_home.string(),
                                                    "alice",
                                                    "sudo",
                                                    "",
                                                    false);
    assert(chown_result.ok);
    const auto chmod_result = admin.run_path_action("chmod",
                                                    app_root.string(),
                                                    "",
                                                    "",
                                                    "755",
                                                    true);
    assert(chmod_result.ok);
    const auto path_reject = admin.run_path_action("chmod", "/etc", "", "", "755", false);
    assert(!path_reject.ok);

    std::string keys_content;
    const auto missing_keys = admin.read_authorized_keys("alice", &keys_content);
    assert(missing_keys.ok);
    assert(keys_content.empty());

    const std::string public_key = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITestKey alice@example\n";
    const auto save_keys = admin.write_authorized_keys("alice", public_key);
    assert(save_keys.ok);
    const auto load_keys = admin.read_authorized_keys("alice", &keys_content);
    assert(load_keys.ok);
    assert(keys_content == public_key);
    assert(fs::exists(alice_home / ".ssh" / "authorized_keys"));

    {
        std::ofstream history(alice_home / ".bash_history");
        history << "ls -la\n";
        history << "sudo systemctl restart nginx\n";
    }
    std::vector<cuddle::SystemUserLogFile> logfiles;
    const auto load_logfiles = admin.read_user_logfiles("alice", &logfiles);
    assert(load_logfiles.ok);
    assert(logfiles.size() == 1);
    assert(logfiles[0].name == ".bash_history");
    assert(logfiles[0].label == "Bash history");
    assert(logfiles[0].content.find("sudo systemctl restart nginx") != std::string::npos);
    const auto daemon_logfiles = admin.read_user_logfiles("daemon", &logfiles);
    assert(!daemon_logfiles.ok);

    {
        std::ofstream notes(alice_home / "notes.txt");
        notes << "hello world\n";
    }
    cuddle::SystemFileBrowserListing listing;
    const auto browse_home = admin.browse_files(alice_home.string(), &listing);
    assert(browse_home.ok);
    assert(listing.current_path == alice_home.string());
    assert(!listing.entries.empty());

    const auto rename_result = admin.run_file_action("rename",
                                                     (alice_home / "notes.txt").string(),
                                                     "",
                                                     "renamed.txt",
                                                     "",
                                                     "",
                                                     "",
                                                     false);
    assert(rename_result.ok);
    assert(fs::exists(alice_home / "renamed.txt"));

    const auto copy_result = admin.run_file_action("copy",
                                                   (alice_home / "renamed.txt").string(),
                                                   app_root.string(),
                                                   "",
                                                   "",
                                                   "",
                                                   "",
                                                   false);
    assert(copy_result.ok);
    assert(fs::exists(app_root / "renamed.txt"));

    const auto zip_result = admin.run_file_action("zip",
                                                  app_root.string(),
                                                  "",
                                                  "",
                                                  "",
                                                  "",
                                                  "",
                                                  false);
    assert(zip_result.ok);
    assert(fs::exists(srv_root / "app.zip"));

    const fs::path unzip_target = home_root / "unzipped";
    fs::create_directories(unzip_target);
    const auto unzip_result = admin.run_file_action("unzip",
                                                    (srv_root / "app.zip").string(),
                                                    unzip_target.string(),
                                                    "",
                                                    "",
                                                    "",
                                                    "",
                                                    false);
    assert(unzip_result.ok);
    assert(fs::exists(unzip_target / "unzipped.txt"));

    const auto daemon_keys = admin.write_authorized_keys("daemon", public_key);
    assert(!daemon_keys.ok);

    fs::remove_all(temp_root);
    std::cout << "system admin tests passed" << std::endl;
    return 0;
}
