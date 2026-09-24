#include "ll/api/data/KeyValueDB.h"
#include "SettingStorage.h"
#include "ll/api/service/PlayerInfo.h"
#include "ltps/database/DbUtils.h"
#include "ltps/database/StorageKeys.h"
#include "ltps/utils/JsonUtls.h"
#include "nlohmann/json.hpp"


namespace ltps::setting {

namespace {

constexpr std::string_view kLegacyBigKey = "rule";

std::string serialize(SettingData const& data) { return json_utils::struct2json(const_cast<SettingData&>(data)).dump(); }

SettingData deserialize(std::string_view raw) {
    SettingData data{};
    auto        json = nlohmann::json::parse(raw, nullptr, false);
    if (!json.is_discarded() && json.is_object()) {
        json_utils::json2structTryPatch(data, json);
    }
    return data;
}

} // namespace


SettingStorage::SettingStorage(ll::data::KeyValueDB& db) : IStorage(db) {}

std::string_view SettingStorage::getBusinessGroup() const { return "setting"; }

void SettingStorage::migrateFromV1(ll::data::KeyValueDB& v1, ll::data::KeyValueDB& v2) {
    auto raw = v1.get(kLegacyBigKey);
    if (!raw.has_value()) {
        return;
    }
    auto json = nlohmann::json::parse(raw.value(), nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        return;
    }
    for (auto& [name, value] : json.items()) {
        auto text = serialize(deserialize(value.dump()));
        if (auto info = ll::service::PlayerInfo::getInstance().fromName(name)) {
            v2.set(fmt::format(keys::kDataSetting, info->uuid.asString()), text);
        } else {
            v2.set(fmt::format(keys::kLegacySetting, key_utils::escapeSegment(name)), text);
        }
    }
}

void SettingStorage::migrateUser(
    ll::data::KeyValueDB::WriteBatch&            batch,
    mce::UUID const&                 uuid,
    RealName const& /*name*/,
    std::vector<LegacyRecord> const& legacyRecords
) {
    for (auto& [key, value] : legacyRecords) {
        batch.set(fmt::format(keys::kDataSetting, uuid.asString()), value);
        batch.del(key);
    }
}

SettingData SettingStorage::getSettingData(mce::UUID const& uuid) const {
    auto raw = getDatabase().get(fmt::format(keys::kDataSetting, uuid.asString()));
    if (!raw.has_value()) {
        return SettingData{};
    }
    return deserialize(raw.value());
}

void SettingStorage::setSettingData(mce::UUID const& uuid, SettingData const& settingData) {
    getDatabase().set(fmt::format(keys::kDataSetting, uuid.asString()), serialize(settingData));
}

void SettingStorage::initPlayerSetting(mce::UUID const& uuid) {
    auto key = fmt::format(keys::kDataSetting, uuid.asString());
    if (!getDatabase().has(key)) {
        getDatabase().set(key, serialize(SettingData{}));
    }
}


} // namespace ltps::setting