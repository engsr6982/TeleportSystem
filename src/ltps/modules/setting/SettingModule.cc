#include "SettingModule.h"

#include "ltps/database/StorageManager.h"

#include <ll/api/event/EventBus.h>
#include <ll/api/event/player/PlayerJoinEvent.h>

namespace ltps::setting {

SettingModule::SettingModule() = default;

std::vector<std::string> SettingModule::getDependencies() const { return {}; }

bool SettingModule::isLoadable() const { return true; }

bool SettingModule::init() { return true; }

bool SettingModule::enable() {
    auto& bus = ll::event::EventBus::getInstance();

    mListeners.emplace_back(bus.emplaceListener<ll::event::PlayerJoinEvent>([this](ll::event::PlayerJoinEvent& ev) {
        if (ev.self().isSimulatedPlayer()) {
            return;
        }
        getStorage()->initPlayerSetting(ev.self().getUuid());
    }));

    return true;
}

bool SettingModule::disable() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& p : mListeners) {
        bus.removeListener(p);
    }
    mListeners.clear();

    return true;
}

SettingStorage* SettingModule::getStorage() const { return getStorageManager().getStorage<SettingStorage>(); }


} // namespace ltps::setting