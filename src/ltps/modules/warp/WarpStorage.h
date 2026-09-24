#pragma once
#include "ltps/Global.h"
#include "ltps/database/IStorage.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Vec3;
class Player;

namespace ltps::warp {

class WarpStorage final : public IStorage {
public:
    struct Warp {
        float       x, y, z;      // 位置
        int         dimid;        // 维度
        std::string createdTime;  // 创建时间
        std::string modifiedTime; // 修改时间
        std::string name;         // 名称

        TPSNDAPI static Warp make(Vec3 const& vec3, int dimid, std::string const& name);

        TPSAPI void teleport(Player& player) const;

        TPSAPI void updateModifiedTime();

        TPSAPI void updatePosition(Vec3 const& vec3);

        TPSNDAPI std::string toString() const;
        TPSNDAPI std::string toPosString() const;
    };
    using Warps = std::vector<Warp>;

private:
    // 索引: 传送点名列表 (保持添加顺序)
    TPSNDAPI std::vector<std::string> getNames() const;

    TPSAPI void setNames(ll::data::KeyValueDB::WriteBatch& batch, std::vector<std::string> const& names) const;

public:
    TPS_DISALLOW_COPY_AND_MOVE(WarpStorage);

    TPSAPI explicit WarpStorage(ll::data::KeyValueDB& db);

    TPSNDAPI std::string_view getBusinessGroup() const override;

    TPSAPI void migrateFromV1(ll::data::KeyValueDB& v1, ll::data::KeyValueDB& v2) override;
    TPSAPI void migrateUser(
        ll::data::KeyValueDB::WriteBatch&            batch,
        mce::UUID const&                 uuid,
        RealName const&                  name,
        std::vector<LegacyRecord> const& legacyRecords
    ) override;
    TPSAPI void rebuildIndexes(ll::data::KeyValueDB& db) override;

    TPSNDAPI bool hasWarp(std::string const& name) const;

    TPSNDAPI Result<void> addWarp(Warp warp);

    TPSNDAPI Result<void> updateWarp(std::string const& name, Warp warp);

    TPSNDAPI Result<void> removeWarp(std::string const& name);

    TPSNDAPI std::optional<Warp> getWarp(std::string const& name) const;

    TPSNDAPI Warps getWarps() const;

    TPSNDAPI Warps getWarps(int count) const;

    TPSNDAPI Warps queryWarp(std::string const& keyword) const; // 模糊查询
};


} // namespace ltps::warp