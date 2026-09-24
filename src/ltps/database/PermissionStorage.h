#pragma once
#include "ltps/Global.h"
#include "ltps/database/IStorage.h"
#include "mc/platform/UUID.h"
#include "nlohmann/json_fwd.hpp"
#include <optional>
#include <string_view>
#include <utility>
#include <vector>


namespace ltps {


class PermissionStorage final : public IStorage {
private:
    // 展开旧版 {mDefaultPerms, mPlayerPerms} 数据到 v2 key
    void expandLegacyData(nlohmann::json const& json, ll::data::KeyValueDB& v2);

public:
    // 旧数据兼容
    TPSNDAPI bool _hasLegacyPermissionFile() const;
    TPSAPI void   _renameLegacyPermissionFile() const;

public:
    enum class Permission : int {
        None          = 0,      // 无权限
        AddWarp       = 1 << 0, // 添加传送点
        RemoveWarp    = 1 << 1, // 删除传送点
        EditWarp      = 1 << 2, // 编辑传送点
        ManagerPanel  = 1 << 3, // 管理面板
        UnlimitedHome = 1 << 4, // 无限传送点
    };

    using Permissions = std::vector<Permission>;

    TPS_DISALLOW_COPY_AND_MOVE(PermissionStorage);

    TPSAPI explicit PermissionStorage(ll::data::KeyValueDB& db);

    TPSNDAPI std::string_view getBusinessGroup() const override;

    TPSAPI void migrateFromV1(ll::data::KeyValueDB& v1, ll::data::KeyValueDB& v2) override;
    TPSAPI void migrateLegacyFiles(ll::data::KeyValueDB& v2) override;
    TPSAPI void migrateUser(
        ll::data::KeyValueDB::WriteBatch& batch,
        mce::UUID const&                  uuid,
        RealName const&                   name,
        std::vector<LegacyRecord> const&  legacyRecords
    ) override;

    /**
     * @brief 判断是否有默认权限
     */
    TPSNDAPI bool hasDefaultPermission(Permission permission) const;

    /**
     * @brief 判断玩家是否有权限
     * @param includeDefault 是否包含默认权限
     */
    TPSNDAPI bool hasPermission(mce::UUID const& uuid, Permission permission, bool includeDefault = true) const;

    /**
     * @brief 授予玩家权限
     */
    TPSAPI Result<void> grantPermission(mce::UUID const& uuid, Permission permission);

    /**
     * @brief 撤销玩家权限
     */
    TPSAPI Result<void> revokePermission(mce::UUID const& uuid, Permission permission);

    /**
     * @brief 获取玩家权限列表
     */
    TPSNDAPI Permissions getPermissions(mce::UUID const& uuid) const;

    /**
     * @brief 授予默认权限
     */
    TPSAPI Result<void> grantDefaultPermission(Permission permission);

    /**
     * @brief 撤销默认权限
     */
    TPSAPI Result<void> revokeDefaultPermission(Permission permission);

    /**
     * @brief 获取默认权限列表
     */
    TPSNDAPI Permissions getDefaultPermissions() const;

    /**
     * @brief 跟踪权限
     * @return <默认权限, 玩家权限>
     */
    TPSNDAPI Result<std::pair<Permissions, Permissions>> tracePermissions(mce::UUID const& uuid) const;

    // ── 管理/控制台路径 (按 RealName 操作) ──────────────────────────────
    // uuid 解析失败时回退到 legacy 组, 等玩家下次进服由懒迁移收编

    TPSAPI Result<void> grantPermissionByName(RealName const& realName, Permission permission);

    TPSAPI Result<void> revokePermissionByName(RealName const& realName, Permission permission);

    TPSNDAPI Result<std::pair<Permissions, Permissions>> tracePermissionsByName(RealName const& realName) const;

    TPSNDAPI static std::string               toString(Permission permission);    // 权限转字符串(枚举键)
    TPSNDAPI static std::optional<Permission> fromString(std::string const& str); // 字符串转权限
    TPSNDAPI static std::vector<Permission>   getPermissions();                   // 获取所有权限
    TPSNDAPI static Result<std::vector<PermissionStorage::Permission>> resolve(std::string const& permissions);

    static inline constexpr std::string_view LEGACY_BIG_KEY   = "permission";
    static inline constexpr std::string_view LEGACY_FILE_NAME = "permission.json";
};


} // namespace ltps