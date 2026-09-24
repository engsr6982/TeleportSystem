#pragma once
#include <memory>

#include "ll/api/mod/NativeMod.h"
#include "ll/api/thread/ThreadPoolExecutor.h"

#include "ltps/database/StorageManager.h"
#include "ltps/modules/ModuleManager.h"

#include <ll/api/thread/ServerThreadExecutor.h>

namespace ll_bstats {
class Telemetry;
}
namespace econbridge {
class IEconomy;
}

namespace ltps {

class TeleportSystem {
public:
    static TeleportSystem& getInstance();

    bool load();

    bool enable();

    bool disable();

    bool unload();

    void postReload();

public:
    [[nodiscard]] ll::mod::NativeMod& getSelf() const;

    [[nodiscard]] ll::thread::ThreadPoolExecutor& getThreadPool();

    [[nodiscard]] ll::thread::ServerThreadExecutor const& getServerThreadExecutor() const;

    [[nodiscard]] StorageManager& getStorageManager();

    [[nodiscard]] ModuleManager& getModuleManager();

    [[nodiscard]] econbridge::IEconomy&       getEconomy();
    [[nodiscard]] econbridge::IEconomy const& getEconomy() const;

private:
    explicit TeleportSystem();

    void postInitTelemetry();
    void postInitEconomy();

    ll::mod::NativeMod&                               mSelf;
    std::unique_ptr<ll::thread::ThreadPoolExecutor>   mThreadPool{nullptr};
    std::unique_ptr<ll::thread::ServerThreadExecutor> mServerThreadExecutor{nullptr};
    std::unique_ptr<StorageManager>                   mStorageManager{nullptr};
    std::unique_ptr<ModuleManager>                    mModuleManager{nullptr};
    std::unique_ptr<ll_bstats::Telemetry>             mTelemetry{nullptr};
    std::unique_ptr<econbridge::IEconomy>             mEconomy{nullptr};
};

} // namespace ltps
