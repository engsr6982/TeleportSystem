#include "TprModule.h"

#include "TprCommand.h"
#include "events/TprEvents.h"
#include "ltps/TeleportSystem.h"
#include "ltps/base/Config.h"
#include "ltps/common/PriceCalculate.h"
#include "ltps/helper/EconomyHelper.h"
#include "ltps/utils/McUtils.h"


#include <ll/api/event/EventBus.h>

namespace ltps::tpr {


TprModule::TprModule() = default;

std::vector<std::string> TprModule::getDependencies() const { return {}; }

bool TprModule::isLoadable() const { return getConfig().modules.tpr.enable; }

bool TprModule::init() {
    if (!mSafeTeleport) {
        mSafeTeleport = std::make_unique<SafeTeleport>(getServerThreadExecutor());
    }
    return true;
}

bool TprModule::enable() {
    auto& bus = ll::event::EventBus::getInstance();

    mListeners.emplace_back(bus.emplaceListener<PlayerRequestTprEvent>([this](PlayerRequestTprEvent& ev) {
        auto& player = ev.getPlayer();

        auto pos = getRandomPosWithConfig(player);
        auto dim = player.getDimensionId();

        auto& bus = ll::event::EventBus::getInstance();

        auto pre = PrepareCreateTprTaskEvent{player, pos, dim};
        bus.publish(pre);

        if (pre.isCancelled()) {
            ev.cancel();
            return;
        }

        mSafeTeleport->launchTask(player, {pos, dim});

        bus.publish(TprTaskCreatedEvent{player, pos, dim});
    }));

    mListeners.emplace_back(bus.emplaceListener<PrepareCreateTprTaskEvent>([this](PrepareCreateTprTaskEvent& ev) {
        auto& player = ev.getPlayer();

        if (getConfig().modules.tpr.disallowedDimensions.contains(player.getDimensionId())) {
            mc_utils::sendText<mc_utils::Error>(player, "此功能在当前维度不可用"_trl(player.getLocaleCode()));
            ev.cancel();
            return;
        }

        auto& cool = getCooldown();
        if (cool.isCooldown(player.getRealName())) {
            mc_utils::sendText<mc_utils::Error>(
                player,
                "TPR 冷却中，请稍后再试, 冷却时间: {0}"_trl(
                    player.getLocaleCode(),
                    cool.getCooldownString(player.getRealName())
                )
            );
            ev.cancel();
            return;
        }

        auto cl    = PriceCalculate(getConfig().modules.tpr.calculate);
        auto price = cl.eval();

        if (!price.has_value()) {
            TeleportSystem::getInstance().getSelf().getLogger().error(
                "An exception occurred while calculating the TPA price, please check the configuration file.\n{}",
                price.error()
            );
            mc_utils::sendText<mc_utils::Error>(player, "Tpr 模块异常，请联系管理员"_trl(player.getLocaleCode()));
            ev.cancel();
            return;
        }

        auto& eco = TeleportSystem::getInstance().getEconomy();
        if (!eco.reduce(player.getUuid(), price.value())) {
            economy_helper::sendNotEnoughMoneyMessage(player, price.value(), player.getLocaleCode());
            ev.cancel();
            return;
        }

        cool.setCooldown(player.getRealName(), getConfig().modules.tpr.cooldownTime);
    }));

    TprCommand::setup();

    return true;
}

bool TprModule::disable() {
    mSafeTeleport.reset();

    auto& bus = ll::event::EventBus::getInstance();
    for (auto& listener : mListeners) {
        bus.removeListener(listener);
    }
    mListeners.clear();

    return true;
}

Cooldown& TprModule::getCooldown() { return mCooldown; }

Vec3 TprModule::getRandomPosWithConfig(Player& player) {
    auto const& cfg = getConfig().modules.tpr;

    if (!cfg.restrictedAreas.enable) {
        auto& [min, max] = cfg.randomRange;
        return {randomInt(min, max), 320, randomInt(min, max)};
    }

    auto const& area = cfg.restrictedAreas;

    auto& pos  = player.getPosition();
    auto  cenx = area.center.usePlayerPositionCenter ? static_cast<int>(pos.x) : area.center.x;
    auto  cenz = area.center.usePlayerPositionCenter ? static_cast<int>(pos.z) : area.center.z;

    return randomCenterVec3(cenx, cenz, area.center.radius, area.isCircle);
}

Vec3 TprModule::randomCenterVec3(int centerX, int centerZ, int radius, bool isCircle) {
    int minX = centerX - radius;
    int maxX = centerX + radius;
    int minZ = centerZ - radius;
    int maxZ = centerZ + radius;
    if (!isCircle) {
        if (minX > maxX) std::swap(minX, maxX);
        if (minZ > maxZ) std::swap(minZ, maxZ);
    }
    return {randomInt(minX, maxX), 320, randomInt(minZ, maxZ)};
}

int TprModule::randomInt(int min, int max) {
    std::random_device rd;

    auto seed_data = rd()
                   ^ (std::hash<long long>()(std::chrono::high_resolution_clock::now().time_since_epoch().count())
                      + std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::mt19937_64                    mt(seed_data);
    std::uniform_int_distribution<int> dist(min, max);
    return dist(mt);
}


} // namespace ltps::tpr