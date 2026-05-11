#include "system_admin.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    namespace fs = std::filesystem;
    const fs::path temp_root = fs::current_path() / "tmp-system-data";
    const fs::path home_root = temp_root / "home";
    const fs::path srv_root = temp_root / "srv";
    const fs::path alice_home = home_root / "alice";
    const fs::path app_root = srv_root / "app";

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
        group << "sudo:x:27:alice\n";
    }
    {
        std::ofstream shadow(temp_root / "shadow");
        shadow << "root:!:20000:0:99999:7:::\n";
        shadow << "alice:$6$hash:20000:0:99999:7:::\n";
        shadow << "daemon:!*:20000:0:99999:7:::\n";
    }

    setenv("CUDDLEPANEL_SYSTEM_ALLOWED_ROOTS",
           (home_root.string() + "," + srv_root.string()).c_str(),
           1);
    setenv("CUDDLEPANEL_USERADD_BIN", "/bin/true", 1);
    setenv("CUDDLEPANEL_PASSWD_BIN", "/bin/true", 1);
    setenv("CUDDLEPANEL_USERMOD_BIN", "/bin/true", 1);
    setenv("CUDDLEPANEL_GPASSWD_BIN", "/bin/true", 1);
    setenv("CUDDLEPANEL_USERDEL_BIN", "/bin/true", 1);
    setenv("CUDDLEPANEL_CHOWN_BIN", "/bin/true", 1);
    setenv("CUDDLEPANEL_CHMOD_BIN", "/bin/true", 1);

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

    auto daemon = admin.find_user("daemon");
    assert(daemon);
    assert(daemon->system_account);
    assert(daemon->locked);
    assert(!daemon->login_user);

    assert(cuddle::valid_system_username("deploy-user"));
    assert(!cuddle::valid_system_username("../bad"));
    assert(cuddle::valid_mode_string("755"));
    assert(cuddle::valid_mode_string("0644"));
    assert(!cuddle::valid_mode_string("888"));
    assert(cuddle::normalize_allowed_system_path(alice_home.string()));
    assert(!cuddle::normalize_allowed_system_path("/etc/passwd"));

    const auto create_result = admin.create_user("deploy", "/bin/bash", "/home/deploy", false);
    assert(create_result.ok);
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

    const auto daemon_keys = admin.write_authorized_keys("daemon", public_key);
    assert(!daemon_keys.ok);

    fs::remove_all(temp_root);
    std::cout << "system admin tests passed" << std::endl;
    return 0;
}
