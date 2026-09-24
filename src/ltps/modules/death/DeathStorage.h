#pragma once
#include "ltps/Global.h"
#include "ltps/database/IStorage.h"
#include "mc/platform/UUID.h"
#include <optional>
#include <string_view>
#include <vector>


class Vec3;
class Player;

namespace ltps::death {

class DeathStorage final : public IStorage {
public:
    struct DeathInfo {
        std::string time;    // 死亡时间
        float       x, y, z; // 死亡位置
        int         dimid;   // 维度ID

        TPSNDAPI static DeathInfo make(Vec3 const& pos, int dimid);

        TPSAPI void teleport(Player& player) const;

        TPSNDAPI std::string toString() const;
        TPSNDAPI std::string toPosString() const;
    };
    using DeathInfos = std::vector<DeathInfo>;

private:
    // 索引: 死亡记录 id 列表 (新的在前, 且字典序 = 时间序)
    TPSNDAPI std::vector<std::string> getIds(mce::UUID const& uuid) const;

    TPSNDAPI std::optional<DeathInfo> getById(mce::UUID const& uuid, std::string_view id) const;

public:
    TPS_DISALLOW_COPY(DeathStorage);

    TPSAPI explicit DeathStorage(ll::data::KeyValueDB& db);

    TPSNDAPI std::string_view getBusinessGroup() const override;

    TPSAPI void migrateFromV1(ll::data::KeyValueDB& v1, ll::data::KeyValueDB& v2) override;
    TPSAPI void migrateUser(
        ll::data::KeyValueDB::WriteBatch&            batch,
        mce::UUID const&                 uuid,
        RealName const&                  name,
        std::vector<LegacyRecord> const& legacyRecords
    ) override;
    TPSAPI void rebuildIndexes(ll::data::KeyValueDB& db) override;

    TPSNDAPI bool hasDeathInfo(mce::UUID const& uuid) const;

    TPSAPI void addDeathInfo(mce::UUID const& uuid, DeathInfo deathInfo);

    TPSNDAPI DeathInfos getDeathInfos(mce::UUID const& uuid) const;

    TPSNDAPI std::optional<DeathInfo> getLatestDeathInfo(mce::UUID const& uuid) const;
    TPSNDAPI std::optional<DeathInfo> getSpecificDeathInfo(mce::UUID const& uuid, int index) const;

    TPSAPI bool clearDeathInfo(mce::UUID const& uuid);
};


} // namespace ltps::death