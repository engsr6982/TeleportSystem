#include "ll/api/data/KeyValueDB.h"
#include "WarpStorage.h"
#include "ltps/database/DbUtils.h"
#include "ltps/database/StorageKeys.h"
#include "ltps/utils/JsonUtls.h"
#include "ltps/utils/TimeUtils.h"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <mc/deps/core/math/Vec3.h>
#include <mc/world/actor/player/Player.h>
#include <mc/world/level/dimension/VanillaDimensions.h>


namespace ltps::warp {

namespace {

constexpr std::string_view kLegacyBigKey = "warp";

std::string serialize(WarpStorage::Warp const& warp) {
    return json_utils::struct2json(const_cast<WarpStorage::Warp&>(warp)).dump();
}

WarpStorage::Warp deserialize(std::string_view raw) {
    WarpStorage::Warp warp{};
    auto              json = nlohmann::json::parse(raw, nullptr, false);
    if (!json.is_discarded() && json.is_object()) {
        json_utils::json2structTryPatch(warp, json);
    }
    return warp;
}

} // namespace


WarpStorage::WarpStorage(ll::data::KeyValueDB& db) : IStorage(db) {}

std::string_view WarpStorage::getBusinessGroup() const { return "warp"; }

std::vector<std::string> WarpStorage::getNames() const {
    auto raw = getDatabase().get(keys::kIndexWarp);
    return raw.has_value() ? db_utils::fromJsonArray(raw.value()) : std::vector<std::string>{};
}

void WarpStorage::setNames(ll::data::KeyValueDB::WriteBatch& batch, std::vector<std::string> const& names) const {
    batch.set(keys::kIndexWarp, db_utils::toJsonArray(names));
}


void WarpStorage::migrateFromV1(ll::data::KeyValueDB& v1, ll::data::KeyValueDB& v2) {
    auto raw = v1.get(kLegacyBigKey);
    if (!raw.has_value()) {
        return;
    }
    auto json = nlohmann::json::parse(raw.value(), nullptr, false);
    if (json.is_discarded() || !json.is_array()) {
        return;
    }
    std::vector<std::string> names;
    for (auto& element : json) {
        Warp warp{};
        json_utils::json2structTryPatch(warp, element);
        if (warp.name.empty()) {
            continue;
        }
        v2.set(fmt::format(keys::kDataWarp, key_utils::escapeSegment(warp.name)), serialize(warp));
        names.emplace_back(warp.name);
    }
    v2.set(keys::kIndexWarp, db_utils::toJsonArray(names));
}

void WarpStorage::migrateUser(
    ll::data::KeyValueDB::WriteBatch& /*batch*/,
    mce::UUID const& /*uuid*/,
    RealName const& /*name*/,
    std::vector<LegacyRecord> const& /*legacyRecords*/
) {
    // 公共传送点无用户归属, 不存在 legacy 记录
}

void WarpStorage::rebuildIndexes(ll::data::KeyValueDB& db) {
    std::vector<std::string> names;
    db_utils::forEachPrefix(db, keys::kPrefixDataWarp, [&](std::string_view key, std::string_view /*value*/) {
        auto name = db_utils::segmentOf(key, 0, keys::kPrefixDataWarp);
        if (!name.empty()) {
            names.emplace_back(name);
        }
    });
    db.set(keys::kIndexWarp, db_utils::toJsonArray(names));
}


bool WarpStorage::hasWarp(std::string const& name) const {
    return getDatabase().has(fmt::format(keys::kDataWarp, key_utils::escapeSegment(name)));
}

Result<void> WarpStorage::addWarp(Warp warp) {
    if (hasWarp(warp.name)) {
        return ll::makeI18nStringError<"Warp name repeated">();
    }
    auto names = getNames();
    names.emplace_back(warp.name);

    ll::data::KeyValueDB::WriteBatch batch;
    batch.set(fmt::format(keys::kDataWarp, key_utils::escapeSegment(warp.name)), serialize(warp));
    setNames(batch, names);
    getDatabase().write(batch);
    return {};
}

Result<void> WarpStorage::updateWarp(std::string const& name, Warp warp) {
    if (!hasWarp(name)) {
        return ll::makeI18nStringError<"Warp not found">();
    }
    warp.updateModifiedTime();

    ll::data::KeyValueDB::WriteBatch batch;
    if (name != warp.name) {
        // 改名: 落新键 + 删旧键 + 同步索引
        batch.del(fmt::format(keys::kDataWarp, key_utils::escapeSegment(name)));
        batch.set(fmt::format(keys::kDataWarp, key_utils::escapeSegment(warp.name)), serialize(warp));
        auto names = getNames();
        if (auto it = std::find(names.begin(), names.end(), name); it != names.end()) {
            *it = warp.name;
        }
        setNames(batch, names);
    } else {
        batch.set(fmt::format(keys::kDataWarp, key_utils::escapeSegment(name)), serialize(warp));
    }
    getDatabase().write(batch);
    return {};
}

Result<void> WarpStorage::removeWarp(std::string const& name) {
    if (!hasWarp(name)) {
        return ll::makeI18nStringError<"Warp not found">();
    }
    auto names = getNames();
    std::erase(names, name);

    ll::data::KeyValueDB::WriteBatch batch;
    batch.del(fmt::format(keys::kDataWarp, key_utils::escapeSegment(name)));
    setNames(batch, names);
    getDatabase().write(batch);
    return {};
}

std::optional<WarpStorage::Warp> WarpStorage::getWarp(std::string const& name) const {
    auto raw = getDatabase().get(fmt::format(keys::kDataWarp, key_utils::escapeSegment(name)));
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return deserialize(raw.value());
}

WarpStorage::Warps WarpStorage::getWarps() const {
    Warps result;
    for (auto const& name : getNames()) {
        if (auto warp = getWarp(name)) {
            result.emplace_back(std::move(warp.value()));
        }
    }
    return result;
}

WarpStorage::Warps WarpStorage::getWarps(int count) const {
    Warps result;
    result.reserve(static_cast<std::size_t>(std::max(0, count)));
    for (auto const& name : getNames()) {
        if (static_cast<int>(result.size()) >= count) {
            break;
        }
        if (auto warp = getWarp(name)) {
            result.emplace_back(std::move(warp.value()));
        }
    }
    return result;
}

WarpStorage::Warps WarpStorage::queryWarp(std::string const& keyword) const {
    Warps result;
    for (auto const& name : getNames()) {
        if (name.find(keyword) == std::string::npos) {
            continue;
        }
        if (auto warp = getWarp(name)) {
            result.emplace_back(std::move(warp.value()));
        }
    }
    return result;
}


// Warp
WarpStorage::Warp WarpStorage::Warp::make(Vec3 const& vec3, int dimid, std::string const& name) {
    auto time = time_utils::getCurrentTimeString();
    return Warp{
        .x            = vec3.x,
        .y            = vec3.y,
        .z            = vec3.z,
        .dimid        = dimid,
        .createdTime  = time,
        .modifiedTime = std::move(time),
        .name         = name
    };
}

void WarpStorage::Warp::teleport(Player& player) const { player.teleport(Vec3{x, y, z}, dimid, player.getRotation()); }

void WarpStorage::Warp::updateModifiedTime() { modifiedTime = time_utils::getCurrentTimeString(); }

void WarpStorage::Warp::updatePosition(Vec3 const& vec3) {
    x = vec3.x;
    y = vec3.y;
    z = vec3.z;
}

std::string WarpStorage::Warp::toString() const { return "{} => {}"_tr(name, toPosString()); }
std::string WarpStorage::Warp::toPosString() const {
    return "{}({},{},{})"_tr(VanillaDimensions::toString(dimid), x, y, z);
}


} // namespace ltps::warp