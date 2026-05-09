#include "totp.h"

#include <openssl/hmac.h>
#include <sodium.h>

#include <array>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace cuddle {
namespace {

constexpr char kBase32Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

std::optional<std::vector<unsigned char>> decode_base32(const std::string& value) {
    std::vector<unsigned char> output;
    int buffer = 0;
    int bits_left = 0;
    for (unsigned char c : value) {
        if (c == '=' || std::isspace(c)) {
            continue;
        }
        c = static_cast<unsigned char>(std::toupper(c));
        const char* pos = std::strchr(kBase32Alphabet, c);
        if (!pos) {
            return std::nullopt;
        }
        buffer = (buffer << 5) | static_cast<int>(pos - kBase32Alphabet);
        bits_left += 5;
        if (bits_left >= 8) {
            bits_left -= 8;
            output.push_back(static_cast<unsigned char>((buffer >> bits_left) & 0xFF));
        }
    }
    return output;
}

std::string encode_base32(const std::vector<unsigned char>& bytes) {
    std::string output;
    int buffer = 0;
    int bits_left = 0;
    for (unsigned char byte : bytes) {
        buffer = (buffer << 8) | byte;
        bits_left += 8;
        while (bits_left >= 5) {
            bits_left -= 5;
            output.push_back(kBase32Alphabet[(buffer >> bits_left) & 0x1F]);
        }
    }
    if (bits_left > 0) {
        output.push_back(kBase32Alphabet[(buffer << (5 - bits_left)) & 0x1F]);
    }
    return output;
}

std::string url_encode(const std::string& value) {
    std::string out;
    char hex[4] = {};
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            std::snprintf(hex, sizeof(hex), "%%%02X", c);
            out += hex;
        }
    }
    return out;
}

std::optional<std::string> hotp_code(const std::vector<unsigned char>& secret, std::uint64_t counter) {
    unsigned char counter_bytes[8] = {};
    for (int i = 7; i >= 0; --i) {
        counter_bytes[i] = static_cast<unsigned char>(counter & 0xFF);
        counter >>= 8;
    }

    unsigned char digest[EVP_MAX_MD_SIZE] = {};
    unsigned int digest_len = 0;
    if (!HMAC(EVP_sha1(),
              secret.data(),
              static_cast<int>(secret.size()),
              counter_bytes,
              sizeof(counter_bytes),
              digest,
              &digest_len)) {
        return std::nullopt;
    }
    if (digest_len < 20) {
        return std::nullopt;
    }

    const int offset = digest[digest_len - 1] & 0x0F;
    const std::uint32_t binary =
        ((digest[offset] & 0x7F) << 24) |
        ((digest[offset + 1] & 0xFF) << 16) |
        ((digest[offset + 2] & 0xFF) << 8) |
        (digest[offset + 3] & 0xFF);
    const std::uint32_t otp = binary % 1000000U;

    char buffer[7] = {};
    std::snprintf(buffer, sizeof(buffer), "%06u", otp);
    return std::string(buffer);
}

}

std::string generate_totp_secret() {
    std::vector<unsigned char> secret_bytes(20);
    randombytes_buf(secret_bytes.data(), secret_bytes.size());
    return encode_base32(secret_bytes);
}

std::string build_otpauth_uri(const std::string& issuer,
                              const std::string& account_name,
                              const std::string& secret) {
    const std::string label = url_encode(issuer + ":" + account_name);
    return "otpauth://totp/" + label +
           "?secret=" + url_encode(secret) +
           "&issuer=" + url_encode(issuer) +
           "&algorithm=SHA1&digits=6&period=30";
}

bool valid_totp_code_format(const std::string& code) {
    if (code.size() != 6) {
        return false;
    }
    for (unsigned char c : code) {
        if (!std::isdigit(c)) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> totp_code_for_time(const std::string& secret,
                                              std::int64_t unix_time) {
    auto decoded = decode_base32(secret);
    if (!decoded || decoded->empty()) {
        return std::nullopt;
    }
    const std::uint64_t counter = static_cast<std::uint64_t>(unix_time / 30);
    return hotp_code(*decoded, counter);
}

bool verify_totp_code(const std::string& secret,
                      const std::string& code,
                      std::int64_t unix_time,
                      int allowed_drift_steps) {
    if (!valid_totp_code_format(code)) {
        return false;
    }
    for (int drift = -allowed_drift_steps; drift <= allowed_drift_steps; ++drift) {
        const auto candidate = totp_code_for_time(secret, unix_time + (drift * 30));
        if (candidate && *candidate == code) {
            return true;
        }
    }
    return false;
}

}
