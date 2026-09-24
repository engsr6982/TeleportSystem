#include "DeathGUI.h"

#include "ltps/TeleportSystem.h"
#include "ltps/modules/death/DeathStorage.h"
#include "ltps/modules/death/event/DeathEvents.h"
#include "ltps/utils/McUtils.h"

#include <ll/api/event/EventBus.h>
#include <mc/world/level/dimension/VanillaDimensions.h>

namespace ltps ::death {


void DeathGUI::sendMainMenu(Player& player, BackCB backCb) {
    auto localeCode = player.getLocaleCode();

    auto infos = TeleportSystem::getInstance().getStorageManager().getStorage<DeathStorage>()->getDeathInfos(
        player.getUuid()
    );

    if (infos.empty()) {
        mc_utils::sendText(player, "You have no death records"_trl(localeCode));
        return;
    }

    auto fm = BackSimpleForm{std::move(backCb)};
    fm.setTitle("Death - Records"_trl(localeCode));
    fm.setContent("You have {0} death record(s)"_trl(localeCode, infos.size()));

    int index = 0;
    for (auto& info : infos) {
        fm.appendButton("{}\n{}"_tr(info.time, info.toPosString()), [index](Player& self) {
            sendBackGUI(self, index, BackSimpleForm::makeCallback<sendMainMenu>(nullptr));
        });
        index++;
    }

    fm.sendTo(player);
}


void DeathGUI::sendBackGUI(Player& player, int index, BackCB backCb) {
    auto localeCode = player.getLocaleCode();

    auto info = TeleportSystem::getInstance().getStorageManager().getStorage<DeathStorage>()->getSpecificDeathInfo(
        player.getUuid(),
        index
    );
    if (!info) {
        mc_utils::sendText(player, "You have no death records"_trl(localeCode));
        return;
    }

    BackSimpleForm{std::move(backCb)}
        .setTitle("Death - Record"_trl(localeCode))
        .setContent("Time: {0}\nPosition: {1}"_trl(localeCode, info->time, info->toPosString()))
        .appendButton(
            "Go to death point"_trl(localeCode),
            [index](Player& self) {
                ll::event::EventBus::getInstance().publish(PlayerRequestBackDeathPointEvent{self, index});
            }
        )
        .appendButton("Cancel"_trl(localeCode))
        .sendTo(player);
}


} // namespace ltps::death