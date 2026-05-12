#include "service_store.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    namespace fs = std::filesystem;

    assert(cuddle::valid_service_name("nginx-main"));
    assert(!cuddle::valid_service_name("../nginx"));
    assert(cuddle::valid_service_unit("nginx.service"));
    assert(cuddle::valid_service_unit("php-fpm@8.3.service"));
    assert(!cuddle::valid_service_unit("nginx.timer"));

    const std::string normalized = cuddle::normalize_service_description(" Main   nginx \n frontend ");
    assert(normalized == "Main nginx frontend");
    assert(cuddle::valid_service_description(normalized));

    fs::remove_all("tmp-service-data");
    cuddle::ServiceStore store("tmp-service-data/services.db");
    assert(store.load());
    assert(store.create_service("nginx", "nginx.service", "Main nginx frontend"));
    assert(store.find_service("nginx"));
    assert(!store.create_service("nginx", "nginx.service", "Duplicate"));
    assert(store.update_service("nginx", "nginx-main", "nginx.service", "Updated description"));
    auto updated = store.find_service("nginx-main");
    assert(updated);
    assert(updated->description == "Updated description");
    assert(!store.update_service("missing", "missing", "nginx.service", "Nope"));

    const fs::path fake_systemctl = fs::path("tmp-service-data") / "fake-systemctl.sh";
    const fs::path action_log = fs::path("tmp-service-data") / "systemctl-actions.log";
    {
        std::ofstream script(fake_systemctl);
        script << "#!/bin/sh\n";
        script << "if [ \"$1\" = \"list-unit-files\" ]; then\n";
        script << "  printf 'nginx.service enabled\\nredis.service disabled\\n'\n";
        script << "  exit 0\n";
        script << "fi\n";
        script << "if [ \"$1\" = \"show\" ]; then\n";
        script << "  unit=\"$2\"\n";
        script << "  if [ \"$unit\" = \"nginx.service\" ]; then\n";
        script << "    cat <<'EOF'\n";
        script << "Id=nginx.service\nDescription=Nginx Web Server\nLoadState=loaded\nActiveState=active\nSubState=running\nUnitFileState=enabled\nFragmentPath=/usr/lib/systemd/system/nginx.service\n";
        script << "EOF\n";
        script << "    exit 0\n";
        script << "  fi\n";
        script << "  if [ \"$unit\" = \"redis.service\" ]; then\n";
        script << "    cat <<'EOF'\n";
        script << "Id=redis.service\nDescription=Redis In-Memory Store\nLoadState=loaded\nActiveState=inactive\nSubState=dead\nUnitFileState=disabled\nFragmentPath=/usr/lib/systemd/system/redis.service\n";
        script << "EOF\n";
        script << "    exit 0\n";
        script << "  fi\n";
        script << "  exit 1\n";
        script << "fi\n";
        script << "if [ \"$1\" = \"is-active\" ]; then\n";
        script << "  if [ \"$2\" = \"nginx.service\" ]; then printf 'active\\n'; else printf 'inactive\\n'; fi\n";
        script << "  exit 0\n";
        script << "fi\n";
        script << "printf '%s %s\\n' \"$1\" \"$2\" >> \"" << action_log.string() << "\"\n";
        script << "printf 'ok %s %s\\n' \"$1\" \"$2\"\n";
    }
    fs::permissions(fake_systemctl,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read,
                    fs::perm_options::replace);
    setenv("CUDDLEPANEL_SYSTEMCTL_BIN", fake_systemctl.c_str(), 1);

    const auto discovered = cuddle::discover_services();
    assert(discovered.size() == 2);
    assert(discovered[0].unit == "nginx.service");
    assert(discovered[0].active_state == "active");
    assert(discovered[0].unit_file_state == "enabled");

    const auto redis = cuddle::discover_service("redis.service");
    assert(redis);
    assert(redis->description == "Redis In-Memory Store");
    assert(redis->unit_file_state == "disabled");

    const auto status = cuddle::query_service_status("nginx.service");
    assert(status.state == "active");

    const auto enable_result = cuddle::run_service_action("redis.service", "enable");
    assert(enable_result.ok);
    const auto restart_result = cuddle::run_service_action("nginx.service", "restart");
    assert(restart_result.ok);
    const auto invalid = cuddle::run_service_action("bad unit", "start");
    assert(!invalid.ok);

    {
        std::ifstream log_file(action_log);
        std::string log((std::istreambuf_iterator<char>(log_file)), std::istreambuf_iterator<char>());
        assert(log.find("enable redis.service") != std::string::npos);
        assert(log.find("restart nginx.service") != std::string::npos);
    }

    fs::remove_all("tmp-service-data");
    std::cout << "service tests passed" << std::endl;
    return 0;
}
