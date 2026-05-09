#include "auth.h"

#include <sodium.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace cuddle {

bool init_crypto() {
    return sodium_init() >= 0;
}

std::optional<std::string> hash_password(const std::string& password) {
    if (!valid_password(password)) {
        return std::nullopt;
    }

    std::vector<char> out(crypto_pwhash_STRBYTES);
    if (crypto_pwhash_str(out.data(),
                          password.c_str(),
                          password.size(),
                          crypto_pwhash_OPSLIMIT_INTERACTIVE,
                          crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        return std::nullopt;
    }
    return std::string(out.data());
}

bool verify_password(const std::string& password, const std::string& hash) {
    if (password.empty() || hash.rfind(crypto_pwhash_STRPREFIX, 0) != 0) {
        return false;
    }
    return crypto_pwhash_str_verify(hash.c_str(), password.c_str(), password.size()) == 0;
}

std::string random_token() {
    constexpr size_t token_bytes = 32;
    std::vector<unsigned char> bytes(token_bytes);
    randombytes_buf(bytes.data(), bytes.size());

    std::string encoded(token_bytes * 2, '\0');
    sodium_bin2hex(encoded.data(), encoded.size() + 1, bytes.data(), bytes.size());
    return encoded;
}

bool valid_username(const std::string& username) {
    if (username.size() < 3 || username.size() > 64) {
        return false;
    }
    return std::all_of(username.begin(), username.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-' || c == '.';
    });
}

bool valid_password(const std::string& password) {
    if (password.size() < 14 || password.size() > 1024) {
        return false;
    }

    bool upper = false;
    bool lower = false;
    bool digit = false;
    bool other = false;
    for (unsigned char c : password) {
        upper = upper || std::isupper(c);
        lower = lower || std::islower(c);
        digit = digit || std::isdigit(c);
        other = other || (!std::isalnum(c) && !std::isspace(c));
    }
    return upper && lower && digit && other;
}

}
