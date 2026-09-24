#include "ltps/modules/home/HomeModule.h"
#include "HomeStorage.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ltps/Global.h"
#include "ltps/TeleportSystem.h"
#include "ltps/base/Config.h"
#include "ltps/common/EconomySystem.h"
#include "ltps/common/PriceCalculate.h"
#include "ltps/database/PermissionStorage.h"
#include "ltps/database/StorageManager.h"
#include "ltps/modules/home/HomeCommand.h"
#include "ltps/modules/home/event/HomeEvents.h"
#include "ltps/utils/McUtils.h"
#include "ltps/utils/StringUtils.h"
#include "ltps/validator/NameValidator.h"


namespace ltps::home {

namespace {

// 家园名称校验 (长度等由配置驱动)
NameValidator makeNameValidator() {
    return NameValidator{
        ValidatorOptions{.maxLength = static_cast<std::size_t>(getConfig().modules.home.nameLength)}
    };
}

void sendNameValidationError(Player& player, std::string_view localeCode, NameValidation result, std::string_view name) {
    switch (result) {
    case NameValidation::TooLong:
        mc_utils::sendText<mc_utils::Error>(
            player,
            "Home name is too long ({}/{})"_trl(localeCode, string_utils::length(std::string{name}),
                                                getConfig().modules.home.nameLength)
        );
        break;
    case NameValidation::Empty:
        mc_utils::sendText<mc_utils::Error>(player, "Home name cannot be empty"_trl(localeCode));
        break;
    case NameValidation::IllegalChar:
        mc_utils::sendText<mc_utils::Error>(player, "Home name contains illegal characters"_trl(localeCode));
        break;
    case NameValidation::Ok:
        break;
    }
}

} // namespace

HomeModule::HomeModule() = default;

std::vector<std::string> HomeModule::getDependencies() const { return {}; }

bool HomeModule::isLoadable() const { return getConfig().modules.home.enable; }

bool HomeModule::init() { return true; }

bool HomeModule::enable() {
    auto& bus = ll::event::EventBus::getInstance();

    mListeners.emplace_back(bus.emplaceListener<PlayerRequestAddHomeEvent>(
        [this](PlayerRequestAddHomeEvent& ev) {
            auto& player     = ev.getPlayer();
            auto  localeCode = player.getLocaleCode();

            auto home = HomeStorage::Home::make(player.getPosition(), player.getDimensionId(), ev.getName());

            auto& bus = ll::event::EventBus::getInstance();

            auto adding = HomeAddingEvent(player, home);
            bus.publish(adding);
            if (adding.isCancelled()) {
                return;
            }

            auto storage = getStorage();
            if (!storage) {
                throw std::runtime_error("HomeStorage not found");
            }

            auto res = storage->addHome(player.getUuid(), home);
            if (!res) {
                mc_utils::sendText<mc_utils::Error>(player, "Failed to add home"_trl(localeCode));
                TeleportSystem::getInstance().getSelf().getLogger().error(
                    "[HomeModule]: Add home failed! player: {}, homeName: {}, error: {}",
                    player.getRealName(),
                    home.name,
                    res.error().message()
                );
                ev.cancel();
                return;
            }

            mc_utils::sendText(player, "Home added"_trl(localeCode));

            auto added = HomeAddedEvent(player, home);
            bus.publish(added);

            ev.invokeCallback(home);
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<HomeAddingEvent>(
        [this](HomeAddingEvent& ev) {
            auto storage = getStorage();
            if (!storage) {
                throw std::runtime_error("HomeStorage not found");
            }

            auto& player     = ev.getPlayer();
            auto  localeCode = player.getLocaleCode();

            auto const& dimid = ev.getHome().dimid;
            if (getConfig().modules.home.disallowedDimensions.contains(dimid)) {
                mc_utils::sendText<mc_utils::Error>(player, "Homes cannot be created in this dimension"_trl(localeCode));
                ev.cancel();
                return;
            }

            auto const& homeName = ev.getHome().name;
            auto        validator = makeNameValidator();
            if (auto result = validator.validate(homeName); result != NameValidation::Ok) {
                sendNameValidationError(player, localeCode, result, homeName);
                ev.cancel();
                return;
            }

            if (storage->hasHome(player.getUuid(), homeName)) {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Home name already exists, please use another name"_trl(localeCode)
                );
                ev.cancel();
                return;
            }

            auto count = storage->getHomeCount(player.getUuid());

            bool unLimited = false;
            if (auto pe = getStorageManager().getStorage<PermissionStorage>()) {
                unLimited = pe->hasPermission(player.getUuid(), PermissionStorage::Permission::UnlimitedHome);
            }

            if (count > getConfig().modules.home.maxHome && !unLimited) {
                mc_utils::sendText<mc_utils::Error>(player, "Home count limit reached"_trl(localeCode));
                ev.cancel();
                return;
            }

            PriceCalculate cl(getConfig().modules.home.createHomeCalculate);
            cl.addVariable("count", count);

            auto price = cl.eval();
            if (!price.has_value()) {
                mc_utils::sendText<mc_utils::Error>(player, "Failed to calculate price"_trl(localeCode));
                TeleportSystem::getInstance().getSelf().getLogger().error(
                    "[HomeModule]: Calculate price failed! player: {}, homeName: {}, count: {}, error: {}",
                    player.getRealName(),
                    homeName,
                    count,
                    price.error().message()
                );
                ev.cancel();
                return;
            }

            auto& economy = EconomySystemManager::getInstance();
            if (!economy->reduce(player, static_cast<llong>(price.value()))) {
                economy->sendNotEnoughMoneyMessage(player, static_cast<llong>(price.value()), localeCode);
                ev.cancel();
                return;
            }
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<PlayerRequestRemoveHomeEvent>(
        [this](PlayerRequestRemoveHomeEvent& ev) {
            auto& bus = ll::event::EventBus::getInstance();

            auto& player = ev.getPlayer();
            auto& name   = ev.getName();

            auto removeing = HomeRemovingEvent(player, name);
            bus.publish(removeing);
            if (removeing.isCancelled()) {
                ev.invokeCallback(false);
                ev.cancel();
                return;
            }

            auto storage = getStorage();
            if (!storage) {
                throw std::runtime_error("HomeStorage not found");
            }

            auto res = storage->removeHome(player.getUuid(), name);
            if (!res) {
                mc_utils::sendText<mc_utils::Error>(player, "Failed to remove home"_trl(player.getLocaleCode()));
                TeleportSystem::getInstance().getSelf().getLogger().error(
                    "[HomeModule]: Remove home failed! player: {}, homeName: {}, error: {}",
                    player.getRealName(),
                    name,
                    res.error().message()
                );
                ev.invokeCallback(false);
                ev.cancel();
                return;
            }
            mc_utils::sendText(player, "Home {} removed!"_trl(player.getLocaleCode(), name));

            auto removed = HomeRemovedEvent(player, name);
            bus.publish(removed);

            ev.invokeCallback(true);
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<PlayerRequestGoHomeEvent>(
        [this](PlayerRequestGoHomeEvent& ev) {
            auto& bus = ll::event::EventBus::getInstance();

            auto& player     = ev.getPlayer();
            auto  localeCode = player.getLocaleCode();
            auto& name       = ev.getName();

            auto storage = getStorage();
            if (!storage) {
                throw std::runtime_error("HomeStorage not found");
            }

            auto home = storage->getHome(player.getUuid(), name);
            if (!home) {
                mc_utils::sendText<mc_utils::Error>(player, "Home does not exist"_trl(localeCode));
                ev.cancel();
                return;
            }

            auto teleporting = HomeTeleportingEvent(player, home.value());
            bus.publish(teleporting);

            if (teleporting.isCancelled()) {
                ev.cancel();
                return;
            }

            home->teleport(player);

            auto teleported = HomeTeleportedEvent(player, home.value());

            bus.publish(teleported);
            ev.invokeCallback(home.value());
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<HomeTeleportingEvent>(
        [this](HomeTeleportingEvent& ev) {
            auto& player     = ev.getPlayer();
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

            auto cl = PriceCalculate(getConfig().modules.home.goHomeCalculate);
            cl.addVariable("dimid", ev.getHome().dimid);
            auto price = cl.eval();

            if (!price) {
                mc_utils::sendText<mc_utils::Error>(player, "Failed to calculate price"_trl(localeCode));
                TeleportSystem::getInstance().getSelf().getLogger().error(
                    "[HomeModule]: Calculate price failed! player: {}, homeName: {}, error: {}",
                    player.getRealName(),
                    ev.getHome().name,
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

            cooldown.setCooldown(uuidKey, getConfig().modules.home.cooldownTime);
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<PlayerRequestEditHomeEvent>(
        [this](PlayerRequestEditHomeEvent& ev) {
            auto& bus        = ll::event::EventBus::getInstance();
            auto& player     = ev.getPlayer();
            auto  localeCode = player.getLocaleCode();

            auto&      name    = ev.getName();
            auto const type    = ev.getType();
            auto const newPos  = ev.getNewPosition();
            auto const newName = ev.getNewName();

            auto storage = this->getStorage();
            auto home    = storage->getHome(player.getUuid(), name);
            if (!home) {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Home {} does not exist, operation aborted"_trl(localeCode, name)
                );
                ev.cancel();
                return;
            }

            auto editing = HomeEditingEvent(player, type, name, *home, newPos, newName);
            bus.publish(editing);
            if (editing.isCancelled()) {
                ev.cancel();
                return;
            }

            if (newName.has_value()) {
                home->name = *newName;
            }
            if (newPos.has_value()) {
                home->updatePosition(*newPos);
                home->dimid = player.getDimensionId();
            }

            if (auto res = storage->updateHome(player.getUuid(), name, *home)) {
                mc_utils::sendText(player, "Home {} updated"_trl(localeCode, name));
            } else {
                mc_utils::sendText(
                    player,
                    "Failed to update home data: {}"_trl(localeCode, res.error().message())
                );
                ev.cancel();
                return;
            }

            auto edited = HomeEditedEvent(player, type, name, *home, newPos, newName);
            bus.publish(edited);
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<HomeEditingEvent>(
        [this](HomeEditingEvent& ev) {
            auto&      player     = ev.getPlayer();
            auto       localeCode = player.getLocaleCode();
            auto const newName    = ev.getNewName();

            if (newName.has_value()) {
                auto validator = makeNameValidator();
                if (auto result = validator.validate(*newName); result != NameValidation::Ok) {
                    sendNameValidationError(player, localeCode, result, *newName);
                    ev.cancel();
                }
            }
        },
        ll::event::EventPriority::High
    ));


    // Admin
    mListeners.emplace_back(bus.emplaceListener<AdminRequestGoPlayerHomeEvent>(
        [this](AdminRequestGoPlayerHomeEvent& ev) {
            auto& bus    = ll::event::EventBus::getInstance();
            auto& player = ev.getAdmin();
            auto& target = ev.getTarget();
            auto& home   = ev.getHome();

            auto ing = AdminTeleportingPlayerHomeEvent{player, target, home};
            bus.publish(ing);

            if (ing.isCancelled()) {
                return;
            }

            home.teleport(player);

            auto ed = AdminTeleportedPlayerHomeEvent{player, target, home};
            bus.publish(ed);
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<AdminRequestCreateHomeForPlayerEvent>(
        [this](AdminRequestCreateHomeForPlayerEvent& ev) {
            auto& bus        = ll::event::EventBus::getInstance();
            auto& player     = ev.getAdmin();
            auto  localeCode = player.getLocaleCode();
            auto& target     = ev.getTarget();
            auto& name       = ev.getName();
            auto& pos        = ev.getPosition();
            auto  dimid      = ev.getDimid();

            auto ing = AdminCreateingHomeForPlayerEvent{player, target, name, dimid, pos};
            bus.publish(ing);

            if (ing.isCancelled()) {
                return;
            }

            auto uuid = getStorageManager().resolveUuid(target);
            if (!uuid) {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Failed to resolve player {}: they must join the server once first"_trl(localeCode, target)
                );
                return;
            }

            auto home = HomeStorage::Home::make(pos, dimid, name);

            auto storage = this->getStorage();
            if (auto res = storage->addHome(uuid.value(), home)) {
                mc_utils::sendText(player, "Created home {} for player {}"_trl(localeCode, name, target));
            } else {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Failed to create home {} for player {}: {}"_trl(localeCode, name, target, res.error().message())
                );
            }
        },
        ll::event::EventPriority::High
    ));
    mListeners.emplace_back(bus.emplaceListener<AdminCreateingHomeForPlayerEvent>(
        [this](AdminCreateingHomeForPlayerEvent& ev) {
            auto& player     = ev.getAdmin();
            auto  localeCode = player.getLocaleCode();
            auto  target     = ev.getTarget();
            auto& name       = ev.getName();
            auto  dimid      = ev.getDimid();

            if (getConfig().modules.home.disallowedDimensions.contains(dimid)) {
                mc_utils::sendText<mc_utils::Error>(player, "Homes cannot be created in this dimension"_trl(localeCode));
                ev.cancel();
                return;
            }

            auto validator = makeNameValidator();
            if (auto result = validator.validate(name); result != NameValidation::Ok) {
                sendNameValidationError(player, localeCode, result, name);
                ev.cancel();
                return;
            }

            if (auto uuid = getStorageManager().resolveUuid(target)) {
                if (getStorage()->hasHome(uuid.value(), name)) {
                    mc_utils::sendText<mc_utils::Error>(
                        player,
                        "Home name already exists, please use another name"_trl(localeCode)
                    );
                    ev.cancel();
                    return;
                }
            }
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<AdminRequestEditPlayerHomeEvent>(
        [this](AdminRequestEditPlayerHomeEvent& ev) {
            auto& bus     = ll::event::EventBus::getInstance();
            auto& player  = ev.getAdmin();
            auto& target  = ev.getTarget();
            auto& home    = ev.getHome();
            auto& newHome = ev.getNewHome();

            auto ing = AdminEditingPlayerHomeEvent{player, target, home, newHome};
            bus.publish(ing);

            if (ing.isCancelled()) {
                return;
            }

            auto uuid = getStorageManager().resolveUuid(target);
            if (!uuid) {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Failed to resolve player {}: they must join the server once first"_trl(
                        player.getLocaleCode(),
                        target
                    )
                );
                return;
            }

            auto storage = this->getStorage();
            if (auto res = storage->updateHome(uuid.value(), home.name, newHome)) {
                mc_utils::sendText(
                    player,
                    "Updated home {} of player {}"_trl(player.getLocaleCode(), home.name, target)
                );
            } else {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Failed to update home {} of player {}: {}"_trl(
                        player.getLocaleCode(),
                        home.name,
                        target,
                        res.error().message()
                    )
                );
            }

            auto ed = AdminEditedPlayerHomeEvent{player, target, home, newHome};
            bus.publish(ed);
        },
        ll::event::EventPriority::High
    ));
    mListeners.emplace_back(bus.emplaceListener<AdminEditingPlayerHomeEvent>(
        [](AdminEditingPlayerHomeEvent& ev) {
            auto&       player     = ev.getAdmin();
            auto        localeCode = player.getLocaleCode();
            auto const& newName    = ev.getNewHome().name;

            auto validator = makeNameValidator();
            if (auto result = validator.validate(newName); result != NameValidation::Ok) {
                sendNameValidationError(player, localeCode, result, newName);
                ev.cancel();
            }
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<AdminRequestRemovePlayerHomeEvent>(
        [this](AdminRequestRemovePlayerHomeEvent& ev) {
            auto& bus    = ll::event::EventBus::getInstance();
            auto& player = ev.getAdmin();
            auto& target = ev.getTarget();
            auto& home   = ev.getHome();

            auto ing = AdminRemovingPlayerHomeEvent{player, target, home};
            bus.publish(ing);

            if (ing.isCancelled()) {
                return;
            }

            auto uuid = getStorageManager().resolveUuid(target);
            if (!uuid) {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Failed to resolve player {}: they must join the server once first"_trl(
                        player.getLocaleCode(),
                        target
                    )
                );
                return;
            }

            auto storage = this->getStorage();
            if (auto res = storage->removeHome(uuid.value(), home.name)) {
                mc_utils::sendText(
                    player,
                    "Removed home {} of player {}"_trl(player.getLocaleCode(), home.name, target)
                );
            } else {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Failed to remove home {} of player {}: {}"_trl(
                        player.getLocaleCode(),
                        home.name,
                        target,
                        res.error().message()
                    )
                );
                return;
            }

            auto rm = AdminRemovedPlayerHomeEvent{player, target, home};
            bus.publish(rm);
        },
        ll::event::EventPriority::High
    ));

    HomeCommand::setup();

    return true;
}

bool HomeModule::disable() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& ptr : mListeners) {
        bus.removeListener(ptr);
    }
    mListeners.clear();

    return true;
}

HomeStorage* HomeModule::getStorage() const { return getStorageManager().getStorage<HomeStorage>(); }

Cooldown& HomeModule::getCooldown() { return mCooldown; }

} // namespace ltps::home