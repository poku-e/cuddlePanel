#include "nginx_store.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    assert(cuddle::valid_nginx_site_name("example-main"));
    assert(!cuddle::valid_nginx_site_name("../example"));
    assert(cuddle::valid_nginx_filename("example.conf"));
    assert(cuddle::valid_nginx_filename("example"));
    assert(!cuddle::valid_nginx_filename("../example.conf"));
    assert(cuddle::valid_nginx_filename("example.txt"));
    assert(cuddle::valid_nginx_content("server { listen 80; }\n"));

    std::filesystem::remove_all("tmp-nginx-data");
    cuddle::NginxStore store(
        "tmp-nginx-data/nginx.db",
        "tmp-nginx-data/sites-available",
        "tmp-nginx-data/sites-enabled");
    assert(store.load());
    assert(store.create_site("example", "example.conf", "Primary site", "server { listen 80; }\n"));
    auto site = store.read_site("example");
    assert(site);
    assert(site->content == "server { listen 80; }\n");
    assert(!site->enabled);
    assert(store.set_enabled("example", true));
    site = store.read_site("example");
    assert(site && site->enabled);
    assert(store.update_site("example", "example-main", "example-main.conf", "Updated", "server { listen 443 ssl; }\n"));
    site = store.read_site("example-main");
    assert(site);
    assert(site->filename == "example-main.conf");
    assert(site->enabled);
    assert(site->content == "server { listen 443 ssl; }\n");
    assert(store.set_enabled("example-main", false));
    site = store.read_site("example-main");
    assert(site && !site->enabled);
    assert(!store.create_site("bad", "../bad.conf", "Nope", "server {}\n"));
    std::filesystem::remove_all("tmp-nginx-data");

    std::filesystem::remove_all("tmp-nginx-legacy");
    std::filesystem::create_directories("tmp-nginx-legacy/sites-available");
    {
        std::ofstream legacy_db("tmp-nginx-legacy/nginx.db", std::ios::trunc);
        legacy_db << "legacy-main\tlegacy-main.conf\n";
    }
    {
        std::ofstream legacy_conf("tmp-nginx-legacy/sites-available/legacy-main.conf", std::ios::trunc);
        legacy_conf << "server { listen 8080; }\n";
    }
    cuddle::NginxStore legacy_store(
        "tmp-nginx-legacy/nginx.db",
        "tmp-nginx-legacy/sites-available",
        "tmp-nginx-legacy/sites-enabled");
    assert(legacy_store.load());
    auto legacy_site = legacy_store.read_site("legacy-main");
    assert(legacy_site);
    assert(legacy_site->description.empty());
    assert(legacy_site->content == "server { listen 8080; }\n");
    std::filesystem::remove_all("tmp-nginx-legacy");

    std::filesystem::remove_all("tmp-nginx-discovery");
    std::filesystem::create_directories("tmp-nginx-discovery/sites-available");
    {
        std::ofstream discovered_conf("tmp-nginx-discovery/sites-available/discovered.conf", std::ios::trunc);
        discovered_conf << "server { listen 9090; }\n";
    }
    cuddle::NginxStore discovered_store(
        "tmp-nginx-discovery/nginx.db",
        "tmp-nginx-discovery/sites-available",
        "tmp-nginx-discovery/sites-enabled");
    assert(discovered_store.load());
    auto discovered_sites = discovered_store.sites();
    assert(discovered_sites.size() == 1);
    assert(discovered_sites[0].filename == "discovered.conf");
    assert(discovered_sites[0].name == "discovered");

    auto discovered_site = discovered_store.read_site("discovered");
    assert(discovered_site);
    assert(discovered_site->filename == "discovered.conf");
    assert(discovered_site->content == "server { listen 9090; }\n");
    assert(discovered_store.set_enabled("discovered", true));
    discovered_site = discovered_store.read_site("discovered");
    assert(discovered_site && discovered_site->enabled);
    assert(discovered_store.update_site("discovered", "renamed-discovered", "renamed-discovered.conf", "Imported", "server { listen 9091; }\n"));
    auto renamed_site = discovered_store.read_site("renamed-discovered");
    assert(renamed_site);
    assert(renamed_site->filename == "renamed-discovered.conf");
    assert(renamed_site->enabled);
    assert(renamed_site->description == "Imported");
    assert(renamed_site->content == "server { listen 9091; }\n");
    std::filesystem::remove_all("tmp-nginx-discovery");

    std::cout << "nginx tests passed" << std::endl;
    return 0;
}
