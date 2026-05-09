#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cuddle {

struct CodexRequest {
    std::string prompt;
};

struct CodexRuntimeConfig {
    std::string binary_path;
    std::string working_directory;
    std::string model;
    int timeout_seconds = 180;
};

struct CodexResult {
    bool ok = false;
    bool timed_out = false;
    std::string output;
    std::string agent_message;
    std::string change_summary;
    std::vector<std::string> changed_files;
    std::string working_directory;
};

CodexRuntimeConfig codex_runtime_config();
std::optional<CodexRequest> codex_request_from_form(const std::map<std::string, std::string>& form,
                                                    std::string* error_message = nullptr);
bool valid_codex_request(const CodexRequest& request, std::string* error_message = nullptr);
CodexResult run_codex_prompt(const CodexRequest& request);

}
