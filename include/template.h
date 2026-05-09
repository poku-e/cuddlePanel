#pragma once

#include <map>
#include <optional>
#include <string>

namespace cuddle {

std::optional<std::string> read_file(const std::string& path);
std::string render_template(const std::string& path, const std::map<std::string, std::string>& values = {});
std::string static_asset_path(const std::string& route_path);

}
