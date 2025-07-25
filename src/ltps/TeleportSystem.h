#pragma once
#include <memory>

#include "ll/api/mod/NativeMod.h"
#include "ll/api/thread/ThreadPoolExecutor.h"

#include "ltps/database/StorageManager.h"
#include "ltps/modules/ModuleManager.h"

#include <ll/api/thread/ServerThreadExecutor.h>

namespace ltps {

class TeleportSystem {
public:
    static TeleportSystem& getInstance();

    bool load();

    bool enable();

    bool disable();

    bool unload();

public:
    [[nodiscard]] ll::mod::NativeMod& getSelf() const;

    [[nodiscard]] ll::thread::ThreadPoolExecutor& getThreadPool();

    [[nodiscard]] ll::thread::ServerThreadExecutor const& getServerThreadExecutor() const;

    [[nodiscard]] StorageManager& getStorageManager();

    [[nodiscard]] ModuleManager& getModuleManager();

private:
    explicit TeleportSystem();

    ll::mod::NativeMod&                               mSelf;
    std::unique_ptr<ll::thread::ThreadPoolExecutor>   mThreadPool;
    std::unique_ptr<ll::thread::ServerThreadExecutor> mServerThreadExecutor;
    std::unique_ptr<StorageManager>                   mStorageManager;
    std::unique_ptr<ModuleManager>                    mModuleManager;
};

} // namespace ltps
