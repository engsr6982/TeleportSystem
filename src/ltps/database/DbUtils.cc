#include "ltps/database/DbUtils.h"
#include "ll/api/data/KeyValueDB.h"
#include "nlohmann/json.hpp"


namespace ltps {

namespace key_utils {

std::string escapeSegment(std::string_view segment) {
    if (segment.find_first_of("\\:") == std::string_view::npos) {
        return std::string{segment};
    }
    std::string result;
    result.reserve(segment.size() + 4);
    for (char c : segment) {
        if (c == '\\' || c == ':') {
            result.push_back('\\');
        }
        result.push_back(c);
    }
    return result;
}

} // namespace key_utils


namespace db_utils {

void forEachPrefix(
    ll::data::KeyValueDB const&                                       db,
    std::string_view                                                  prefix,
    std::function<void(std::string_view key, std::string_view value)> const& fn
) {
    for (auto&& [key, value] : db.iter()) {
        if (!key.starts_with(prefix)) {
            continue;
        }
        fn(key, value);
    }
}

std::string toJsonArray(std::vector<std::string> const& items) {
    return nlohmann::json{items}.dump();
}

std::vector<std::string> fromJsonArray(std::string_view raw) {
    auto json = nlohmann::json::parse(raw, nullptr, false);
    if (json.is_discarded() || !json.is_array()) {
        return {};
    }
    std::vector<std::string> result;
    result.reserve(json.size());
    for (auto& item : json) {
        if (item.is_string()) {
            result.emplace_back(item.get<std::string>());
        }
    }
    return result;
}

std::string_view segmentOf(std::string_view key, std::size_t index, std::string_view prefix) {
    if (!key.starts_with(prefix)) {
        return {};
    }
    std::string_view rest = key.substr(prefix.size());
    for (std::size_t i = 0; i < index; ++i) {
        auto pos = rest.find(':');
        if (pos == std::string_view::npos) {
            return {};
        }
        rest = rest.substr(pos + 1);
    }
    auto pos = rest.find(':');
    return (pos == std::string_view::npos) ? rest : rest.substr(0, pos);
}

} // namespace db_utils


} // namespace ltps