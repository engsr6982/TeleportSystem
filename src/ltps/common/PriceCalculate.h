#pragma once
#include "ltps/Global.h"
#include <optional>
#include <string>
#include <unordered_map>


namespace ltps {


class PriceCalculate {
public:
    using Impl = std::unordered_map<std::string, double>;

    enum class InternalFuncOptions {
        None           = 0,
        RandomNum      = 1 << 0, // random_num()
        RandomNumRange = 1 << 1, // random_num_range(min, max)
        Random         = RandomNum | RandomNumRange,
        All            = Random
    };


    TPS_DISALLOW_COPY(PriceCalculate);
    virtual ~PriceCalculate() = default;

    TPSAPI explicit PriceCalculate(std::string expression, InternalFuncOptions options = InternalFuncOptions::All);
    TPSAPI explicit PriceCalculate(
        std::string         expression,
        Impl                variables,
        InternalFuncOptions options = InternalFuncOptions::All
    );

    TPSAPI                 PriceCalculate(PriceCalculate&&) noexcept;
    TPSAPI PriceCalculate& operator=(PriceCalculate&&) noexcept;

public:
    TPSAPI PriceCalculate& addVariable(std::string name, double value);

    TPSNDAPI std::optional<double> getVariable(std::string const& name) const;

    TPSNDAPI std::string getExpression() const;

    TPSAPI void setExpression(std::string expression);

    TPSNDAPI Impl&       getVariables();
    TPSNDAPI Impl const& getVariables() const;

    TPSAPI void setOptions(InternalFuncOptions options);

    TPSNDAPI Result<double> eval() const;

public:
    template <typename T>
    TPSNDAPI decltype(auto) operator[](T&& key) {
        return mVariables[std::forward<T>(key)];
    }

private:
    Impl                mVariables;
    std::string         mExpression;
    InternalFuncOptions mOptions;
};


namespace internals {

TPSNDAPI double random_num();
TPSNDAPI double random_num_range(double min, double max);

} // namespace internals


} // namespace ltps