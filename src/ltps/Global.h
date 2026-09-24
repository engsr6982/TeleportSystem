#pragma once

#ifdef TPS_EXPORTS
#define TPSAPI __declspec(dllexport)
#else
#define TPSAPI __declspec(dllimport)
#endif

#define TPSNDAPI [[nodiscard]] TPSAPI


#define TPS_DISALLOW_COPY(CLASS)                                                                                       \
    CLASS(const CLASS&)            = delete;                                                                           \
    CLASS& operator=(const CLASS&) = delete;

#define TPS_DISALLOW_MOVE(CLASS)                                                                                       \
    CLASS(CLASS&&)            = delete;                                                                                \
    CLASS& operator=(CLASS&&) = delete;

#define TPS_DISALLOW_COPY_AND_MOVE(CLASS) TPS_DISALLOW_COPY(CLASS) TPS_DISALLOW_MOVE(CLASS)

#define IMPL_EVENT_EMITTER(EventName)                                                                                  \
    static std::unique_ptr<ll::event::EmitterBase> emitterFactory##EventName();                                        \
    class EventName##Emitter : public ll::event::Emitter<emitterFactory##EventName, EventName> {};                     \
    static std::unique_ptr<ll::event::EmitterBase> emitterFactory##EventName() {                                       \
        return std::make_unique<EventName##Emitter>();                                                                 \
    }


#include "ll/api/Expected.h"
#include "ll/api/i18n/I18n.h"
namespace ltps {

using ll::operator""_tr;
using ll::operator""_trl;

template <typename T>
using Result = ll::Expected<T>;

using RealName = std::string;

} // namespace ltps