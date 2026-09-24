#pragma once
#include "ll/api/event/Cancellable.h"
#include "ll/api/event/Event.h"
#include "ltps/Global.h"
#include "ltps/modules/home/HomeStorage.h"
#include "mc/world/actor/player/Player.h"
#include <functional>
#include <optional>
#include <string>


namespace ltps::home {

using ll::event::Cancellable;
using ll::event::Event;


class IPlayerRequestActionEvent {
protected:
    Player&     mPlayer;
    std::string mName;

public:
    TPSAPI explicit IPlayerRequestActionEvent(Player& player, std::string name);

    TPSNDAPI Player& getPlayer() const;
    TPSNDAPI std::string const& getName() const;
};

/**
 * @brief 请求添加家(使用玩家当前坐标作为家坐标)
 * 流程: PlayerRequestAddHomeEvent -> HomeAddingEvent -> HomeStorage::addHome -> HomeAddedEvent
 */
class PlayerRequestAddHomeEvent final : public Cancellable<Event>, public IPlayerRequestActionEvent {
    using Callback = std::function<void(Player& player, std::string name, home::HomeStorage::Home home)>;
    Callback mCallback;

public:
    TPSAPI explicit PlayerRequestAddHomeEvent(Player& player, std::string name, Callback callback = {});

    TPSAPI void invokeCallback(home::HomeStorage::Home home) const;
};

class IAddHomeEvent {
protected:
    Player&                  mPlayer;
    home::HomeStorage::Home mHome; // 按值持有: 发布点常为临时量

public:
    TPSAPI explicit IAddHomeEvent(Player& player, home::HomeStorage::Home const& home);

    TPSNDAPI Player& getPlayer() const;

    TPSNDAPI home::HomeStorage::Home const& getHome() const;
};

class HomeAddingEvent final : public Cancellable<Event>, public IAddHomeEvent {
public:
    TPSAPI explicit HomeAddingEvent(Player& player, home::HomeStorage::Home const& home);
};

class HomeAddedEvent final : public Event, public IAddHomeEvent {
public:
    TPSAPI explicit HomeAddedEvent(Player& player, home::HomeStorage::Home const& home);
};


/**
 * @brief 请求移除家
 * 流程: PlayerRequestRemoveHomeEvent -> HomeRemovingEvent -> HomeStorage::removeHome -> HomeRemovedEvent
 */
class PlayerRequestRemoveHomeEvent final : public Cancellable<Event>, public IPlayerRequestActionEvent {
    using Callback = std::function<void(Player& player, std::string name, bool success)>;
    Callback mCallback;

public:
    TPSAPI explicit PlayerRequestRemoveHomeEvent(Player& player, std::string name, Callback callback = {});

    TPSAPI void invokeCallback(bool success) const;
};

class IRemoveHomeEvent {
protected:
    Player&            mPlayer;
    std::string const& mName;

public:
    TPSAPI explicit IRemoveHomeEvent(Player& player, std::string const& name);

    TPSNDAPI Player& getPlayer() const;
    TPSNDAPI std::string const& getName() const;
};

class HomeRemovingEvent final : public Cancellable<Event>, public IRemoveHomeEvent {
public:
    TPSAPI explicit HomeRemovingEvent(Player& player, std::string const& name);
};

class HomeRemovedEvent final : public Event, public IRemoveHomeEvent {
public:
    TPSAPI explicit HomeRemovedEvent(Player& player, std::string const& name);
};


/**
 * @brief 请求传送回家
 * 流程: PlayerRequestGoHomeEvent -> HomeTeleportingEvent -> HomeStorage::Home::teleport -> HomeTeleportedEvent
 */
class PlayerRequestGoHomeEvent final : public Cancellable<Event>, public IPlayerRequestActionEvent {
    using Callback = std::function<void(Player& player, std::string name, home::HomeStorage::Home home)>;
    Callback mCallback;

public:
    TPSAPI explicit PlayerRequestGoHomeEvent(Player& player, std::string name, Callback callback = {});

    TPSAPI void invokeCallback(home::HomeStorage::Home home) const;
};

class ITeleportHomeEvent {
protected:
    Player&                  mPlayer;
    home::HomeStorage::Home mHome; // 按值持有: 发布点常为临时量

public:
    TPSAPI explicit ITeleportHomeEvent(Player& player, home::HomeStorage::Home const& home);

    TPSNDAPI Player& getPlayer() const;
    TPSNDAPI home::HomeStorage::Home const& getHome() const;
};

class HomeTeleportingEvent final : public Cancellable<Event>, public ITeleportHomeEvent {
public:
    TPSAPI explicit HomeTeleportingEvent(Player& player, home::HomeStorage::Home const& home);
};

class HomeTeleportedEvent final : public Event, public ITeleportHomeEvent {
public:
    TPSAPI explicit HomeTeleportedEvent(Player& player, home::HomeStorage::Home const& home);
};


class PlayerRequestEditHomeEvent final : public Cancellable<Event>, public IPlayerRequestActionEvent {
public:
    enum class Type { Name, Position };

    TPSAPI explicit PlayerRequestEditHomeEvent(
        Player&                    player,
        std::string                name,
        Type                       type,
        std::optional<Vec3>        newPosition = std::nullopt,
        std::optional<std::string> newName     = std::nullopt
    );

    TPSNDAPI Type getType() const;

    TPSNDAPI std::optional<Vec3> getNewPosition() const;

    TPSNDAPI std::optional<std::string> getNewName() const;

private:
    Type                       mType;
    std::optional<Vec3>        mNewPosition{std::nullopt};
    std::optional<std::string> mNewName{std::nullopt};
};

class IHomeEditEvent {
protected:
    using Type = PlayerRequestEditHomeEvent::Type;

    Player&                    mPlayer;
    Type                       mType;
    std::string                mName;
    HomeStorage::Home          mHome; // 按值持有: 发布点常为临时量
    std::optional<Vec3>        mNewPosition;
    std::optional<std::string> mNewName;

public:
    TPSAPI explicit IHomeEditEvent(
        Player&                    player,
        Type                       type,
        std::string                name,
        HomeStorage::Home const&   home,
        std::optional<Vec3>        newpos,
        std::optional<std::string> newName
    );

    TPSNDAPI Player& getPlayer() const;

    TPSNDAPI Type getType() const;

    TPSNDAPI HomeStorage::Home const& getHome() const;

    TPSNDAPI std::string const& getName() const;

    TPSNDAPI std::optional<Vec3> getNewPosition() const;

    TPSNDAPI std::optional<std::string> getNewName() const;
};

class HomeEditingEvent final : public Cancellable<Event>, public IHomeEditEvent {
public:
    TPSAPI explicit HomeEditingEvent(
        Player&                    player,
        Type                       type,
        std::string                name,
        HomeStorage::Home const&   home,
        std::optional<Vec3>        newpos,
        std::optional<std::string> newName
    );
};

class HomeEditedEvent final : public Event, public IHomeEditEvent {
public:
    TPSAPI explicit HomeEditedEvent(
        Player&                    player,
        Type                       type,
        std::string                name,
        HomeStorage::Home const&   home,
        std::optional<Vec3>        newpos,
        std::optional<std::string> newName
    );
};


// =========
// Admin
// =========


class IAdminEvent {
protected:
    Player&         mAdmin;  // 发起请求的管理员
    RealName const& mTarget; // 目标玩家

public:
    TPSAPI explicit IAdminEvent(Player& admin, RealName const& target);

    TPSNDAPI Player&         getAdmin() const;
    TPSNDAPI RealName const& getTarget() const;
};

class IAdminOperateHomeEvent : public IAdminEvent {
protected:
    HomeStorage::Home mHome; // 正在操作的家 (按值持有: 发布点常为临时量)

public:
    TPSAPI explicit IAdminOperateHomeEvent(Player& admin, RealName const& target, HomeStorage::Home const& home);

    TPSNDAPI HomeStorage::Home const& getHome() const;
};

/**
 * @brief 管理员请求传送到玩家的家
 */
class AdminRequestGoPlayerHomeEvent final : public Event, public IAdminOperateHomeEvent {
public:
    TPSAPI explicit AdminRequestGoPlayerHomeEvent(Player& admin, RealName const& target, HomeStorage::Home const& home);
};

class AdminTeleportingPlayerHomeEvent final : public Cancellable<Event>, public IAdminOperateHomeEvent {
public:
    TPSAPI explicit AdminTeleportingPlayerHomeEvent(
        Player&                  admin,
        RealName const&          target,
        HomeStorage::Home const& home
    );
};

class AdminTeleportedPlayerHomeEvent final : public Event, public IAdminOperateHomeEvent {
public:
    TPSAPI explicit AdminTeleportedPlayerHomeEvent(
        Player&                  admin,
        RealName const&          target,
        HomeStorage::Home const& home
    );
};


class IAdminCreateHomeEvent : public IAdminEvent {
protected:
    std::string mName;     // 家的名字
    int         mDimid;    // 家的维度
    Vec3        mPosition; // 家的位置

public:
    TPSAPI explicit IAdminCreateHomeEvent(
        Player&         admin,
        RealName const& target,
        std::string     name,
        int             dimid,
        Vec3            position
    );

    TPSNDAPI std::string const& getName() const;
    TPSNDAPI int                getDimid() const;
    TPSNDAPI Vec3 const&        getPosition() const;
};

/**
 * @brief 管理员请求为玩家创建家
 */
class AdminRequestCreateHomeForPlayerEvent final : public Event, public IAdminCreateHomeEvent {
public:
    TPSAPI explicit AdminRequestCreateHomeForPlayerEvent(
        Player&         admin,
        RealName const& target,
        std::string     homeName,
        int             dimid,
        Vec3            homePosition
    );
};

class AdminCreateingHomeForPlayerEvent final : public Cancellable<Event>, public IAdminCreateHomeEvent {
public:
    TPSAPI explicit AdminCreateingHomeForPlayerEvent(
        Player&         admin,
        RealName const& target,
        std::string     homeName,
        int             dimid,
        Vec3            homePosition
    );
};

class AdminCreatedHomeForPlayerEvent final : public Event, public IAdminCreateHomeEvent {
public:
    TPSAPI explicit AdminCreatedHomeForPlayerEvent(
        Player&         admin,
        RealName const& target,
        std::string     homeName,
        int             dimid,
        Vec3            homePosition
    );
};


class IAdminEditHomeEvent : public IAdminOperateHomeEvent {
    HomeStorage::Home mNewHome; // 按值持有: 发布点常为临时量

public:
    TPSAPI explicit IAdminEditHomeEvent(
        Player&                  admin,
        RealName const&          target,
        HomeStorage::Home const& home,
        HomeStorage::Home const& newHomeF
    );

    TPSNDAPI HomeStorage::Home const& getNewHome() const;
};

/**
 * @brief 管理员请求编辑玩家的家
 */
class AdminRequestEditPlayerHomeEvent final : public Event, public IAdminEditHomeEvent {
public:
    TPSAPI explicit AdminRequestEditPlayerHomeEvent(
        Player&                  admin,
        RealName const&          target,
        HomeStorage::Home const& home,
        HomeStorage::Home const& newHome
    );
};

class AdminEditingPlayerHomeEvent final : public Cancellable<Event>, public IAdminEditHomeEvent {
public:
    TPSAPI explicit AdminEditingPlayerHomeEvent(
        Player&                  admin,
        RealName const&          target,
        HomeStorage::Home const& home,
        HomeStorage::Home const& newHome
    );
};

class AdminEditedPlayerHomeEvent final : public Event, public IAdminEditHomeEvent {
public:
    TPSAPI explicit AdminEditedPlayerHomeEvent(
        Player&                  admin,
        RealName const&          target,
        HomeStorage::Home const& home,
        HomeStorage::Home const& newHome
    );
};


/**
 * @brief 管理员请求删除玩家的家
 */
class AdminRequestRemovePlayerHomeEvent final : public Event, public IAdminOperateHomeEvent {
public:
    TPSAPI explicit AdminRequestRemovePlayerHomeEvent(
        Player&                  admin,
        RealName const&          target,
        HomeStorage::Home const& home
    );
};

class AdminRemovingPlayerHomeEvent final : public Cancellable<Event>, public IAdminOperateHomeEvent {
public:
    TPSAPI explicit AdminRemovingPlayerHomeEvent(Player& admin, RealName const& target, HomeStorage::Home const& home);
};

class AdminRemovedPlayerHomeEvent final : public Event, public IAdminOperateHomeEvent {
public:
    TPSAPI explicit AdminRemovedPlayerHomeEvent(Player& admin, RealName const& target, HomeStorage::Home const& home);
};


} // namespace ltps::home