#include "ltps/database/StorageManager.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/data/KeyValueDB.h"
#include "ltps/TeleportSystem.h"
#include <memory>
#include <stdexcept>


namespace ltps {

StorageManager::StorageManager(ll::thread::ThreadPoolExecutor& threadPoolExecutor) {
    if (!mDatabase) {
        auto path = TeleportSystem::getInstance().getSelf().getModDir() / "leveldb";
        mDatabase = std::make_unique<ll::data::KeyValueDB>(path);
    }
    mInterruptableSleep     = std::make_shared<ll::coro::ConditionVariable>();
    mWriteBackTaskAbortFlag = std::make_shared<std::atomic_bool>(false);

    ll::coro::keepThis(
        [this,
         interruptableSleep     = mInterruptableSleep,
         writeBackTaskAbortFlag = mWriteBackTaskAbortFlag]() -> ll::coro::CoroTask<> {
            while (!writeBackTaskAbortFlag->load()) {
                co_await interruptableSleep->waitFor(std::chrono::seconds(60));
                if (writeBackTaskAbortFlag->load()) {
                    break;
                }
                postWriteBack();
            }
            co_return;
        }
    ).launch(threadPoolExecutor);
}

StorageManager::~StorageManager() {
    mWriteBackTaskAbortFlag->store(true);
    mInterruptableSleep->notifyAll(true);
}

void StorageManager::postLoad() {
    for (auto& [_, storage] : mStorages) {
        try {
            storage->load();
        } catch (const std::exception& e) {
            TeleportSystem::getInstance().getSelf().getLogger().error(
                "StorageManager: Failed to load storage: {}",
                e.what()
            );
        } catch (...) {
            TeleportSystem::getInstance().getSelf().getLogger().error(
                "StorageManager: Failed to load storage: unknown error"
            );
        }
    }
}
void StorageManager::postUnload() {
    for (auto& [_, storage] : mStorages) {
        try {
            storage->unload();
        } catch (const std::exception& e) {
            TeleportSystem::getInstance().getSelf().getLogger().error(
                "StorageManager: Failed to unload storage: {}",
                e.what()
            );
        } catch (...) {
            TeleportSystem::getInstance().getSelf().getLogger().error(
                "StorageManager: Failed to unload storage: unknown error"
            );
        }
    }
}
void StorageManager::postWriteBack() {
    for (auto& [_, storage] : mStorages) {
        try {
            storage->writeBack();
        } catch (const std::exception& e) {
            TeleportSystem::getInstance().getSelf().getLogger().error(
                "StorageManager: Failed to write back storage: {}",
                e.what()
            );
        } catch (...) {
            TeleportSystem::getInstance().getSelf().getLogger().error(
                "StorageManager: Failed to write back storage: unknown error"
            );
        }
    }
}


} // namespace ltps