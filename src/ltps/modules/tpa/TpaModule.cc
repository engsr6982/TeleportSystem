#include "ltps/modules/tpa/TpaModule.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/PlayerInfo.h"
#include "ltps/TeleportSystem.h"
#include "ltps/base/Config.h"
#include "ltps/common/PriceCalculate.h"
#include "ltps/modules/tpa/TpaCommand.h"
#include "ltps/modules/tpa/TpaRequest.h"
#include "ltps/modules/tpa/event/TpaEvents.h"
#include "ltps/utils/McUtils.h"
#include <algorithm>


namespace ltps::tpa {

TpaModule::TpaModule() = default;

std::vector<std::string> TpaModule::getDependencies() const { return {}; }

bool TpaModule::isLoadable() const { return getConfig().modules.tpa.enable; }

bool TpaModule::init() {
    if (!mTpaRequestPool) {
        mTpaRequestPool = std::make_unique<TpaRequestPool>();
    }
    return true;
}

bool TpaModule::enable() {
    auto& bus = ll::event::EventBus::getInstance();

    mListeners.emplace_back(bus.emplaceListener<CreateTpaRequestEvent>(
        [this, &bus](CreateTpaRequestEvent& ev) {
            auto before = CreatingTpaRequestEvent(ev);
            bus.publish(before);

            if (before.isCancelled()) {
                return;
            }

            auto ptr = getRequestPool().createRequest(ev.getSender(), ev.getReceiver(), ev.getType());

            ev.invokeCallback(ptr);

            bus.publish(CreatedTpaRequestEvent(ptr));
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<CreatingTpaRequestEvent>(
        [this](CreatingTpaRequestEvent& ev) {
            auto& sender = ev.getSender();

            auto localeCode = sender.getLocaleCode();

            // 维度检查
            if (std::find(
                    getConfig().modules.tpa.disallowedDimensions.begin(),
                    getConfig().modules.tpa.disallowedDimensions.end(),
                    sender.getDimensionId()
                )
                != getConfig().modules.tpa.disallowedDimensions.end()) {
                mc_utils::sendText<mc_utils::Error>(sender, "此功能在当前维度不可用"_trl(localeCode));
                ev.cancel();
                return;
            }

            // TPA 请求冷却
            if (this->mCooldown.isCooldown(sender.getRealName())) {
                mc_utils::sendText<mc_utils::Error>(
                    sender,
                    "TPA 请求冷却中，剩余时间 {0}"_trl(
                        localeCode,
                        this->mCooldown.getCooldownString(sender.getRealName())
                    )
                );
                ev.cancel();
                return;
            }
            this->mCooldown.setCooldown(sender.getRealName(), getConfig().modules.tpa.cooldownTime);

            // 费用检查
            PriceCalculate cl(getConfig().modules.tpa.createRequestCalculate);
            auto           clValue = cl.eval();
            if (!clValue.has_value()) {
                TeleportSystem::getInstance().getSelf().getLogger().error(
                    "An exception occurred while calculating the TPA price, please check the configuration file.\n{}",
                    clValue.error()
                );
                mc_utils::sendText<mc_utils::Error>(sender, "TPA 模块异常，请联系管理员"_trl(localeCode));
                ev.cancel();
                return;
            }

            auto price = static_cast<llong>(*clValue);

            auto economy = EconomySystemManager::getInstance().getEconomySystem();
            if (!economy->reduce(sender, price)) {
                economy->sendNotEnoughMoneyMessage(sender, price, localeCode);
                ev.cancel();
            }
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<CreatedTpaRequestEvent>(
        [](CreatedTpaRequestEvent& ev) {
            auto request  = ev.getRequest();
            auto sender   = request->getSender();
            auto receiver = request->getReceiver();
            auto type     = TpaRequest::getTypeString(request->getType());

            mc_utils::sendText(
                *sender,
                "已向 '{0}' 发起 '{1}' 请求"_trl(sender->getLocaleCode(), receiver->getRealName(), type)
            );
            mc_utils::sendText(
                *receiver,
                "收到来自 '{0}' 的 '{1}' 请求"_trl(receiver->getLocaleCode(), sender->getRealName(), type)
            );
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<PlayerExecuteTpaCommandEvent>(
        [this](PlayerExecuteTpaCommandEvent& ev) { handlePlayerExecuteTpaCommand(ev); },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<TpaRequestExpiredEvent>([](TpaRequestExpiredEvent& ev) {
        auto& req = ev.getRequest();
        if (req->isSenderAndReceiverOnline()) {
            req->notifyExpired(); // 通知双方请求已过期
        }
    }));

    TpaCommand::setup();

    return true;
}

bool TpaModule::disable() {
    mTpaRequestPool.reset();

    auto& bus = ll::event::EventBus::getInstance();
    for (auto& listener : mListeners) {
        bus.removeListener(listener);
    }
    mListeners.clear();

    return true;
}

Cooldown& TpaModule::getCooldown() { return mCooldown; }

TpaRequestPool&       TpaModule::getRequestPool() { return *mTpaRequestPool; }
TpaRequestPool const& TpaModule::getRequestPool() const { return *mTpaRequestPool; }


void TpaModule::handlePlayerExecuteTpaCommand(PlayerExecuteTpaCommandEvent& ev) {
    auto&      self       = ev.getPlayer();
    auto const localeCode = self.getLocaleCode();

    if (self.isSleeping()) {
        mc_utils::sendText<mc_utils::Error>(self, "你不能在睡觉时使用此命令"_trl(localeCode));
        return;
    }

    auto const action = ev.getAction();
    switch (action) {
    case PlayerExecuteTpaCommandEvent::Action::Accept:
    case PlayerExecuteTpaCommandEvent::Action::Deny:
        handleAcceptOrDenyTpaRequest(self, action == PlayerExecuteTpaCommandEvent::Action::Accept);
        break;
    case PlayerExecuteTpaCommandEvent::Action::Cancel:
        handleCancelTpaRequest(self);
        break;
    }
}

void TpaModule::handleAcceptOrDenyTpaRequest(Player& receiver, bool accept) {
    auto const localeCode = receiver.getLocaleCode();

    auto& pool    = this->getRequestPool();
    auto  senders = pool.getSenders(receiver.getUuid());

    switch (senders.size()) {
    case 0:
        mc_utils::sendText<mc_utils::Error>(receiver, "您没有收到任何 TPA 请求"_trl(localeCode));
        return;
    case 1: {
        auto request = pool.getRequest(senders[0], receiver.getUuid());
        if (request) {
            accept ? request->accept() : request->deny();
        } else {
            mc_utils::sendText<mc_utils::Error>(receiver, "TPA 请求不存在"_trl(localeCode));
            TeleportSystem::getInstance().getSelf().getLogger().error("An unexpected request is null pointer.");
        }
        return;
    }
    default: {
        auto& infoDb = ll::service::PlayerInfo::getInstance();

        ll::form::SimpleForm fm;
        fm.setTitle("Tpa 请求列表 [{}]"_trl(localeCode, senders.size()));
        fm.setContent("选择一个要 接受/拒绝 的 TPA 请求"_trl(localeCode));

        for (auto const& sender : senders) {
            auto info = infoDb.fromUuid(sender);
            fm.appendButton(
                "发起者: {0}"_trl(localeCode, info.has_value() ? info->name : sender.asString()),
                [&pool, sender, accept](Player& receiver) {
                    if (auto request = pool.getRequest(sender, receiver.getUuid())) {
                        accept ? request->accept() : request->deny();
                    }
                }
            );
        }

        fm.sendTo(receiver);

        return;
    }
    }
}

void TpaModule::handleCancelTpaRequest(Player& sender) {
    auto const localeCode = sender.getLocaleCode();
    auto&      pool       = this->getRequestPool();

    auto requests = pool.getInitiatedRequest(sender);
    switch (requests.size()) {
    case 0:
        mc_utils::sendText<mc_utils::Error>(sender, "您没有发起任何 TPA 请求"_trl(localeCode));
        break;
    case 1:
        requests[0]->cancel();
        break;
    default: {
        auto& infoDb = ll::service::PlayerInfo::getInstance();

        ll::form::SimpleForm fm;
        fm.setTitle("Tpa 请求列表 [{}]"_trl(localeCode, requests.size()));
        fm.setContent("请选择需要取消的 Tpa 请求"_trl(localeCode));

        for (auto& request : requests) {
            auto info = infoDb.fromUuid(request->getReceiverUUID());
            fm.appendButton(
                "接收者: {0}"_trl(localeCode, info.has_value() ? info->name : request->getReceiverUUID().asString()),
                [request](Player&) { request->cancel(); }
            );
        }
        fm.sendTo(sender);
    }
    }
}


} // namespace ltps::tpa