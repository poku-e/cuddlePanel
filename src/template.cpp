#include "template.h"

#include <fstream>
#include <sstream>

namespace cuddle {

std::optional<std::string> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string render_template(const std::string& path, const std::map<std::string, std::string>& values) {
    auto content = read_file(path).value_or("Template not found");
    for (const auto& [key, value] : values) {
        std::string token = "{{" + key + "}}";
        size_t pos = 0;
        while ((pos = content.find(token, pos)) != std::string::npos) {
            content.replace(pos, token.size(), value);
            pos += value.size();
        }
    }
    return content;
}

std::string static_asset_path(const std::string& route_path) {
    if (route_path.rfind("/static/", 0) != 0 || route_path.find("..") != std::string::npos) {
        return {};
    }
    return "." + route_path;
}

}
