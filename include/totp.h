#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace cuddle {

std::string generate_totp_secret();
std::string build_otpauth_uri(const std::string& issuer,
                              const std::string& account_name,
                              const std::string& secret);
bool valid_totp_code_format(const std::string& code);
std::optional<std::string> totp_code_for_time(const std::string& secret,
                                              std::int64_t unix_time);
bool verify_totp_code(const std::string& secret,
                      const std::string& code,
                      std::int64_t unix_time,
                      int allowed_drift_steps = 1);

}
