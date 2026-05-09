#include "codex_runner.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path);
    file << content;
}

int run_command(const std::string& command) {
    return std::system(command.c_str());
}

}

int main() {
    const auto temp_root = std::filesystem::temp_directory_path() / ("cuddlepanel-codex-tests-" + std::to_string(getpid()));
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(temp_root);

    const auto repo = temp_root / "repo";
    std::filesystem::create_directories(repo);
    write_file(repo / "tracked.txt", "before\n");

    assert(run_command("git init \"" + repo.string() + "\" >/dev/null 2>&1") == 0);
    assert(run_command("git -C \"" + repo.string() + "\" config user.email test@example.com") == 0);
    assert(run_command("git -C \"" + repo.string() + "\" config user.name cuddlePanel") == 0);
    assert(run_command("git -C \"" + repo.string() + "\" add tracked.txt") == 0);
    assert(run_command("git -C \"" + repo.string() + "\" commit -m init >/dev/null 2>&1") == 0);

    const auto fake_codex = temp_root / "fake-codex.sh";
    write_file(fake_codex,
               "#!/bin/sh\n"
               "output_file=\"\"\n"
               "workdir=\"\"\n"
               "prompt=\"\"\n"
               "while [ \"$#\" -gt 0 ]; do\n"
               "  case \"$1\" in\n"
               "    exec) shift ;;\n"
               "    --output-last-message|--cd|--color|--sandbox|--ask-for-approval|--model)\n"
               "      key=\"$1\"\n"
               "      value=\"$2\"\n"
               "      if [ \"$key\" = \"--output-last-message\" ]; then output_file=\"$value\"; fi\n"
               "      if [ \"$key\" = \"--cd\" ]; then workdir=\"$value\"; fi\n"
               "      shift 2 ;;\n"
               "    *) prompt=\"$1\"; shift ;;\n"
               "  esac\n"
               "done\n"
               "printf 'Final Codex message for: %s\\n' \"$prompt\" > \"$output_file\"\n"
               "printf 'CLI output for: %s\\n' \"$prompt\"\n"
               "printf 'changed by codex\\n' >> \"$workdir/tracked.txt\"\n");
    std::filesystem::permissions(fake_codex,
                                 std::filesystem::perms::owner_read |
                                 std::filesystem::perms::owner_write |
                                 std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace);

    setenv("CUDDLEPANEL_CODEX_BIN", fake_codex.c_str(), 1);
    setenv("CUDDLEPANEL_CODEX_WORKDIR", repo.c_str(), 1);
    setenv("CUDDLEPANEL_CODEX_TIMEOUT_SECONDS", "60", 1);
    unsetenv("CUDDLEPANEL_CODEX_MODEL");

    std::string error;
    cuddle::CodexRequest invalid;
    assert(!cuddle::valid_codex_request(invalid, &error));

    std::map<std::string, std::string> form = {
        {"prompt", "Update the tracked file"}
    };
    auto request = cuddle::codex_request_from_form(form, &error);
    assert(request);

    const auto result = cuddle::run_codex_prompt(*request);
    assert(result.ok);
    assert(!result.timed_out);
    assert(result.agent_message.find("Update the tracked file") != std::string::npos);
    assert(result.output.find("CLI output for: Update the tracked file") != std::string::npos);
    assert(result.change_summary.find("tracked.txt") != std::string::npos);
    assert(!result.changed_files.empty());

    std::filesystem::remove_all(temp_root);
    std::cout << "codex tests passed" << std::endl;
    return 0;
}
