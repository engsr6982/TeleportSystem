#pragma once
#include "ll/api/data/KeyValueDB.h"
#include "ll/api/thread/ThreadPoolExecutor.h"
#include "ltps/Global.h"
#include "ltps/database/IStorage.h"
#include <ll/api/coro/ConditionVariable.h>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>


namespace ltps {

class StorageManager final {
private:
    std::unique_ptr<ll::data::KeyValueDB>                          mDatabase;
    std::unordered_map<std::type_index, std::unique_ptr<IStorage>> mStorages;
    std::shared_ptr<ll::coro::ConditionVariable>                   mInterruptableSleep{nullptr};
    std::shared_ptr<std::atomic_bool>                              mWriteBackTaskAbortFlag{nullptr};


    explicit StorageManager(ll::thread::ThreadPoolExecutor& threadPoolExecutor);

    friend IStorage;
    friend class TeleportSystem;

public:
    TPS_DISALLOW_COPY_AND_MOVE(StorageManager);

    TPSAPI ~StorageManager();

    TPSAPI void postLoad();      // 通知所有Storage实例加载
    TPSAPI void postUnload();    // 通知所有Storage实例卸载
    TPSAPI void postWriteBack(); // 通知所有Storage实例回写

    // 注册一个Storage实例
    template <typename T, typename... Args>
        requires std::derived_from<T, IStorage> && std::is_final_v<T>
    void registerStorage(Args&&... args) {
        auto storage         = std::make_unique<T>(std::forward<Args>(args)...);
        mStorages[typeid(T)] = std::move(storage);
    }

    // 获取一个Storage实例
    template <typename T>
        requires std::derived_from<T, IStorage> && std::is_final_v<T>
    [[nodiscard]] T* getStorage() {
        auto it = mStorages.find(typeid(T));
        if (it == mStorages.end()) {
            return nullptr;
        }
        return static_cast<T*>(it->second.get());
    }
};

} // namespace ltps