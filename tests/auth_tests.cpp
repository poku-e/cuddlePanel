#include "auth.h"
#include "http.h"
#include "totp.h"
#include "user_store.h"

#include <cassert>
#include <filesystem>
#include <iostream>

int main() {
    assert(cuddle::init_crypto());
    assert(cuddle::valid_username("admin.user-1"));
    assert(!cuddle::valid_username("../root"));
    assert(cuddle::valid_password("VeryStrong!Pass123"));
    assert(!cuddle::valid_password("short"));

    auto hash = cuddle::hash_password("VeryStrong!Pass123");
    assert(hash);
    assert(hash->rfind("$argon2id$", 0) == 0);
    assert(cuddle::verify_password("VeryStrong!Pass123", *hash));
    assert(!cuddle::verify_password("WrongStrong!Pass123", *hash));
    assert(cuddle::valid_totp_code_format("123456"));
    assert(!cuddle::valid_totp_code_format("12345a"));
    auto rfc_code = cuddle::totp_code_for_time("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ", 59);
    assert(rfc_code);
    assert(*rfc_code == "287082");
    assert(cuddle::verify_totp_code("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ", "287082", 59, 0));

    std::filesystem::remove_all("tmp-test-data");
    cuddle::UserStore store("tmp-test-data/users.db");
    assert(store.load());
    assert(!store.has_users());
    assert(store.create_superuser("admin", "VeryStrong!Pass123"));
    assert(store.has_users());
    assert(!store.create_superuser("other", "VeryStrong!Pass123"));
    assert(store.authenticate("admin", "VeryStrong!Pass123"));
    assert(store.has_permission("admin", "dashboard", cuddle::PermissionLevel::Manage));
    auto admin_user = store.find_user("admin");
    assert(admin_user);
    assert(!admin_user->totp_secret.empty());
    assert(!admin_user->totp_confirmed);
    assert(store.confirm_totp("admin"));
    admin_user = store.find_user("admin");
    assert(admin_user && admin_user->totp_confirmed);
    assert(store.create_user("operator1",
                             "AnotherStrong!Pass123",
                             "operator",
                             {{"dashboard", cuddle::PermissionLevel::View},
                              {"users", cuddle::PermissionLevel::View}}));
    assert(store.find_user("operator1"));
    auto operator_user = store.find_user("operator1");
    assert(operator_user);
    assert(operator_user->totp_secret.empty());
    assert(!operator_user->totp_confirmed);
    assert(store.set_totp_secret("operator1", cuddle::generate_totp_secret()));
    operator_user = store.find_user("operator1");
    assert(operator_user && !operator_user->totp_secret.empty());
    assert(!store.create_user("operator1",
                              "AnotherStrong!Pass123",
                              "operator",
                              {{"dashboard", cuddle::PermissionLevel::View}}));
    assert(!store.create_user("super2",
                              "AnotherStrong!Pass123",
                              "superuser",
                              {{"dashboard", cuddle::PermissionLevel::Manage}}));
    assert(store.update_permissions("operator1",
                                    {{"dashboard", cuddle::PermissionLevel::Manage},
                                     {"users", cuddle::PermissionLevel::Manage}}));
    assert(store.has_permission("operator1", "dashboard", cuddle::PermissionLevel::Manage));
    assert(!store.update_permissions("admin", {{"dashboard", cuddle::PermissionLevel::View}}));
    assert(!store.delete_user("admin"));
    assert(store.delete_user("operator1"));
    assert(!store.find_user("operator1"));
    std::filesystem::remove_all("tmp-test-data");

    auto form = cuddle::parse_form("username=admin&password=VeryStrong%21Pass123");
    assert(form["username"] == "admin");
    assert(form["password"] == "VeryStrong!Pass123");

    cuddle::SessionStore sessions;
    auto session_token = sessions.create("admin", true);
    assert(sessions.pending_totp_setup(session_token));
    sessions.mark_totp_setup_complete(session_token);
    assert(!sessions.pending_totp_setup(session_token));
    sessions.mark_terminal_totp_verified(session_token, 1000);
    assert(sessions.terminal_totp_recently_verified(session_token, 1000 + 60, 1800));
    assert(!sessions.terminal_totp_recently_verified(session_token, 1000 + 1900, 1800));

    std::cout << "auth tests passed" << std::endl;
    return 0;
}
