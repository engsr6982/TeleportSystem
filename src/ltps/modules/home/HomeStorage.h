#pragma once
#include "ltps/Global.h"
#include "ltps/database/IStorage.h"
#include "mc/platform/UUID.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Vec3;
class Player;

namespace ltps::home {


class HomeStorage final : public IStorage {
public:
    struct Home {
        float       x, y, z;      // 位置
        int         dimid;        // 维度
        std::string createdTime;  // 创建时间
        std::string modifiedTime; // 修改时间
        std::string name;         // 名称

        TPSNDAPI static Home make(Vec3 const& vec3, int dimid, std::string const& name);

        TPSAPI void teleport(Player& player) const;

        TPSAPI void updateModifiedTime();

        TPSAPI void updatePosition(Vec3 const& vec3);

        TPSNDAPI std::string toString() const;
        TPSNDAPI std::string toPosString() const;
    };
    using Homes = std::vector<Home>;

private:
    // 索引: 家园名列表
    TPSNDAPI std::vector<std::string> getNames(mce::UUID const& uuid) const;

    TPSAPI void setNames(ll::data::KeyValueDB::WriteBatch& batch, mce::UUID const& uuid, std::vector<std::string> const& names)
        const;

public:
    TPS_DISALLOW_COPY_AND_MOVE(HomeStorage);

    TPSAPI explicit HomeStorage(ll::data::KeyValueDB& db);

    TPSNDAPI std::string_view getBusinessGroup() const override;

    TPSAPI void migrateFromV1(ll::data::KeyValueDB& v1, ll::data::KeyValueDB& v2) override;
    TPSAPI void migrateUser(
        ll::data::KeyValueDB::WriteBatch&            batch,
        mce::UUID const&                 uuid,
        RealName const&                  name,
        std::vector<LegacyRecord> const& legacyRecords
    ) override;
    TPSAPI void rebuildIndexes(ll::data::KeyValueDB& db) override;

    TPSNDAPI bool hasPlayer(mce::UUID const& uuid) const;

    TPSNDAPI bool hasHome(mce::UUID const& uuid, std::string const& name) const;

    TPSNDAPI std::optional<Home> getHome(mce::UUID const& uuid, std::string const& name) const;

    TPSNDAPI Result<void> updateHome(mce::UUID const& uuid, std::string const& name, Home home);

    TPSNDAPI Result<void> addHome(mce::UUID const& uuid, Home home);

    TPSNDAPI Result<void> removeHome(mce::UUID const& uuid, std::string const& name);

    TPSNDAPI int getHomeCount(mce::UUID const& uuid) const;

    TPSNDAPI Homes getHomes(mce::UUID const& uuid) const;

    // 管理面板: 拥有家园的玩家名列表 (已 uuid 化的用档案名, 未迁移的用 legacy 名)
    TPSNDAPI std::vector<RealName> getAllOwnerNames() const;
};


} // namespace ltps::home