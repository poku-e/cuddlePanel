#pragma once

#include <optional>
#include <string>

namespace cuddle {

bool init_crypto();
std::optional<std::string> hash_password(const std::string& password);
bool verify_password(const std::string& password, const std::string& hash);
std::string random_token();
bool valid_username(const std::string& username);
bool valid_password(const std::string& password);

}
