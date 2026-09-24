#pragma once
#include "ll/api/data/KeyValueDB.h"
#include "ltps/Global.h"
#include "ltps/database/IStorage.h"
#include "mc/platform/UUID.h"
#include <concepts>
#include <memory>
#include <string>
#include <type_traits>
#include <typeindex>


namespace ltps {

// 数据布局: <库分组>:<业务组>:<用户唯一标识符>:<数据唯一标识符> => <数据>
// 库分组 meta/data/legacy/index; 用户段为 uuid.asString(), legacy 组为 realName
class StorageManager final {
private:
    class Impl;
    std::unique_ptr<Impl> mImpl;

    explicit StorageManager();

    friend class TeleportSystem;

    void      registerStorageImpl(std::type_index type, std::unique_ptr<IStorage> storage);
    IStorage* getStorageImpl(std::type_index type) const;

    [[nodiscard]] ll::data::KeyValueDB& getDatabase() const;

public:
    TPS_DISALLOW_COPY_AND_MOVE(StorageManager);

    TPSAPI ~StorageManager();

    // 版本协议: 校验 meta:schema, 执行 v1 -> v2 迁移/改名/写标记
    // 必须在所有 registerStorage 之后、模块初始化之前调用
    TPSAPI void initialize();

    // 通知所有 Storage 全量重建索引
    TPSAPI void rebuildIndexes();

    // 确保玩家数据已 uuid 化; 幂等, profile 记录兼作负缓存
    TPSAPI void ensureUserMigrated(mce::UUID const& uuid, RealName const& name, std::string_view xuid = {});

    // RealName -> uuid: index:ref:name: -> ll::service::PlayerInfo
    TPSNDAPI Result<mce::UUID> resolveUuid(RealName const& name) const;

    // 把 legacy 组中 <legacyName> 名下的记录收编到 uuid (管理命令 /ltps admin attach)
    TPSAPI Result<void>
           attachLegacyData(mce::UUID const& uuid, RealName const& legacyName, RealName const& currentName);

    // 注册一个 Storage 实例
    template <typename T, typename... Args>
        requires std::derived_from<T, IStorage> && std::is_final_v<T>
    void registerStorage(Args&&... args) {
        registerStorageImpl(typeid(T), std::make_unique<T>(getDatabase(), std::forward<Args>(args)...));
    }

    // 获取一个 Storage 实例
    template <typename T>
        requires std::derived_from<T, IStorage> && std::is_final_v<T>
    [[nodiscard]] T* getStorage() {
        return static_cast<T*>(getStorageImpl(typeid(T)));
    }
};


} // namespace ltps