#include "ltps/modules/tpa/TpaRequest.h"
#include "../setting/SettingStorage.h"
#include "fmt/core.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/i18n/I18n.h"
#include "ltps/TeleportSystem.h"
#include "ltps/base/Config.h"
#include "ltps/common/EconomySystem.h"
#include "ltps/database/StorageManager.h"
#include "ltps/modules/tpa/event/TpaEvents.h"
#include "ltps/utils/McUtils.h"
#include "ltps/utils/TimeUtils.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/ecs/WeakEntityRef.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include <chrono>
#include <memory>


namespace ltps::tpa {


struct TpaRequest::Impl {
    WeakRef<EntityContext> mSender;
    WeakRef<EntityContext> mReceiver;
    mce::UUID              mSenderUUID;
    mce::UUID              mReceiverUUID;
    Type                   mType;
    State                  mState;
    SystemTime             mCreationTime;   // 请求创建时间
    SteadyTime             mExpirationTime; // 请求失效时间

    explicit Impl(Player& sender, Player& receiver, Type type)
    : mSender(sender.getWeakEntity()),
      mReceiver(receiver.getWeakEntity()),
      mSenderUUID(sender.getUuid()),
      mReceiverUUID(receiver.getUuid()),
      mType(type),
      mState(State::Available),
      mCreationTime(time_utils::now()),
      mExpirationTime(std::chrono::steady_clock::now() + std::chrono::seconds(getConfig().modules.tpa.expirationTime)) {
    }
};


TpaRequest::TpaRequest(Player& sender, Player& receiver, Type type)
: mImpl(std::make_unique<Impl>(sender, receiver, type)) {}
TpaRequest::~TpaRequest() = default;

Player*          TpaRequest::getSender() const { return mImpl->mSender.tryUnwrap<Player>().as_ptr(); }
Player*          TpaRequest::getReceiver() const { return mImpl->mReceiver.tryUnwrap<Player>().as_ptr(); }
mce::UUID const& TpaRequest::getSenderUUID() const { return mImpl->mSenderUUID; }
mce::UUID const& TpaRequest::getReceiverUUID() const { return mImpl->mReceiverUUID; }

TpaRequest::Type  TpaRequest::getType() const { return mImpl->mType; }
TpaRequest::State TpaRequest::getState() const { return mImpl->mState; }

TpaRequest::SystemTime const& TpaRequest::getCreationTime() const { return mImpl->mCreationTime; }

std::chrono::seconds TpaRequest::getRemainingTime() const {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(
        mImpl->mCreationTime + std::chrono::seconds(getConfig().modules.tpa.expirationTime) - now
    );
}

std::string TpaRequest::getExpirationTime() const {
    // 获取过期时间点
    auto expirationTime = mImpl->mCreationTime + std::chrono::seconds(getConfig().modules.tpa.expirationTime);
    return time_utils::timeToString(expirationTime);
}

TpaRequest::SteadyTime const& TpaRequest::getExpireTime() const { return mImpl->mExpirationTime; }

bool TpaRequest::tryUpdateState(State state) {
    if (mImpl->mState == State::Available || mImpl->mState == state) {
        mImpl->mState = state; // 状态不可逆，只允许从Available状态转换
        return true;
    }
    return false;
}

bool TpaRequest::isExpired() const {
    auto now = std::chrono::steady_clock::now();
    return now >= mImpl->mExpirationTime;
}

bool TpaRequest::isFinalState() const { return mImpl->mState != State::Available; }

bool TpaRequest::isAvailable() const { return mImpl->mState == State::Available; }

bool TpaRequest::isSenderOnline() const { return mImpl->mSender.lock().has_value(); }

bool TpaRequest::isReceiverOnline() const { return mImpl->mReceiver.lock().has_value(); }

bool TpaRequest::isSenderAndReceiverOnline() const { return isSenderOnline() && isReceiverOnline(); }

void TpaRequest::refreshAvailability() {
    if (mImpl->mState != State::Available) {
        return; // 请求已经被处理，不再更新状态
    }
    if (!isSenderOnline()) {
        tryUpdateState(State::SenderOffline);
    } else if (!isReceiverOnline()) {
        tryUpdateState(State::ReceiverOffline);
    } else if (isExpired()) {
        tryUpdateState(State::Expired);
    }
}

void TpaRequest::accept() {
    refreshAvailability();
    if (!isAvailable()) {
        return;
    }

    auto& bus = ll::event::EventBus::getInstance();

    TpaRequestAcceptingEvent event(shared_from_this());
    bus.publish(event);
    if (event.isCancelled()) {
        return;
    }


    auto sender   = getSender();
    auto receiver = getReceiver();

    switch (mImpl->mType) {
    case Type::To: {
        sender->teleport(receiver->getPosition(), receiver->getDimensionId(), mc_utils::getRotation(*sender));
        break;
    }
    case Type::Here: {
        receiver->teleport(sender->getPosition(), sender->getDimensionId(), mc_utils::getRotation(*receiver));
        break;
    }
    }

    tryUpdateState(State::Accepted);
    notifyAccepted();

    bus.publish(TpaRequestAcceptedEvent(shared_from_this()));
}

void TpaRequest::deny() {
    refreshAvailability();
    if (!isAvailable()) {
        return;
    }

    auto& bus = ll::event::EventBus::getInstance();

    TpaRequestDenyingEvent event(shared_from_this());
    bus.publish(event);
    if (event.isCancelled()) {
        return;
    }

    tryUpdateState(State::Denied);
    notifyDenied();

    bus.publish(TpaRequestDeniedEvent(shared_from_this()));
}

void TpaRequest::cancel() {
    refreshAvailability();
    if (!isAvailable()) {
        return;
    }

    tryUpdateState(State::Cancelled);
    notifyCancelled();

    ll::event::EventBus::getInstance().publish(TpaRequestCancelledEvent(shared_from_this()));
}

void TpaRequest::sendFormToReceiver() {
    refreshAvailability();
    if (!isAvailable()) {
        return;
    }
    auto receiver = getReceiver();
    auto sender   = getSender();

    auto& settingStorage     = *TeleportSystem::getInstance().getStorageManager().getStorage<setting::SettingStorage>();
    auto  receiverSettings   = settingStorage.getSettingData(receiver->getRealName()).value();
    auto  receiverLocaleCode = receiver->getLocaleCode();

    if (!receiverSettings.tpaPopup) {
        return; // 玩家不接受 tpa 弹窗
    }

    ll::form::SimpleForm form;
    form.setTitle("Tpa Request"_trl(receiverLocaleCode));

    std::string desc = mImpl->mType == Type::To
                         ? "'{0}' 希望传送到您当前位置"_trl(receiverLocaleCode, sender->getRealName())
                         : "'{0}' 希望将您传送到他(她)那里"_trl(receiverLocaleCode, sender->getRealName());
    form.setContent(desc);

    std::weak_ptr<TpaRequest> self = shared_from_this();
    form.appendButton("接受"_trl(receiverLocaleCode), "textures/ui/realms_green_check", "path", [self](Player&) {
        if (auto req = self.lock()) {
            req->accept();
        }
    });
    form.appendButton("拒绝"_trl(receiverLocaleCode), "textures/ui/realms_red_x", "path", [self](Player&) {
        if (auto req = self.lock()) {
            req->deny();
        }
    });
    form.appendButton(
        "忽略\n失效时间: {0}"_trl(receiverLocaleCode, getExpirationTime()),
        "textures/ui/backup_replace",
        "path"
    );

    form.sendTo(*receiver);
}

void TpaRequest::notifyAccepted() const {
    auto sender   = getSender();
    auto receiver = getReceiver();
    auto type     = getType();

    if (!sender || !receiver) {
        return;
    }

    mc_utils::sendText(
        *sender,
        "'{0}' 接受了您的 '{1}' 请求。"_trl(
            sender->getLocaleCode(),
            receiver->getRealName(),
            TpaRequest::getTypeString(type)
        )
    );
    mc_utils::sendText(
        *receiver,
        "您接受了来自 '{0}' 的 '{1}' 请求。"_trl(
            receiver->getLocaleCode(),
            sender->getRealName(),
            TpaRequest::getTypeString(type)
        )
    );
}

void TpaRequest::notifyDenied() const {
    auto sender   = getSender();
    auto receiver = getReceiver();
    auto type     = getType();

    if (!sender || !receiver) {
        return;
    }

    mc_utils::sendText<mc_utils::Error>(
        *sender,
        "'{0}' 拒绝了您的 '{1}' 请求。"_trl(
            sender->getLocaleCode(),
            receiver->getRealName(),
            TpaRequest::getTypeString(type)
        )
    );
    mc_utils::sendText<mc_utils::Warn>(
        *receiver,
        "您拒绝了来自 '{0}' 的 '{1}' 请求。"_trl(
            receiver->getLocaleCode(),
            sender->getRealName(),
            TpaRequest::getTypeString(type)
        )
    );
}

void TpaRequest::notifyCancelled() const {
    _notifyState(getReceiver());
    _notifyState(getSender());
}

void TpaRequest::notifyExpired() const {
    _notifyState(getReceiver());
    _notifyState(getSender());
}

void TpaRequest::notifySenderOffline() const { _notifyState(getReceiver()); }

void TpaRequest::notifyReceiverOffline() const { _notifyState(getSender()); }

void TpaRequest::_notifyState(Player* player) const {
    if (player) {
        mc_utils::sendText<mc_utils::Error>(*player, getStateDescription(mImpl->mState, player->getLocaleCode()));
    }
}

std::string TpaRequest::getStateDescription(State state, std::string const& localeCode) {
    switch (state) {
    case State::Available:
        return "请求有效"_trl(localeCode);
    case State::Accepted:
        return "请求已接受"_trl(localeCode);
    case State::Denied:
        return "请求已拒绝"_trl(localeCode);
    case State::Expired:
        return "请求已过期"_trl(localeCode);
    case State::SenderOffline:
        return "发起者离线"_trl(localeCode);
    case State::ReceiverOffline:
        return "接收者离线"_trl(localeCode);
    case State::Cancelled:
        return "请求已取消"_trl(localeCode);
    default:
        return "未知状态"_trl(localeCode);
    }
}
std::string TpaRequest::getTypeString(Type type) {
    switch (type) {
    case Type::To:
        return "tpa";
    case Type::Here:
        return "tpahere";
    }
    return "unknown";
}


} // namespace ltps::tpa