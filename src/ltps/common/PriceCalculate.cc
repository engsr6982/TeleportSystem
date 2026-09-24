#include "ltps/common/PriceCalculate.h"
#include <random>

#pragma warning(disable : 4702)
#include "exprtk.hpp"
#pragma warning(default : 4702)

namespace ltps {


PriceCalculate::PriceCalculate(std::string expression, InternalFuncOptions options)
: mVariables{},
  mExpression(std::move(expression)),
  mOptions(options) {}
PriceCalculate::PriceCalculate(std::string expression, Impl variables, InternalFuncOptions options)
: mVariables{std::move(variables)},
  mExpression(std::move(expression)),
  mOptions(options) {}

PriceCalculate::PriceCalculate(PriceCalculate&&) noexcept            = default;
PriceCalculate& PriceCalculate::operator=(PriceCalculate&&) noexcept = default;


PriceCalculate& PriceCalculate::addVariable(std::string name, double value) {
    mVariables[std::move(name)] = value;
    return *this;
}

std::optional<double> PriceCalculate::getVariable(std::string const& name) const {
    auto it = mVariables.find(name);
    if (it != mVariables.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::string PriceCalculate::getExpression() const { return mExpression; }

void PriceCalculate::setExpression(std::string expression) { mExpression = std::move(expression); }

PriceCalculate::Impl&       PriceCalculate::getVariables() { return mVariables; }
PriceCalculate::Impl const& PriceCalculate::getVariables() const { return mVariables; }

void PriceCalculate::setOptions(InternalFuncOptions options) { mOptions = options; }


inline bool hasOption(PriceCalculate::InternalFuncOptions options, PriceCalculate::InternalFuncOptions targetOption) {
    return static_cast<int>(options) & static_cast<int>(targetOption);
}

inline void
parseInternalFuncOptions(exprtk::symbol_table<double>& symbolTable, PriceCalculate::InternalFuncOptions options) {
    if (hasOption(options, PriceCalculate::InternalFuncOptions::RandomNum)) {
        symbolTable.add_function("random_num", &internals::random_num);
    }
    if (hasOption(options, PriceCalculate::InternalFuncOptions::RandomNumRange)) {
        symbolTable.add_function("random_num_range", &internals::random_num_range);
    }
}

Result<double> PriceCalculate::eval() const {
    exprtk::symbol_table<double> symbolTable;

    parseInternalFuncOptions(symbolTable, mOptions);

    auto variables = mVariables;
    for (auto& [name, value] : variables) {
        symbolTable.add_variable(name, value);
    }

    exprtk::expression<double> expression;
    expression.register_symbol_table(symbolTable);

    exprtk::parser<double> parser;
    if (!parser.compile(mExpression, expression)) {
        return ll::makeStringError(parser.error());
    }
    return expression.value();
}


namespace internals {

double random_num() {
    static std::random_device               rd;
    static std::mt19937                     gen(rd());
    static std::uniform_real_distribution<> dis(0.0, 1.0);
    return dis(gen);
}

double random_num_range(double min, double max) {
    static std::random_device        rd;
    static std::mt19937              gen(rd());
    std::uniform_real_distribution<> dis(min, max);
    return dis(gen);
}

} // namespace internals

} // namespace ltps