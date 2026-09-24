#include "DeathModule.h"

#include "DeathCommand.h"
#include "event/DeathEvents.h"
#include "gui/DeathGUI.h"
#include "ltps/TeleportSystem.h"
#include "ltps/base/Config.h"
#include "ltps/common/PriceCalculate.h"
#include "ltps/database/StorageManager.h"
#include "ltps/utils/McUtils.h"

#include "../setting/SettingStorage.h"
#include "ll/api/event/player/PlayerDieEvent.h"
#include "ll/api/event/player/PlayerRespawnEvent.h"

#include <ll/api/event/EventBus.h>
#include <mc/world/actor/player/Player.h>

namespace ltps ::death {


DeathModule::DeathModule() = default;

std::vector<std::string> DeathModule::getDependencies() const { return {}; }

bool DeathModule::isLoadable() const { return getConfig().modules.death.enable; }

bool DeathModule::init() { return true; }

bool DeathModule::enable() {
    auto& bus = ll::event::EventBus::getInstance();

    mListeners.emplace_back(bus.emplaceListener<PlayerRequestBackDeathPointEvent>(
        [this](PlayerRequestBackDeathPointEvent& ev) {
            auto&      player     = ev.getPlayer();
            auto const index      = ev.getIndex();
            auto       localeCode = player.getLocaleCode();

            auto info = getStorage()->getSpecificDeathInfo(player.getUuid(), index);
            if (!info.has_value()) {
                mc_utils::sendText<mc_utils::Error>(player, "No matching death record found"_trl(localeCode));
                ev.cancel();
                return;
            }

            auto& bus     = ll::event::EventBus::getInstance();
            auto  backing = BackingDeathPointEvent{player, *info, index};
            bus.publish(backing);

            if (backing.isCancelled()) {
                ev.cancel();
                return;
            }

            info->teleport(player);
            mc_utils::sendText(player, "Teleported"_trl(localeCode));

            auto backed = BackedDeathPointEvent{player, *info, index};
            bus.publish(backed);
        },
        ll::event::EventPriority::High
    ));
    mListeners.emplace_back(bus.emplaceListener<BackingDeathPointEvent>(
        [this](BackingDeathPointEvent& ev) {
            auto& player     = ev.getPlayer();
            auto  realName   = player.getRealName();
            auto  localeCode = player.getLocaleCode();

            auto&      info  = ev.getDeathInfo();
            auto const index = ev.getIndex();

            auto cl = PriceCalculate(getConfig().modules.death.goDeathCalculate);
            cl.addVariable("dimid", info.dimid);
            cl.addVariable("index", index);
            auto price = cl.eval();

            if (!price) {
                mc_utils::sendText<mc_utils::Error>(player, "Failed to calculate price"_trl(localeCode));
                TeleportSystem::getInstance().getSelf().getLogger().error(
                    "[DeathModule]: Calculate price failed! player: {}, deathInfo: {}, error: {}",
                    realName,
                    info.toString(),
                    price.error().message()
                );
                ev.cancel();
                return;
            }

            if (const auto& economy = EconomySystemManager::getInstance();
                !economy->reduce(player, static_cast<llong>(price.value()))) {
                economy->sendNotEnoughMoneyMessage(player, static_cast<llong>(price.value()), localeCode);
                ev.cancel();
                return;
            }
        },
        ll::event::EventPriority::High
    ));


    mListeners.emplace_back(bus.emplaceListener<ll::event::PlayerDieEvent>([this](ll::event::PlayerDieEvent& ev) {
        auto& player = ev.self();
        if (player.isSimulatedPlayer()) {
            return;
        }

        auto& pos   = player.getPosition();
        auto  dimid = player.getDimensionId();

        if (getConfig().modules.death.disallowedDimensions.contains(dimid)) {
            return;
        }

        auto info = DeathStorage::DeathInfo::make(pos, dimid);
        getStorage()->addDeathInfo(player.getUuid(), std::move(info));

        mc_utils::sendText(
            player,
            "Death location recorded, use /death back to return"_trl(player.getLocaleCode())
        );
    }));

    mListeners.emplace_back(bus.emplaceListener<ll::event::PlayerRespawnEvent>([this](ll::event::PlayerRespawnEvent& ev
                                                                               ) {
        auto& player = ev.self();
        if (player.isSimulatedPlayer()) {
            return;
        }

        if (auto ps = getStorageManager().getStorage<setting::SettingStorage>();
            ps && ps->getSettingData(player.getUuid()).deathPopup) {
            DeathGUI::sendBackGUI(player);
        }
    }));


    DeathCommand::setup();

    return true;
}

bool DeathModule::disable() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& p : mListeners) {
        bus.removeListener(p);
    }
    mListeners.clear();

    return true;
}

DeathStorage* DeathModule::getStorage() const { return getStorageManager().getStorage<DeathStorage>(); }


} // namespace ltps::death