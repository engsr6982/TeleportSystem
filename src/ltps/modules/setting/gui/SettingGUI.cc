#include "SettingGUI.h"

#include "ltps/TeleportSystem.h"
#include "ltps/modules/setting/SettingStorage.h"
#include "ltps/utils/McUtils.h"

#include <ll/api/form/CustomForm.h>
#include <mc/world/actor/player/Player.h>

namespace ltps::setting {


void SettingGUI::sendMainGUI(Player& player) {
    auto localeCode = player.getLocaleCode();
    auto setting    = TeleportSystem::getInstance().getStorageManager().getStorage<SettingStorage>()->getSettingData(
        player.getUuid()
    );

    ll::form::CustomForm fm{"Setting - Personal Settings"_trl(localeCode)};
    fm.appendToggle("allowTpa", "Allow TPA requests to me"_trl(localeCode), setting.allowTpa);
    fm.appendToggle("deathPopup", "Show a back-to-death-point popup after death"_trl(localeCode), setting.deathPopup);
    fm.appendToggle("tpaPopup", "Show a dialog when receiving a TPA request"_trl(localeCode), setting.tpaPopup);

    fm.sendTo(player, [](Player& self, ll::form::CustomFormResult const& res, auto) {
        if (!res) return;

        auto localeCode = self.getLocaleCode();

        bool allowTpa   = std::get<uint64>(res->at("allowTpa"));
        bool deathPopup = std::get<uint64>(res->at("deathPopup"));
        bool tpaPopup   = std::get<uint64>(res->at("tpaPopup"));

        TeleportSystem::getInstance().getStorageManager().getStorage<SettingStorage>()->setSettingData(
            self.getUuid(),
            {.deathPopup = deathPopup, .allowTpa = allowTpa, .tpaPopup = tpaPopup}
        );

        mc_utils::sendText(self, "Settings saved"_trl(localeCode));
    });
}


} // namespace ltps::setting