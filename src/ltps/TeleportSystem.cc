#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "ltps/TeleportSystem.h"
#include "ltps/Version.h"
#include "ltps/base/BaseCommand.h"
#include "ltps/base/Config.h"
#include "ll/api/i18n/I18n.h"
#include "ltps/common/EconomySystem.h"
#include "ltps/database/PermissionStorage.h"
#include "ltps/database/StorageKeys.h"
#include "ltps/database/StorageManager.h"
#include "ltps/modules/ModuleManager.h"
#include "ltps/modules/home/HomeModule.h"
#include "ltps/modules/tpa/TpaModule.h"
#include "ltps/modules/warp/WarpModule.h"
#include "ltps/modules/warp/WarpStorage.h"
#include "modules/death/DeathModule.h"
#include "modules/death/DeathStorage.h"
#include "modules/setting/SettingModule.h"
#include "modules/setting/SettingStorage.h"
#include "modules/tpr/TprModule.h"
#include <memory>


namespace ltps {


#ifdef TPS_TEST
namespace test {
extern void Test_Main();
}
#endif


TeleportSystem& TeleportSystem::getInstance() {
    static TeleportSystem instance;
    return instance;
}


bool TeleportSystem::load() {
    auto& logger = mSelf.getLogger();
    logger.info("Version: {}", LTPS_VERSION_STRING);

    // 加载翻译文件 (缺失/损坏时退化为英文源文本, 不阻断启动)
    if (auto langDir = mSelf.getModDir() / keys::kLangDirName; std::filesystem::exists(langDir)) {
        if (auto res = ll::i18n::getInstance().load(langDir); !res) {
            logger.error("Failed to load translations: {}", res.error().message());
        }
    }
#ifdef TPS_DEBUG
    logger.setLevel(ll::io::LogLevel::Trace);
    logger.warn("TeleportSystem is running in debug mode!");
#endif
#ifdef TPS_TEST
    logger.warn("TeleportSystem is running in test mode!");
#endif

    mThreadPool           = std::make_unique<ll::thread::ThreadPoolExecutor>("TeleportSystem-ThreadPool", 2);
    mServerThreadExecutor = std::make_unique<ll::thread::ServerThreadExecutor>(
        "TeleportSystem-ServerThreadExecutor",
        std::chrono::milliseconds{30},
        16
    );
    mModuleManager = std::unique_ptr<ModuleManager>(new ModuleManager());

    // 初始化全局配置
    loadConfig();

    EconomySystemManager::getInstance().initEconomySystem();

    try {
        mStorageManager = std::unique_ptr<StorageManager>(new StorageManager());

        // 注册 Storage
        mStorageManager->registerStorage<PermissionStorage>();
        mStorageManager->registerStorage<setting::SettingStorage>();
        mStorageManager->registerStorage<home::HomeStorage>();
        mStorageManager->registerStorage<warp::WarpStorage>();
        mStorageManager->registerStorage<death::DeathStorage>();

        mStorageManager->initialize(); // 数据库版本协议 + 迁移 + 懒迁移监听
    } catch (std::exception const& e) {
        // 数据库状态非法或迁移失败: 明确中止加载, 不以残缺状态运行
        logger.error("Database initialization aborted: {}", e.what());
        mStorageManager.reset();
        mModuleManager.reset();
        mServerThreadExecutor.reset();
        if (mThreadPool) {
            mThreadPool->destroy();
            mThreadPool.reset();
        }
        return false;
    }

    // 注册模块
    mModuleManager->registerModule<setting::SettingModule>();
    mModuleManager->registerModule<tpa::TpaModule>();
    mModuleManager->registerModule<home::HomeModule>();
    mModuleManager->registerModule<warp::WarpModule>();
    mModuleManager->registerModule<death::DeathModule>();
    mModuleManager->registerModule<tpr::TprModule>();

    mModuleManager->initModules(); // 初始化模块
    return true;
}

bool TeleportSystem::enable() {
    mModuleManager->enableModules(); // 启用模块
    BaseCommand::setup();            // 基础命令

#ifdef TPS_TEST
    test::Test_Main();
#endif

    return true;
}

bool TeleportSystem::disable() {
    // 加载中途 abort 时成员可能为空, 逐项判空
    if (mModuleManager) {
        mModuleManager->disableModules(); // 禁用模块
    }

    mModuleManager.reset();        // 销毁模块管理器指针
    mStorageManager.reset();       // 销毁 Storage 指针
    mServerThreadExecutor.reset(); // 销毁 Server 线程池指针
    if (mThreadPool) {
        mThreadPool->destroy(); // 销毁线程池
        mThreadPool.reset();    // 销毁线程池指针
    }
    return true;
}

bool TeleportSystem::unload() {
    // TODO: 卸载插件
    return true;
}

TeleportSystem::TeleportSystem() : mSelf(*ll::mod::NativeMod::current()) {}
ll::mod::NativeMod&                     TeleportSystem::getSelf() const { return mSelf; }
ll::thread::ThreadPoolExecutor&         TeleportSystem::getThreadPool() { return *mThreadPool; }
ll::thread::ServerThreadExecutor const& TeleportSystem::getServerThreadExecutor() const {
    return *mServerThreadExecutor;
}
StorageManager& TeleportSystem::getStorageManager() { return *mStorageManager; }
ModuleManager&  TeleportSystem::getModuleManager() { return *mModuleManager; }

} // namespace ltps

LL_REGISTER_MOD(ltps::TeleportSystem, ltps::TeleportSystem::getInstance());
