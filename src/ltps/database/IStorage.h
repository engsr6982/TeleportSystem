#pragma once
#include "ll/api/data/KeyValueDB.h"
#include "ltps/Global.h"

#include "mc/platform/UUID.h"
#include <string>
#include <string_view>
#include <utility>
#include <vector>


namespace ltps {

// legacy 组记录的原始键值对
using LegacyRecord = std::pair<std::string, std::string>;

class IStorage {
    friend class StorageManager;

public:
    virtual ~IStorage() = default;

    // 业务组名, 用于 legacy 记录按组分发
    [[nodiscard]] virtual std::string_view getBusinessGroup() const = 0;

    // 启动迁移: 拆解 v1 大键为 v2 逐条记录 + 索引构建
    virtual void migrateFromV1(ll::data::KeyValueDB& v1, ll::data::KeyValueDB& v2) = 0;

    // 懒迁移: 把该玩家归属 legacy 组的记录搬成 uuid 键
    virtual void migrateUser(
        ll::data::KeyValueDB::WriteBatch& batch,
        mce::UUID const&                  uuid,
        RealName const&                   name,
        std::vector<LegacyRecord> const&  legacyRecords
    ) = 0;

    // 迁移旧版独立文件 (如 permission.json); 无此类文件的 Storage 用默认空实现
    virtual void migrateLegacyFiles(ll::data::KeyValueDB& /*v2*/) {}

    // 索引重建 (无索引的 Storage 用默认空实现)
    virtual void rebuildIndexes(ll::data::KeyValueDB& /*db*/) {}

protected:
    explicit IStorage(ll::data::KeyValueDB& database) : mDatabase(database) {}

    [[nodiscard]] inline ll::data::KeyValueDB& getDatabase() const { return mDatabase; }

private:
    ll::data::KeyValueDB& mDatabase;
};


} // namespace ltps