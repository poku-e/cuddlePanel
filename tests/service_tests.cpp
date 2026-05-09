#include "service_store.h"

#include <cassert>
#include <filesystem>
#include <iostream>

int main() {
    assert(cuddle::valid_service_name("nginx-main"));
    assert(!cuddle::valid_service_name("../nginx"));
    assert(cuddle::valid_service_unit("nginx.service"));
    assert(cuddle::valid_service_unit("php-fpm@8.3.service"));
    assert(!cuddle::valid_service_unit("nginx.timer"));

    const std::string normalized = cuddle::normalize_service_description(" Main   nginx \n frontend ");
    assert(normalized == "Main nginx frontend");
    assert(cuddle::valid_service_description(normalized));

    std::filesystem::remove_all("tmp-service-data");
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
    std::filesystem::remove_all("tmp-service-data");

    const auto invalid = cuddle::run_service_action("bad unit", "start");
    assert(!invalid.ok);

    std::cout << "service tests passed" << std::endl;
    return 0;
}
