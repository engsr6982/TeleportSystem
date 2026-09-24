#include "ll/api/data/KeyValueDB.h"
#include "DeathStorage.h"
#include "ll/api/service/PlayerInfo.h"
#include "ltps/TeleportSystem.h"
#include "ltps/base/Config.h"
#include "ltps/database/DbUtils.h"
#include "ltps/database/StorageKeys.h"
#include "ltps/utils/JsonUtls.h"
#include "ltps/utils/TimeUtils.h"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <mc/deps/core/math/Vec3.h>
#include <mc/world/actor/player/Player.h>
#include <mc/world/level/dimension/VanillaDimensions.h>
#include <unordered_set>


namespace ltps::death {

namespace {

constexpr std::string_view kLegacyBigKey = "death";

std::string serialize(DeathStorage::DeathInfo const& info) {
    return json_utils::struct2json(const_cast<DeathStorage::DeathInfo&>(info)).dump();
}

DeathStorage::DeathInfo deserialize(std::string_view raw) {
    DeathStorage::DeathInfo info{};
    auto      json = nlohmann::json::parse(raw, nullptr, false);
    if (!json.is_discarded() && json.is_object()) {
        json_utils::json2structTryPatch(info, json);
    }
    return info;
}

// 由死亡时间推导记录 id (紧凑时间串; 冲突时追加 -n 后缀)
std::string makeId(std::string const& timeString, std::unordered_set<std::string>& used) {
    auto base = time_utils::getCurrentCompactTimeString();
    if (auto tp = time_utils::parseTimeString(timeString)) {
        base = time_utils::toCompactTimeString(tp.value());
    }
    auto id     = base;
    int  suffix = 0;
    while (used.contains(id)) {
        id = fmt::format("{}-{}", base, ++suffix);
    }
    used.insert(id);
    return id;
}

// id 字典序 = 时间序 → 降序即新的在前
void sortIdsNewestFirst(std::vector<std::string>& ids) {
    std::sort(ids.begin(), ids.end(), std::greater<>{});
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
}

} // namespace


DeathStorage::DeathStorage(ll::data::KeyValueDB& db) : IStorage(db) {}

std::string_view DeathStorage::getBusinessGroup() const { return "death"; }

std::vector<std::string> DeathStorage::getIds(mce::UUID const& uuid) const {
    auto raw = getDatabase().get(fmt::format(keys::kIndexDeath, uuid.asString()));
    return raw.has_value() ? db_utils::fromJsonArray(raw.value()) : std::vector<std::string>{};
}

std::optional<DeathStorage::DeathInfo> DeathStorage::getById(mce::UUID const& uuid, std::string_view id) const {
    auto raw = getDatabase().get(fmt::format(keys::kDataDeath, uuid.asString(), id));
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return deserialize(raw.value());
}


void DeathStorage::migrateFromV1(ll::data::KeyValueDB& v1, ll::data::KeyValueDB& v2) {
    auto raw = v1.get(kLegacyBigKey);
    if (!raw.has_value()) {
        return;
    }
    auto json = nlohmann::json::parse(raw.value(), nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        return;
    }

    for (auto& [name, records] : json.items()) {
        if (!records.is_array()) {
            continue;
        }
        std::optional<mce::UUID>          uuid;
        if (auto info = ll::service::PlayerInfo::getInstance().fromName(name)) {
            uuid = info->uuid;
        }
        auto                     escapedName = key_utils::escapeSegment(name);
        std::unordered_set<std::string> used;
        // 索引: 死亡记录 id 列表 (新的在前, 且字典序 = 时间序)
        std::vector<std::string> ids;

        for (auto& record : records) {
            DeathInfo info{};
            json_utils::json2structTryPatch(info, record);
            auto id = makeId(info.time, used);
            if (uuid.has_value()) {
                v2.set(fmt::format(keys::kDataDeath, uuid->asString(), id), serialize(info));
            } else {
                v2.set(fmt::format(keys::kLegacyDeath, escapedName, id), serialize(info));
            }
            ids.emplace_back(std::move(id));
        }

        if (uuid.has_value()) {
            // v1 数组顺序即新的在前
            v2.set(fmt::format(keys::kIndexDeath, uuid->asString()), db_utils::toJsonArray(ids));
        }
    }
}

void DeathStorage::migrateUser(
    ll::data::KeyValueDB::WriteBatch&            batch,
    mce::UUID const&                 uuid,
    RealName const& /*name*/,
    std::vector<LegacyRecord> const& legacyRecords
) {
    if (legacyRecords.empty()) {
        return;
    }
    auto ids = getIds(uuid); // 可能已有 (极端: 迁移前已记录过死亡)
    for (auto& [key, value] : legacyRecords) {
        auto id = db_utils::segmentOf(key, 0, keys::kPrefixLegacyDeath);
        if (id.empty()) {
            continue;
        }
        batch.set(fmt::format(keys::kDataDeath, uuid.asString(), id), value);
        batch.del(key);
        ids.emplace_back(id);
    }
    sortIdsNewestFirst(ids);

    auto maxCount = static_cast<std::size_t>(std::max(0, getConfig().modules.death.maxDeathInfos));
    while (ids.size() > maxCount) {
        batch.del(fmt::format(keys::kDataDeath, uuid.asString(), ids.back()));
        ids.pop_back();
    }
    batch.set(fmt::format(keys::kIndexDeath, uuid.asString()), db_utils::toJsonArray(ids));
}

void DeathStorage::rebuildIndexes(ll::data::KeyValueDB& db) {
    std::unordered_map<std::string, std::vector<std::string>> grouped;
    db_utils::forEachPrefix(db, keys::kPrefixDataDeath, [&](std::string_view key, std::string_view /*value*/) {
        // data:death:<uuid>:<id>
        auto uuid = db_utils::segmentOf(key, 0, keys::kPrefixDataDeath);
        auto id   = db_utils::segmentOf(key, 1, keys::kPrefixDataDeath);
        if (uuid.empty() || id.empty()) {
            return;
        }
        grouped[std::string{uuid}].emplace_back(id);
    });
    for (auto& [uuid, ids] : grouped) {
        sortIdsNewestFirst(ids);
        db.set(fmt::format(keys::kIndexDeath, uuid), db_utils::toJsonArray(ids));
    }
}


bool DeathStorage::hasDeathInfo(mce::UUID const& uuid) const { return !getIds(uuid).empty(); }

void DeathStorage::addDeathInfo(mce::UUID const& uuid, DeathInfo deathInfo) {
    auto& db = getDatabase();

    std::unordered_set<std::string> used;
    for (auto const& existing : getIds(uuid)) {
        used.insert(existing);
    }
    auto id = makeId(deathInfo.time, used);

    auto ids = getIds(uuid);
    ids.insert(ids.begin(), id);

    ll::data::KeyValueDB::WriteBatch batch;
    batch.set(fmt::format(keys::kDataDeath, uuid.asString(), id), serialize(deathInfo));

    auto maxCount = static_cast<std::size_t>(std::max(0, getConfig().modules.death.maxDeathInfos));
    while (ids.size() > maxCount) {
        batch.del(fmt::format(keys::kDataDeath, uuid.asString(), ids.back()));
        ids.pop_back();
    }
    batch.set(fmt::format(keys::kIndexDeath, uuid.asString()), db_utils::toJsonArray(ids));
    db.write(batch);
}

DeathStorage::DeathInfos DeathStorage::getDeathInfos(mce::UUID const& uuid) const {
    DeathInfos result;
    for (auto const& id : getIds(uuid)) {
        if (auto info = getById(uuid, id)) {
            result.emplace_back(std::move(info.value()));
        }
    }
    return result;
}

std::optional<DeathStorage::DeathInfo> DeathStorage::getLatestDeathInfo(mce::UUID const& uuid) const {
    auto ids = getIds(uuid);
    if (ids.empty()) {
        return std::nullopt;
    }
    return getById(uuid, ids.front());
}

std::optional<DeathStorage::DeathInfo> DeathStorage::getSpecificDeathInfo(mce::UUID const& uuid, int index) const {
    auto ids = getIds(uuid);
    if (index < 0 || static_cast<std::size_t>(index) >= ids.size()) {
        return std::nullopt;
    }
    return getById(uuid, ids[static_cast<std::size_t>(index)]);
}

bool DeathStorage::clearDeathInfo(mce::UUID const& uuid) {
    auto ids = getIds(uuid);
    if (ids.empty()) {
        return false;
    }
    ll::data::KeyValueDB::WriteBatch batch;
    for (auto const& id : ids) {
        batch.del(fmt::format(keys::kDataDeath, uuid.asString(), id));
    }
    batch.del(fmt::format(keys::kIndexDeath, uuid.asString()));
    getDatabase().write(batch);
    return true;
}


DeathStorage::DeathInfo DeathStorage::DeathInfo::make(Vec3 const& pos, int dimid) {
    return {.time = time_utils::getCurrentTimeString(), .x = pos.x, .y = pos.y, .z = pos.z, .dimid = dimid};
}

void DeathStorage::DeathInfo::teleport(Player& player) const {
    player.teleport(Vec3(x, y, z), dimid, player.getRotation());
}

std::string DeathStorage::DeathInfo::toString() const { return "{} => {}"_tr(time, toPosString()); }
std::string DeathStorage::DeathInfo::toPosString() const {
    return "{}({},{},{})"_tr(VanillaDimensions::toString(dimid), x, y, z);
}


} // namespace ltps::death