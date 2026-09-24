#include "WarpGUI.h"

#include "ltps/TeleportSystem.h"
#include "ltps/modules/warp/event/WarpEvents.h"
#include "ltps/utils/McUtils.h"

#include <ll/api/event/EventBus.h>
#include <ll/api/form/CustomForm.h>

namespace ltps::warp {


void WarpGUI::sendMainMenu(Player& player, BackCB backCB) {
    auto localeCode = player.getLocaleCode();
    BackSimpleForm{std::move(backCB), BackSimpleForm::ButtonPos::Lower}
        .setTitle("Warp Menu"_trl(localeCode))
        .setContent(" · Please choose an action"_trl(localeCode))
        .appendButton(
            "Create warp"_trl(localeCode),
            "textures/ui/color_plus",
            "path",
            [](Player& self) { sendAddWarpGUI(self); }
        )
        .appendButton(
            "Go to warp"_trl(localeCode),
            "textures/ui/send_icon",
            "path",
            [](Player& self) { sendGoWarpGUI(self); }
        )
        .appendButton(
            "Remove warp"_trl(localeCode),
            "textures/ui/trash_default",
            "path",
            [](Player& self) { sendRemoveWarpGUI(self); }
        )
        .sendTo(player);
}

void WarpGUI::sendChooseWarpGUI(Player& player, ChooseWarpCB callback) {
    _sendChooseWarpGUI(
        player,
        TeleportSystem::getInstance().getStorageManager().getStorage<WarpStorage>()->getWarps(),
        std::move(callback)
    );
}

void WarpGUI::sendChooseNameGUI(Player& player, ChooseNameCB callback) {
    sendChooseWarpGUI(player, [cb = std::move(callback)](Player& self, WarpStorage::Warp warp) {
        cb(self, warp.name);
    });
}

void WarpGUI::_sendFuzzySearchGUI(Player& player, ChooseWarpCB callback) {
    auto localeCode = player.getLocaleCode();

    ll::form::CustomForm fm;
    fm.setTitle("Warp - Fuzzy search"_trl(localeCode));
    fm.appendInput("name", "Enter a warp name to search"_trl(localeCode), "string");
    fm.sendTo(player, [cb = std::move(callback)](Player& self, ll::form::CustomFormResult const& result, auto) {
        if (!result) return;
        auto name = std::get<std::string>(result->at("name"));
        if (name.empty()) {
            mc_utils::sendText<mc_utils::Error>(self, "Name cannot be empty"_trl(self.getLocaleCode()));
            return;
        }
        _sendChooseWarpGUI(
            self,
            TeleportSystem::getInstance().getStorageManager().getStorage<WarpStorage>()->queryWarp(name),
            std::move(cb)
        );
    });
}

void WarpGUI::_sendChooseWarpGUI(Player& player, WarpStorage::Warps const& warps, ChooseWarpCB callback) {
    auto localeCode = player.getLocaleCode();
    auto fm         = BackSimpleForm::make<WarpGUI::sendMainMenu>(nullptr);
    fm.setTitle("Warp - Choose a warp"_trl(localeCode));
    fm.setContent("Choose a warp to teleport to"_trl(localeCode));
    fm.appendButton(
        "Fuzzy search"_trl(localeCode),
        "textures/ui/magnifyingGlass",
        "path",
        [rawCB = callback](Player& self) { _sendFuzzySearchGUI(self, rawCB); }
    );
    for (auto const& warp : warps) {
        fm.appendButton(warp.name, [warp, cb = callback](Player& self) { cb(self, warp); });
    }
    fm.sendTo(player);
}


void WarpGUI::sendGoWarpGUI(Player& player) {
    sendChooseNameGUI(player, [](Player& self, std::string name) {
        ll::event::EventBus::getInstance().publish(PlayerRequestGoWarpEvent{self, name});
    });
}

void WarpGUI::sendAddWarpGUI(Player& player) {
    auto localeCode = player.getLocaleCode();
    ll::form::CustomForm{"Warp - Create public warp"_trl(localeCode)}
        .appendInput("name", "Enter warp name"_trl(localeCode), "string")
        .sendTo(player, [](Player& self, ll::form::CustomFormResult const& result, auto) {
            if (!result) return;

            auto name = std::get<std::string>(result->at("name"));
            if (name.empty()) {
                mc_utils::sendText<mc_utils::Error>(self, "Name cannot be empty"_trl(self.getLocaleCode()));
                return;
            }
            ll::event::EventBus::getInstance().publish(PlayerRequestAddWarpEvent{self, name});
        });
}

void WarpGUI::sendRemoveWarpGUI(Player& player) {
    sendChooseNameGUI(player, [](Player& self, std::string name) {
        ll::event::EventBus::getInstance().publish(PlayerRequestRemoveWarpEvent{self, name});
    });
}


} // namespace ltps::warp