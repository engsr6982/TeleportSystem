#pragma once
#include "ltps/Global.h"
#include "ltps/database/IStorage.h"
#include "mc/platform/UUID.h"
#include <string_view>


namespace ltps::setting {

struct SettingData {
    bool deathPopup = true; // 死亡后立即发送返回弹窗
    bool allowTpa   = true; // 允许对xx发送tpa请求
    bool tpaPopup   = true; // tpa弹窗
};


class SettingStorage final : public IStorage {
public:
    TPS_DISALLOW_COPY_AND_MOVE(SettingStorage);

    TPSAPI explicit SettingStorage(ll::data::KeyValueDB& db);

    TPSNDAPI std::string_view getBusinessGroup() const override;

    TPSAPI void migrateFromV1(ll::data::KeyValueDB& v1, ll::data::KeyValueDB& v2) override;
    TPSAPI void migrateUser(
        ll::data::KeyValueDB::WriteBatch&            batch,
        mce::UUID const&                 uuid,
        RealName const&                  name,
        std::vector<LegacyRecord> const& legacyRecords
    ) override;

    // 无记录时返回默认值 (新玩家的常态路径)
    TPSNDAPI SettingData getSettingData(mce::UUID const& uuid) const;

    TPSAPI void setSettingData(mce::UUID const& uuid, SettingData const& settingData);

    TPSAPI void initPlayerSetting(mce::UUID const& uuid);
};


} // namespace ltps::setting