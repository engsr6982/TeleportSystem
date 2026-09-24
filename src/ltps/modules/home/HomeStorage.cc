#include "ll/api/data/KeyValueDB.h"
#include "ltps/modules/home/HomeStorage.h"
#include "ll/api/service/PlayerInfo.h"
#include "ltps/database/DbUtils.h"
#include "ltps/database/StorageKeys.h"
#include "ltps/utils/JsonUtls.h"
#include "ltps/utils/McUtils.h"
#include "ltps/utils/TimeUtils.h"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <mc/deps/core/math/Vec3.h>
#include <mc/world/actor/player/Player.h>
#include <mc/world/level/dimension/VanillaDimensions.h>
#include <set>


namespace ltps::home {

namespace {

constexpr std::string_view kLegacyBigKey = "home";

std::string serialize(HomeStorage::Home const& home) {
    return json_utils::struct2json(const_cast<HomeStorage::Home&>(home)).dump();
}

HomeStorage::Home deserialize(std::string_view raw) {
    HomeStorage::Home home{};
    auto              json = nlohmann::json::parse(raw, nullptr, false);
    if (!json.is_discarded() && json.is_object()) {
        json_utils::json2structTryPatch(home, json);
    }
    return home;
}

} // namespace


HomeStorage::HomeStorage(ll::data::KeyValueDB& db) : IStorage(db) {}

std::string_view HomeStorage::getBusinessGroup() const { return "home"; }

std::vector<std::string> HomeStorage::getNames(mce::UUID const& uuid) const {
    auto raw = getDatabase().get(fmt::format(keys::kIndexHome, uuid.asString()));
    return raw.has_value() ? db_utils::fromJsonArray(raw.value()) : std::vector<std::string>{};
}

void HomeStorage::setNames(ll::data::KeyValueDB::WriteBatch& batch, mce::UUID const& uuid, std::vector<std::string> const& names)
    const {
    batch.set(fmt::format(keys::kIndexHome, uuid.asString()), db_utils::toJsonArray(names));
}


void HomeStorage::migrateFromV1(ll::data::KeyValueDB& v1, ll::data::KeyValueDB& v2) {
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
        std::optional<mce::UUID> uuid;
        if (auto info = ll::service::PlayerInfo::getInstance().fromName(name)) {
            uuid = info->uuid;
        }
        auto                     escapedName = key_utils::escapeSegment(name);
        std::vector<std::string> homeNames;

        for (auto& record : records) {
            Home home{};
            json_utils::json2structTryPatch(home, record);
            if (home.name.empty()) {
                continue;
            }
            if (uuid.has_value()) {
                v2.set(
                    fmt::format(keys::kDataHome, uuid->asString(), key_utils::escapeSegment(home.name)),
                    serialize(home)
                );
            } else {
                v2.set(
                    fmt::format(keys::kLegacyHome, escapedName, key_utils::escapeSegment(home.name)),
                    serialize(home)
                );
            }
            homeNames.emplace_back(home.name);
        }

        if (uuid.has_value()) {
            v2.set(fmt::format(keys::kIndexHome, uuid->asString()), db_utils::toJsonArray(homeNames));
        }
    }
}

void HomeStorage::migrateUser(
    ll::data::KeyValueDB::WriteBatch&            batch,
    mce::UUID const&                 uuid,
    RealName const& /*name*/,
    std::vector<LegacyRecord> const& legacyRecords
) {
    if (legacyRecords.empty()) {
        return;
    }
    auto names = getNames(uuid);
    for (auto& [key, value] : legacyRecords) {
        // legacy:home:<realName>:<homeName>
        auto escapedHomeName = db_utils::segmentOf(key, 1, keys::kPrefixLegacyHome);
        if (escapedHomeName.empty()) {
            continue;
        }
        auto home = deserialize(value);
        if (home.name.empty()) {
            home.name = escapedHomeName;
        }
        batch.set(
            fmt::format(keys::kDataHome, uuid.asString(), key_utils::escapeSegment(home.name)),
            value
        );
        batch.del(key);
        names.emplace_back(home.name);
    }
    setNames(batch, uuid, names);
}

void HomeStorage::rebuildIndexes(ll::data::KeyValueDB& db) {
    std::unordered_map<std::string, std::vector<std::string>> grouped;
    db_utils::forEachPrefix(db, keys::kPrefixDataHome, [&](std::string_view key, std::string_view value) {
        // data:home:<uuid>:<homeName>
        auto uuid = db_utils::segmentOf(key, 0, keys::kPrefixDataHome);
        if (uuid.empty()) {
            return;
        }
        auto home = deserialize(value);
        if (home.name.empty()) {
            home.name = std::string{db_utils::segmentOf(key, 1, keys::kPrefixDataHome)};
        }
        grouped[std::string{uuid}].emplace_back(home.name);
    });
    for (auto& [uuid, names] : grouped) {
        db.set(fmt::format(keys::kIndexHome, uuid), db_utils::toJsonArray(names));
    }
}


bool HomeStorage::hasPlayer(mce::UUID const& uuid) const { return !getNames(uuid).empty(); }

bool HomeStorage::hasHome(mce::UUID const& uuid, std::string const& name) const {
    return getDatabase().has(fmt::format(keys::kDataHome, uuid.asString(), key_utils::escapeSegment(name)));
}

std::optional<HomeStorage::Home> HomeStorage::getHome(mce::UUID const& uuid, std::string const& name) const {
    auto raw = getDatabase().get(fmt::format(keys::kDataHome, uuid.asString(), key_utils::escapeSegment(name)));
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return deserialize(raw.value());
}

Result<void> HomeStorage::updateHome(mce::UUID const& uuid, std::string const& name, Home home) {
    if (!hasHome(uuid, name)) {
        return ll::makeI18nStringError<"Home not found">();
    }
    home.updateModifiedTime();

    ll::data::KeyValueDB::WriteBatch batch;
    if (name != home.name) {
        // 改名: 落新键 + 删旧键 + 同步索引
        batch.del(fmt::format(keys::kDataHome, uuid.asString(), key_utils::escapeSegment(name)));
        batch.set(fmt::format(keys::kDataHome, uuid.asString(), key_utils::escapeSegment(home.name)), serialize(home));
        auto names = getNames(uuid);
        if (auto it = std::find(names.begin(), names.end(), name); it != names.end()) {
            *it = home.name;
        }
        setNames(batch, uuid, names);
    } else {
        batch.set(fmt::format(keys::kDataHome, uuid.asString(), key_utils::escapeSegment(name)), serialize(home));
    }
    getDatabase().write(batch);
    return {};
}

Result<void> HomeStorage::addHome(mce::UUID const& uuid, Home home) {
    if (hasHome(uuid, home.name)) {
        return ll::makeI18nStringError<"Home name repeated">();
    }
    auto names = getNames(uuid);
    names.emplace_back(home.name);

    ll::data::KeyValueDB::WriteBatch batch;
    batch.set(fmt::format(keys::kDataHome, uuid.asString(), key_utils::escapeSegment(home.name)), serialize(home));
    setNames(batch, uuid, names);
    getDatabase().write(batch);
    return {};
}

Result<void> HomeStorage::removeHome(mce::UUID const& uuid, std::string const& name) {
    if (!hasHome(uuid, name)) {
        return ll::makeI18nStringError<"Home not found">();
    }
    auto names = getNames(uuid);
    std::erase(names, name);

    ll::data::KeyValueDB::WriteBatch batch;
    batch.del(fmt::format(keys::kDataHome, uuid.asString(), key_utils::escapeSegment(name)));
    setNames(batch, uuid, names);
    getDatabase().write(batch);
    return {};
}

int HomeStorage::getHomeCount(mce::UUID const& uuid) const { return static_cast<int>(getNames(uuid).size()); }

HomeStorage::Homes HomeStorage::getHomes(mce::UUID const& uuid) const {
    Homes result;
    for (auto const& name : getNames(uuid)) {
        if (auto home = getHome(uuid, name)) {
            result.emplace_back(std::move(home.value()));
        }
    }
    return result;
}

std::vector<RealName> HomeStorage::getAllOwnerNames() const {
    auto& db = getDatabase();

    std::vector<RealName> result;
    // 1) 已 uuid 化: 档案名 + 存在家园索引
    db_utils::forEachPrefix(db, keys::kPrefixDataUser, [&](std::string_view key, std::string_view value) {
        // data:user:<uuid>:profile
        auto uuid = db_utils::segmentOf(key, 0, keys::kPrefixDataUser);
        if (uuid.empty()) {
            return;
        }
        auto indexRaw = db.get(fmt::format(keys::kIndexHome, uuid));
        if (!indexRaw.has_value() || db_utils::fromJsonArray(indexRaw.value()).empty()) {
            return; // 无家园的玩家不进管理列表
        }
        auto json = nlohmann::json::parse(value, nullptr, false);
        if (!json.is_discarded() && json.is_object()) {
            result.emplace_back(json.value("name", std::string{uuid}));
        }
    });

    // 2) 未迁移 (legacy): 名字段直接展示
    std::set<std::string> legacyNames;
    db_utils::forEachPrefix(db, keys::kPrefixLegacyHome, [&](std::string_view key, std::string_view /*value*/) {
        auto name = db_utils::segmentOf(key, 0, keys::kPrefixLegacyHome);
        if (!name.empty()) {
            legacyNames.emplace(name);
        }
    });
    result.insert(result.end(), legacyNames.begin(), legacyNames.end());

    return result;
}


HomeStorage::Home HomeStorage::Home::make(Vec3 const& vec3, int dimid, std::string const& name) {
    auto time = time_utils::getCurrentTimeString();
    return Home{
        .x            = vec3.x,
        .y            = vec3.y,
        .z            = vec3.z,
        .dimid        = dimid,
        .createdTime  = time,
        .modifiedTime = std::move(time),
        .name         = name
    };
}

void HomeStorage::Home::teleport(Player& player) const { player.teleport(Vec3{x, y, z}, dimid, player.getRotation()); }

void HomeStorage::Home::updateModifiedTime() { modifiedTime = time_utils::getCurrentTimeString(); }

void HomeStorage::Home::updatePosition(Vec3 const& vec3) {
    x = vec3.x;
    y = vec3.y;
    z = vec3.z;
}

std::string HomeStorage::Home::toString() const { return "{} => {}"_tr(name, toPosString()); }
std::string HomeStorage::Home::toPosString() const {
    return "{}({},{},{})"_tr(VanillaDimensions::toString(dimid), x, y, z);
}


} // namespace ltps::home