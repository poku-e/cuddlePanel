#include "auth.h"
#include "codex_chat.h"
#include "http.h"
#include "log.h"
#include "nginx_store.h"
#include "service_store.h"
#include "system_admin.h"
#include "terminal_manager.h"
#include "user_store.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

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

void load_dotenv_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.good()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::string trimmed = trim_copy(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        if (trimmed.rfind("export ", 0) == 0) {
            trimmed = trim_copy(trimmed.substr(7));
        }

        const auto pos = trimmed.find('=');
        if (pos == std::string::npos || pos == 0) {
            continue;
        }

        std::string key = trim_copy(trimmed.substr(0, pos));
        std::string value = trim_copy(trimmed.substr(pos + 1));
        if (key.empty() || std::getenv(key.c_str())) {
            continue;
        }

        if (value.size() >= 2) {
            const char first = value.front();
            const char last = value.back();
            if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
                value = value.substr(1, value.size() - 2);
            }
        }

        setenv(key.c_str(), value.c_str(), 0);
    }
}

}

int main() {
    load_dotenv_file(".env");
    cuddle::init_logging();
    cuddle::log_message(cuddle::LogLevel::Info, "cuddlePanel startup initialized");

    if (!cuddle::init_crypto()) {
        cuddle::log_message(cuddle::LogLevel::Error, "failed to initialize crypto");
        return 1;
    }

    cuddle::UserStore users("data/users.db");
    if (!users.load()) {
        cuddle::log_message(cuddle::LogLevel::Error, "failed to load users from data/users.db");
        return 1;
    }

    cuddle::ServiceStore services("data/services.db");
    if (!services.load()) {
        cuddle::log_message(cuddle::LogLevel::Error, "failed to load services from data/services.db");
        return 1;
    }

    std::string nginx_available_dir = "/etc/nginx/sites-available";
    if (const char* configured = std::getenv("CUDDLEPANEL_NGINX_AVAILABLE_DIR")) {
        nginx_available_dir = configured;
    }
    std::string nginx_enabled_dir = "/etc/nginx/sites-enabled";
    if (const char* configured = std::getenv("CUDDLEPANEL_NGINX_ENABLED_DIR")) {
        nginx_enabled_dir = configured;
    }

    cuddle::NginxStore nginx("data/nginx.db", nginx_available_dir, nginx_enabled_dir);
    if (!nginx.load()) {
        cuddle::log_message(cuddle::LogLevel::Error, "failed to load nginx sites from data/nginx.db");
        return 1;
    }

    int port = 8080;
    if (const char* configured = std::getenv("CUDDLEPANEL_PORT")) {
        port = std::atoi(configured);
    }
    if (port < 1 || port > 65535) {
        cuddle::log_message(cuddle::LogLevel::Error, "invalid CUDDLEPANEL_PORT value; expected 1-65535");
        return 1;
    }

    cuddle::SystemAdmin system_admin(cuddle::passwd_file_path(),
                                     cuddle::group_file_path(),
                                     cuddle::shadow_file_path());
    cuddle::TerminalManager terminal;
    cuddle::CodexProjectStore codex_projects("data/codex_projects.db");
    if (!codex_projects.load()) {
        cuddle::log_message(cuddle::LogLevel::Error, "failed to load Codex projects from data/codex_projects.db");
        return 1;
    }
    cuddle::CodexConversationManager codex_conversations(codex_projects, "data/codex_conversations.db");
    if (!codex_conversations.load()) {
        cuddle::log_message(cuddle::LogLevel::Error, "failed to load Codex conversations from data/codex_conversations.db");
        return 1;
    }
    cuddle::SessionStore sessions;
    cuddle::App app(users, services, nginx, system_admin, terminal, codex_projects, codex_conversations, sessions);
    cuddle::log_message(cuddle::LogLevel::Info, "starting HTTP server on 0.0.0.0:" + std::to_string(port));
    const bool started = cuddle::run_server(app, port);
    if (!started) {
        cuddle::log_message(cuddle::LogLevel::Error, "server stopped during startup");
        return 1;
    }
    cuddle::log_message(cuddle::LogLevel::Warning, "server loop exited unexpectedly");
    return 1;
}
