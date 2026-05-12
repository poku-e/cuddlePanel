#include "codex_runner.h"

#include "log.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <poll.h>
#include <sstream>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace cuddle {
namespace {

constexpr size_t kMaxPromptBytes = 16000;
constexpr size_t kMaxOutputBytes = 1024 * 1024;
constexpr int kGitProbeTimeoutSeconds = 10;

struct CommandCaptureResult {
    bool ok = false;
    bool timed_out = false;
    int exit_code = -1;
    std::string output;
};

std::string env_or_default(const char* name, const std::string& fallback) {
    const char* configured = std::getenv(name);
    return configured && *configured ? configured : fallback;
}

int env_or_default_int(const char* name, int fallback) {
    const char* configured = std::getenv(name);
    if (!configured || !*configured) {
        return fallback;
    }
    char* end = nullptr;
    const long value = std::strtol(configured, &end, 10);
    if (!end || *end != '\0' || value < 1 || value > 3600) {
        return fallback;
    }
    return static_cast<int>(value);
}

std::string current_working_directory() {
    std::error_code ec;
    const auto path = std::filesystem::current_path(ec);
    return ec ? "/" : path.string();
}

bool valid_absolute_path(const std::string& value, std::size_t max_length) {
    return !value.empty() &&
           value.size() <= max_length &&
           value.front() == '/' &&
           value.find('\0') == std::string::npos;
}

bool valid_optional_model(const std::string& value) {
    if (value.empty() || value.size() > 128) {
        return value.empty();
    }
    for (unsigned char c : value) {
        if (!(std::isalnum(c) || c == '.' || c == ':' || c == '_' || c == '-' || c == '/')) {
            return false;
        }
    }
    return true;
}

bool valid_runtime_config(const CodexRuntimeConfig& config, std::string* error_message) {
    auto fail = [error_message](const std::string& message) {
        if (error_message) {
            *error_message = message;
        }
        return false;
    };

    if (!valid_absolute_path(config.binary_path, 512)) {
        return fail("invalid codex binary path");
    }
    if (access(config.binary_path.c_str(), X_OK) != 0) {
        return fail("codex binary is missing or not executable");
    }
    if (!valid_absolute_path(config.working_directory, 512)) {
        return fail("invalid codex working directory");
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(config.working_directory, ec)) {
        return fail("codex working directory does not exist");
    }
    if (!valid_optional_model(config.model)) {
        return fail("invalid codex model");
    }
    if (config.timeout_seconds < 10 || config.timeout_seconds > 3600) {
        return fail("invalid codex timeout");
    }
    return true;
}

CommandCaptureResult run_command_capture(const std::vector<std::string>& args,
                                         const std::string& working_directory,
                                         int timeout_seconds,
                                         bool search_path = false) {
    CommandCaptureResult result;
    if (args.empty()) {
        result.output = "unable to execute empty command";
        return result;
    }

    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        result.output = "unable to create command pipe";
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        result.output = "unable to fork command";
        return result;
    }

    if (pid == 0) {
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);

        if (!working_directory.empty() && chdir(working_directory.c_str()) != 0) {
            _exit(126);
        }

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        if (search_path) {
            execvp(argv[0], argv.data());
        } else {
            execv(argv[0], argv.data());
        }
        _exit(127);
    }

    close(pipe_fds[1]);
    const int flags = fcntl(pipe_fds[0], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK);
    }

    std::array<char, 4096> buffer{};
    const auto started_at = std::time(nullptr);
    bool child_exited = false;
    int status = 0;

    while (true) {
        pollfd poll_fd{};
        poll_fd.fd = pipe_fds[0];
        poll_fd.events = POLLIN | POLLHUP;
        const int poll_result = poll(&poll_fd, 1, 200);
        if (poll_result > 0 && (poll_fd.revents & (POLLIN | POLLHUP))) {
            while (true) {
                const ssize_t read_count = read(pipe_fds[0], buffer.data(), buffer.size());
                if (read_count > 0) {
                    if (result.output.size() < kMaxOutputBytes) {
                        const size_t remaining = kMaxOutputBytes - result.output.size();
                        result.output.append(buffer.data(), static_cast<size_t>(read_count > static_cast<ssize_t>(remaining) ? remaining : read_count));
                        if (static_cast<size_t>(read_count) > remaining) {
                            result.output.append("\n[output truncated]\n");
                        }
                    }
                    continue;
                }
                if (read_count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    break;
                }
                break;
            }
        }

        const pid_t wait_result = waitpid(pid, &status, WNOHANG);
        if (wait_result == pid) {
            child_exited = true;
        }

        if (!child_exited && timeout_seconds > 0 && (std::time(nullptr) - started_at) >= timeout_seconds) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            result.timed_out = true;
            child_exited = true;
        }

        if (child_exited && !(poll_fd.revents & POLLIN)) {
            while (true) {
                const ssize_t read_count = read(pipe_fds[0], buffer.data(), buffer.size());
                if (read_count <= 0) {
                    break;
                }
                if (result.output.size() < kMaxOutputBytes) {
                    const size_t remaining = kMaxOutputBytes - result.output.size();
                    result.output.append(buffer.data(), static_cast<size_t>(read_count > static_cast<ssize_t>(remaining) ? remaining : read_count));
                    if (static_cast<size_t>(read_count) > remaining) {
                        result.output.append("\n[output truncated]\n");
                    }
                }
            }
            break;
        }
    }

    close(pipe_fds[0]);

    if (result.timed_out) {
        result.output += result.output.empty() ? "command timed out" : "\ncommand timed out";
        result.exit_code = 124;
        return result;
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
        result.ok = result.exit_code == 0;
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }

    if (result.output.empty()) {
        result.output = result.ok ? "command completed successfully" : "command failed";
    }
    return result;
}

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

std::map<std::string, std::string> parse_git_status_snapshot(const std::string& output) {
    std::map<std::string, std::string> status_by_path;
    std::stringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.size() < 4) {
            continue;
        }
        const std::string status = line.substr(0, 2);
        const std::string path = trim_copy(line.substr(3));
        if (!path.empty()) {
            status_by_path[path] = status;
        }
    }
    return status_by_path;
}

CommandCaptureResult git_status_snapshot(const std::string& working_directory) {
    return run_command_capture({"git", "status", "--short", "--untracked-files=all"},
                               working_directory,
                               kGitProbeTimeoutSeconds,
                               true);
}

std::vector<std::string> detect_changed_files(const std::string& before, const std::string& after) {
    const auto before_map = parse_git_status_snapshot(before);
    const auto after_map = parse_git_status_snapshot(after);
    std::vector<std::string> changed_files;
    for (const auto& [path, status] : after_map) {
        auto found = before_map.find(path);
        if (found == before_map.end() || found->second != status) {
            changed_files.push_back(path);
        }
    }
    std::sort(changed_files.begin(), changed_files.end());
    return changed_files;
}

std::string create_temp_path() {
    std::string pattern = "/tmp/cuddlepanel-codex-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const int fd = mkstemp(buffer.data());
    if (fd < 0) {
        return {};
    }
    close(fd);
    return std::string(buffer.data());
}

std::string read_text_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.good()) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

bool command_output_mentions_skip_git_repo_check(const std::vector<std::string>& args) {
    const auto result = run_command_capture(args, "", kGitProbeTimeoutSeconds, false);
    return result.output.find("--skip-git-repo-check") != std::string::npos;
}

}

CodexRuntimeConfig codex_runtime_config() {
    CodexRuntimeConfig config;
    config.binary_path = env_or_default("CUDDLEPANEL_CODEX_BIN", "/usr/bin/codex");
    config.working_directory = env_or_default("CUDDLEPANEL_CODEX_WORKDIR", current_working_directory());
    config.model = env_or_default("CUDDLEPANEL_CODEX_MODEL", "");
    config.timeout_seconds = env_or_default_int("CUDDLEPANEL_CODEX_TIMEOUT_SECONDS", 180);
    return config;
}

bool codex_cli_supports_skip_git_repo_check(const CodexRuntimeConfig& config) {
    static std::mutex cache_mutex;
    static std::map<std::string, bool> cache;

    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        const auto found = cache.find(config.binary_path);
        if (found != cache.end()) {
            return found->second;
        }
    }

    bool supported = false;
    if (valid_absolute_path(config.binary_path, 512) && access(config.binary_path.c_str(), X_OK) == 0) {
        supported =
            command_output_mentions_skip_git_repo_check({config.binary_path, "--help"}) ||
            command_output_mentions_skip_git_repo_check({config.binary_path, "resume", "--help"}) ||
            command_output_mentions_skip_git_repo_check({config.binary_path, "exec", "--help"});
    }

    std::lock_guard<std::mutex> lock(cache_mutex);
    cache[config.binary_path] = supported;
    return supported;
}

std::optional<CodexRequest> codex_request_from_form(const std::map<std::string, std::string>& form,
                                                    std::string* error_message) {
    CodexRequest request;
    auto found = form.find("prompt");
    request.prompt = found == form.end() ? "" : trim_copy(found->second);
    if (!valid_codex_request(request, error_message)) {
        return std::nullopt;
    }
    return request;
}

bool valid_codex_request(const CodexRequest& request, std::string* error_message) {
    if (request.prompt.empty()) {
        if (error_message) {
            *error_message = "prompt is required";
        }
        return false;
    }
    if (request.prompt.size() > kMaxPromptBytes) {
        if (error_message) {
            *error_message = "prompt is too large";
        }
        return false;
    }
    if (request.prompt.find('\0') != std::string::npos) {
        if (error_message) {
            *error_message = "prompt contains invalid characters";
        }
        return false;
    }
    return true;
}

CodexResult run_codex_prompt(const CodexRequest& request) {
    CodexResult result;
    std::string error;
    if (!valid_codex_request(request, &error)) {
        result.output = error;
        return result;
    }

    const auto config = codex_runtime_config();
    result.working_directory = config.working_directory;
    if (!valid_runtime_config(config, &error)) {
        result.output = error;
        return result;
    }

    const auto before_status = git_status_snapshot(config.working_directory);
    if (!before_status.ok && before_status.exit_code != 0) {
        result.output = "codex working directory is not a usable git repository";
        return result;
    }

    const std::string last_message_path = create_temp_path();
    if (last_message_path.empty()) {
        result.output = "unable to prepare codex output capture";
        return result;
    }

    std::vector<std::string> args = {
        config.binary_path,
        "exec",
        "--color", "never",
        "--sandbox", "workspace-write",
        "--ask-for-approval", "never",
        "--cd", config.working_directory,
        "--output-last-message", last_message_path
    };
    if (!config.model.empty()) {
        args.insert(args.end(), {"--model", config.model});
    }
    args.push_back(request.prompt);

    log_message(LogLevel::Info, "running Codex prompt in " + config.working_directory);
    const auto command_result = run_command_capture(args, "", config.timeout_seconds, false);
    result.ok = command_result.ok;
    result.timed_out = command_result.timed_out;
    result.output = command_result.output;
    result.agent_message = trim_copy(read_text_file(last_message_path));
    std::filesystem::remove(last_message_path);

    const auto after_status = git_status_snapshot(config.working_directory);
    result.change_summary = after_status.output;
    result.changed_files = detect_changed_files(before_status.output, after_status.output);

    if (result.timed_out) {
        result.output = result.output.empty() ? "Codex run timed out" : result.output;
        return result;
    }

    if (!result.ok && result.output.empty()) {
        result.output = "Codex command failed";
    }
    if (result.ok && result.output.empty() && !result.agent_message.empty()) {
        result.output = result.agent_message;
    }
    return result;
}

}
