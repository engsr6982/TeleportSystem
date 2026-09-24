#include "WarpModule.h"

#include "WarpCommand.h"
#include "event/WarpEvents.h"
#include "ltps/TeleportSystem.h"
#include "ltps/base/Config.h"
#include "ltps/common/PriceCalculate.h"
#include "ltps/database/PermissionStorage.h"
#include "ltps/database/StorageManager.h"
#include "ltps/utils/McUtils.h"

#include <ll/api/event/EventBus.h>

namespace ltps::warp {


WarpModule::WarpModule() = default;

std::vector<std::string> WarpModule::getDependencies() const { return {}; }

bool WarpModule::isLoadable() const { return getConfig().modules.warp.enable; }

bool WarpModule::init() { return true; }

bool WarpModule::enable() {
    auto& bus = ll::event::EventBus::getInstance();

    mListeners.emplace_back(bus.emplaceListener<PlayerRequestGoWarpEvent>(
        [this](PlayerRequestGoWarpEvent& ev) {
            auto& bus = ll::event::EventBus::getInstance();

            auto& player     = ev.getPlayer();
            auto  localeCode = player.getLocaleCode();
            auto& name       = ev.getName();

            auto storage = getStorage();
            if (!storage) {
                throw std::runtime_error("WarpStorage not found");
            }

            auto warp = storage->getWarp(name);
            if (!warp) {
                mc_utils::sendText<mc_utils::Error>(player, "Public warp {} does not exist"_trl(localeCode, name));
                ev.cancel();
                return;
            }

            auto teleporting = WarpTeleportingEvent(player, warp.value());
            bus.publish(teleporting);

            if (teleporting.isCancelled()) {
                ev.cancel();
                return;
            }

            warp->teleport(player);

            auto teleported = WarpTeleportedEvent(player, warp.value());

            bus.publish(teleported);
            ev.invokeCallback(warp.value());
        },
        ll::event::EventPriority::High
    ));
    mListeners.emplace_back(bus.emplaceListener<WarpTeleportingEvent>(
        [this](WarpTeleportingEvent& ev) {
            auto& player     = ev.getPlayer();
            auto  realName   = player.getRealName();
            auto  localeCode = player.getLocaleCode();

            auto& cooldown = getCooldown();
            auto  uuidKey  = player.getUuid().asString();

            if (cooldown.isCooldown(uuidKey)) {
                mc_utils::sendText(
                    player,
                    "Teleport on cooldown, please retry later. Remaining: {}"_trl(
                        localeCode,
                        cooldown.getCooldownString(uuidKey)
                    )
                );
                ev.cancel();
                return;
            }

            auto cl = PriceCalculate(getConfig().modules.warp.goWarpCalculate);
            cl.addVariable("dimid", ev.getWarp().dimid);
            auto price = cl.eval();

            if (!price) {
                mc_utils::sendText<mc_utils::Error>(player, "Failed to calculate price"_trl(localeCode));
                TeleportSystem::getInstance().getSelf().getLogger().error(
                    "[WarpModule]: Calculate price failed! player: {}, warpName: {}, error: {}",
                    realName,
                    ev.getWarp().name,
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

            cooldown.setCooldown(uuidKey, getConfig().modules.warp.cooldownTime);
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<PlayerRequestAddWarpEvent>(
        [this](PlayerRequestAddWarpEvent& ev) {
            auto&           player     = ev.getPlayer();
            auto            localeCode = player.getLocaleCode();
            RealName const& realName   = player.getRealName();

            auto warp = WarpStorage::Warp::make(player.getPosition(), player.getDimensionId(), ev.getName());

            auto& bus = ll::event::EventBus::getInstance();

            auto adding = WarpAddingEvent(player, warp);
            bus.publish(adding);
            if (adding.isCancelled()) {
                return;
            }

            auto storage = getStorage();
            if (!storage) {
                throw std::runtime_error("WarpStorage not found");
            }

            auto res = storage->addWarp(warp);
            if (!res) {
                mc_utils::sendText<mc_utils::Error>(player, "Failed to add public warp"_trl(localeCode));
                TeleportSystem::getInstance().getSelf().getLogger().error(
                    "[WarpModule]: Add warp failed! player: {}, warpName: {}, error: {}",
                    realName,
                    warp.name,
                    res.error().message()
                );
                ev.cancel();
                return;
            }

            mc_utils::sendText(player, "Public warp added"_trl(localeCode));

            auto added = WarpAddedEvent(player, warp);
            bus.publish(added);

            ev.invokeCallback(warp);
        },
        ll::event::EventPriority::High
    ));
    mListeners.emplace_back(bus.emplaceListener<WarpAddingEvent>(
        [this](WarpAddingEvent& ev) {
            auto storage = getStorage();
            if (!storage) {
                throw std::runtime_error("WarpStorage not found");
            }

            auto&           player     = ev.getPlayer();
            auto            localeCode = player.getLocaleCode();

            auto const& dimid = ev.getWarp().dimid;
            if (getConfig().modules.warp.disallowedDimensions.contains(dimid)) {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Public warps cannot be created in this dimension"_trl(localeCode)
                );
                ev.cancel();
                return;
            }

            auto pe = getStorageManager().getStorage<PermissionStorage>();
            if (pe && !pe->hasPermission(player.getUuid(), PermissionStorage::Permission::AddWarp)) {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "You do not have permission to create public warps"_trl(localeCode)
                );
                ev.cancel();
                return;
            }

            auto const& warpName = ev.getWarp().name;
            if (storage->hasWarp(warpName)) {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Warp name already exists, please use another name"_trl(localeCode)
                );
                ev.cancel();
                return;
            }
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<PlayerRequestRemoveWarpEvent>(
        [this](PlayerRequestRemoveWarpEvent& ev) {
            auto& bus = ll::event::EventBus::getInstance();

            auto& player = ev.getPlayer();
            auto& name   = ev.getName();

            auto removeing = WarpRemovingEvent(player, name);
            bus.publish(removeing);
            if (removeing.isCancelled()) {
                ev.invokeCallback(false);
                ev.cancel();
                return;
            }

            auto storage = getStorage();
            if (!storage) {
                throw std::runtime_error("WarpStorage not found");
            }

            auto res = storage->removeWarp(name);
            if (!res) {
                mc_utils::sendText<mc_utils::Error>(player, "Failed to remove public warp"_trl(player.getLocaleCode()));
                TeleportSystem::getInstance().getSelf().getLogger().error(
                    "[WarpModule]: Remove warp failed! player: {}, warpName: {}, error: {}",
                    player.getRealName(),
                    name,
                    res.error().message()
                );
                ev.invokeCallback(false);
                ev.cancel();
                return;
            }
            mc_utils::sendText(player, "Public warp {} removed!"_trl(player.getLocaleCode(), name));

            auto removed = WarpRemovedEvent(player, name);
            bus.publish(removed);

            ev.invokeCallback(true);
        },
        ll::event::EventPriority::High
    ));
    mListeners.emplace_back(bus.emplaceListener<WarpRemovingEvent>(
        [this](WarpRemovingEvent& ev) {
            auto&           player     = ev.getPlayer();
            auto            localeCode = player.getLocaleCode();

            auto pe = getStorageManager().getStorage<PermissionStorage>();
            if (pe && !pe->hasPermission(player.getUuid(), PermissionStorage::Permission::RemoveWarp)) {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "You do not have permission to remove public warps"_trl(localeCode)
                );
                ev.cancel();
                return;
            }
        },
        ll::event::EventPriority::High
    ));


    // Admin
    mListeners.emplace_back(bus.emplaceListener<AdminRequestGoWarpEvent>(
        [this](AdminRequestGoWarpEvent& ev) {
            auto& bus    = ll::event::EventBus::getInstance();
            auto& player = ev.getAdmin();
            auto& warp   = ev.getWarp();

            auto ing = AdminTeleportingWarpEvent{player, warp};
            bus.publish(ing);

            if (ing.isCancelled()) {
                return;
            }

            warp.teleport(player);

            auto ed = AdminTeleportedWarpEvent{player, warp};
            bus.publish(ed);
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<AdminRequestCreateWarpEvent>(
        [this](AdminRequestCreateWarpEvent& ev) {
            auto& bus        = ll::event::EventBus::getInstance();
            auto& player     = ev.getAdmin();
            auto  localeCode = player.getLocaleCode();
            auto& name       = ev.getName();
            auto& pos        = ev.getPosition();
            auto  dimid      = ev.getDimid();

            auto ing = AdminCreateingWarpEvent{player, name, dimid, pos};
            bus.publish(ing);

            if (ing.isCancelled()) {
                return;
            }

            auto warp = WarpStorage::Warp::make(pos, dimid, name);

            auto storage = this->getStorage();
            if (auto res = storage->addWarp(warp)) {
                mc_utils::sendText(player, "Public warp {} created"_trl(localeCode, name));
            } else {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Failed to create public warp {}: {}"_trl(localeCode, name, res.error().message())
                );
            }
        },
        ll::event::EventPriority::High
    ));
    mListeners.emplace_back(bus.emplaceListener<AdminCreateingWarpEvent>(
        [this](AdminCreateingWarpEvent& ev) {
            auto& player     = ev.getAdmin();
            auto  localeCode = player.getLocaleCode();
            auto& name       = ev.getName();
            auto  dimid      = ev.getDimid();

            if (getConfig().modules.warp.disallowedDimensions.contains(dimid)) {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Public warps cannot be created in this dimension"_trl(localeCode)
                );
                ev.cancel();
                return;
            }

            if (getStorage()->hasWarp(name)) {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Warp name already exists, please use another name"_trl(localeCode)
                );
                ev.cancel();
                return;
            }
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<AdminRequestEditWarpEvent>(
        [this](AdminRequestEditWarpEvent& ev) {
            auto& bus     = ll::event::EventBus::getInstance();
            auto& player  = ev.getAdmin();
            auto& warp    = ev.getWarp();
            auto& newWarp = ev.getNewWarp();

            auto ing = AdminEditingWarpEvent{player, warp, newWarp};
            bus.publish(ing);

            if (ing.isCancelled()) {
                return;
            }

            auto storage = this->getStorage();
            if (auto res = storage->updateWarp(warp.name, newWarp)) {
                mc_utils::sendText(player, "Public warp {} updated"_trl(player.getLocaleCode(), warp.name));
            } else {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Failed to update public warp {}: {}"_trl(player.getLocaleCode(), warp.name, res.error().message())
                );
            }

            auto ed = AdminEditedWarpEvent{player, warp, newWarp};
            bus.publish(ed);
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<AdminRequestRemoveWarpEvent>(
        [this](AdminRequestRemoveWarpEvent& ev) {
            auto& bus    = ll::event::EventBus::getInstance();
            auto& player = ev.getAdmin();

            auto& warp = ev.getWarp();

            auto ing = AdminRemovingWarpEvent{player, warp};
            bus.publish(ing);

            if (ing.isCancelled()) {
                return;
            }

            auto storage = this->getStorage();
            if (auto res = storage->removeWarp(warp.name)) {
                mc_utils::sendText(player, "Public warp {} removed"_trl(player.getLocaleCode(), warp.name));
            } else {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Failed to remove public warp {}: {}"_trl(player.getLocaleCode(), warp.name, res.error().message())
                );
                return;
            }

            auto rm = AdminRemovedWarpEvent{player, warp};
            bus.publish(rm);
        },
        ll::event::EventPriority::High
    ));


    WarpCommand::setup();
    return true;
}

bool WarpModule::disable() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& p : mListeners) {
        bus.removeListener(p);
    }
    mListeners.clear();

    return true;
}

WarpStorage* WarpModule::getStorage() const { return getStorageManager().getStorage<WarpStorage>(); }

Cooldown& WarpModule::getCooldown() { return mCooldown; }


} // namespace ltps::warp