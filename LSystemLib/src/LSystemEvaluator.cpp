#include "LSystemEvaluator.h"

#include "LSystem.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

using Bindings = std::unordered_map<std::string, double>;

constexpr double kEpsilon = 1e-9;

bool approx_equal(double a, double b)
{
    return std::fabs(a - b) <= kEpsilon;
}

ExprPtr clone_expr(const ExprPtr& expr)
{
    if (!expr)
    {
        return nullptr;
    }

    auto out = std::make_unique<Expr>();
    out->kind = expr->kind;
    out->number_value = expr->number_value;
    out->ident = expr->ident;
    out->left = clone_expr(expr->left);
    out->right = clone_expr(expr->right);
    return out;
}

Symbol clone_symbol(const Symbol& sym)
{
    Symbol out;
    out.kind = sym.kind;
    out.name = sym.name;
    out.subsystem = sym.subsystem;
    out.args.reserve(sym.args.size());
    for (const ExprPtr& arg : sym.args)
    {
        out.args.push_back(clone_expr(arg));
    }
    return out;
}

std::vector<Symbol> clone_generation(const std::vector<Symbol>& generation)
{
    std::vector<Symbol> out;
    out.reserve(generation.size());
    for (const Symbol& sym : generation)
    {
        out.push_back(clone_symbol(sym));
    }
    return out;
}

bool eval_expr(const Expr* expr, const Bindings& bindings, double& out_value)
{
    if (!expr)
    {
        return false;
    }

    switch (expr->kind)
    {
    case Expr::Kind::Number:
        out_value = expr->number_value;
        return true;
    case Expr::Kind::Ident:
    {
        const auto it = bindings.find(expr->ident);
        if (it == bindings.end())
        {
            return false;
        }
        out_value = it->second;
        return true;
    }
    case Expr::Kind::Add:
    case Expr::Kind::Sub:
    case Expr::Kind::Mul:
    case Expr::Kind::Div:
    {
        double lhs = 0.0;
        double rhs = 0.0;
        if (!eval_expr(expr->left.get(), bindings, lhs) || !eval_expr(expr->right.get(), bindings, rhs))
        {
            return false;
        }
        if (expr->kind == Expr::Kind::Add)
        {
            out_value = lhs + rhs;
            return true;
        }
        if (expr->kind == Expr::Kind::Sub)
        {
            out_value = lhs - rhs;
            return true;
        }
        if (expr->kind == Expr::Kind::Mul)
        {
            out_value = lhs * rhs;
            return true;
        }
        if (approx_equal(rhs, 0.0))
        {
            return false;
        }
        out_value = lhs / rhs;
        return true;
    }
    case Expr::Kind::Lt:
    case Expr::Kind::Lte:
    case Expr::Kind::Gt:
    case Expr::Kind::Gte:
    case Expr::Kind::Eq:
    {
        double lhs = 0.0;
        double rhs = 0.0;
        if (!eval_expr(expr->left.get(), bindings, lhs) || !eval_expr(expr->right.get(), bindings, rhs))
        {
            return false;
        }
        bool result = false;
        if (expr->kind == Expr::Kind::Lt)
        {
            result = lhs < rhs;
        }
        else if (expr->kind == Expr::Kind::Lte)
        {
            result = lhs <= rhs;
        }
        else if (expr->kind == Expr::Kind::Gt)
        {
            result = lhs > rhs;
        }
        else if (expr->kind == Expr::Kind::Gte)
        {
            result = lhs >= rhs;
        }
        else
        {
            result = approx_equal(lhs, rhs);
        }
        out_value = result ? 1.0 : 0.0;
        return true;
    }
    default:
        return false;
    }
}

bool match_pattern_expr(const Expr* pattern, double actual_value, Bindings& bindings)
{
    if (!pattern)
    {
        return false;
    }

    if (pattern->kind == Expr::Kind::Ident)
    {
        const auto it = bindings.find(pattern->ident);
        if (it == bindings.end())
        {
            bindings.emplace(pattern->ident, actual_value);
            return true;
        }
        return approx_equal(it->second, actual_value);
    }

    double expected_value = 0.0;
    if (!eval_expr(pattern, bindings, expected_value))
    {
        return false;
    }
    return approx_equal(expected_value, actual_value);
}

bool match_symbol(const Symbol& pattern, const Symbol& actual, Bindings& bindings)
{
    if (pattern.kind != actual.kind)
    {
        return false;
    }

    if (pattern.kind == SymbolKind::SubsystemRef)
    {
        return pattern.subsystem == actual.subsystem;
    }

    if (pattern.name != actual.name || pattern.args.size() != actual.args.size())
    {
        return false;
    }

    for (std::size_t arg_index = 0; arg_index < pattern.args.size(); ++arg_index)
    {
        double actual_arg_value = 0.0;
        if (!eval_expr(actual.args[arg_index].get(), bindings, actual_arg_value))
        {
            return false;
        }
        if (!match_pattern_expr(pattern.args[arg_index].get(), actual_arg_value, bindings))
        {
            return false;
        }
    }
    return true;
}

bool match_left_context(
    const std::vector<Symbol>& context,
    const std::vector<Symbol>& generation,
    std::size_t predecessor_index,
    Bindings& bindings)
{
    if (context.empty())
    {
        return true;
    }
    if (context.size() > predecessor_index)
    {
        return false;
    }

    const std::size_t start = predecessor_index - context.size();
    for (std::size_t i = 0; i < context.size(); ++i)
    {
        if (!match_symbol(context[i], generation[start + i], bindings))
        {
            return false;
        }
    }
    return true;
}

bool match_right_context(
    const std::vector<Symbol>& context,
    const std::vector<Symbol>& generation,
    std::size_t predecessor_index,
    Bindings& bindings)
{
    if (context.empty())
    {
        return true;
    }
    const std::size_t start = predecessor_index + 1;
    if (start + context.size() > generation.size())
    {
        return false;
    }

    for (std::size_t i = 0; i < context.size(); ++i)
    {
        if (!match_symbol(context[i], generation[start + i], bindings))
        {
            return false;
        }
    }
    return true;
}

bool rule_matches(const ParsedRule& rule,
    const std::vector<Symbol>& generation,
    std::size_t predecessor_index,
    const std::unordered_map<std::string, double>& constants,
    Bindings& out_bindings)
{
    Bindings bindings = constants;
    if (!match_left_context(rule.left_context, generation, predecessor_index, bindings))
    {
        return false;
    }
    if (!match_symbol(rule.predecessor, generation[predecessor_index], bindings))
    {
        return false;
    }
    if (!match_right_context(rule.right_context, generation, predecessor_index, bindings))
    {
        return false;
    }

    if (rule.condition)
    {
        double condition_value = 0.0;
        if (!eval_expr(rule.condition.get(), bindings, condition_value) || approx_equal(condition_value, 0.0))
        {
            return false;
        }
    }

    out_bindings = std::move(bindings);
    return true;
}

std::size_t choose_matched_rule(
    const std::vector<std::size_t>& matched_rule_indices,
    const std::vector<ParsedRule>& rules,
    std::mt19937_64& rng)
{
    if (matched_rule_indices.empty())
    {
        throw std::runtime_error("internal error: no matched rules to choose from");
    }

    std::vector<double> weights;
    weights.reserve(matched_rule_indices.size());
    double total_weight = 0.0;
    for (const std::size_t rule_index : matched_rule_indices)
    {
        const double p = rules[rule_index].probability;
        if (std::isfinite(p) && p > 0.0)
        {
            weights.push_back(p);
            total_weight += p;
        }
        else
        {
            weights.push_back(0.0);
        }
    }

    if (!(total_weight > 0.0))
    {
        return 0;
    }

    std::uniform_real_distribution<double> distribution(0.0, total_weight);
    const double selected = distribution(rng);
    double running = 0.0;
    for (std::size_t i = 0; i < matched_rule_indices.size(); ++i)
    {
        running += weights[i];
        if (selected <= running)
        {
            return i;
        }
    }
    return matched_rule_indices.size() - 1;
}

Symbol instantiate_successor_symbol(const Symbol& template_symbol, const Bindings& bindings)
{
    Symbol out;
    out.kind = template_symbol.kind;
    out.name = template_symbol.name;
    out.subsystem = template_symbol.subsystem;
    out.args.reserve(template_symbol.args.size());

    for (const ExprPtr& arg_expr : template_symbol.args)
    {
        double value = 0.0;
        if (eval_expr(arg_expr.get(), bindings, value))
        {
            auto numeric = std::make_unique<Expr>();
            numeric->kind = Expr::Kind::Number;
            numeric->number_value = value;
            out.args.push_back(std::move(numeric));
            continue;
        }

        if (template_symbol.name == "Mat"
            && arg_expr
            && arg_expr->kind == Expr::Kind::Ident)
        {
            out.args.push_back(clone_expr(arg_expr));
            continue;
        }

        throw std::runtime_error("unable to evaluate successor argument expression");
    }
    return out;
}

std::vector<Symbol> derive_generation(const std::vector<Symbol>& generation,
    const std::vector<ParsedRule>& rules,
    const std::unordered_map<std::string, double>& constants,
    std::mt19937_64& rng)
{
    std::vector<Symbol> next_generation;
    next_generation.reserve(generation.size());

    for (std::size_t symbol_index = 0; symbol_index < generation.size(); ++symbol_index)
    {
        std::vector<std::size_t> matched_rule_indices;
        std::vector<Bindings> matched_bindings;

        for (std::size_t rule_index = 0; rule_index < rules.size(); ++rule_index)
        {
            Bindings bindings;
            if (rule_matches(rules[rule_index], generation, symbol_index, constants, bindings))
            {
                matched_rule_indices.push_back(rule_index);
                matched_bindings.push_back(std::move(bindings));
            }
        }

        if (matched_rule_indices.empty())
        {
            next_generation.push_back(clone_symbol(generation[symbol_index]));
            continue;
        }

        const std::size_t selected_match = choose_matched_rule(matched_rule_indices, rules, rng);
        const ParsedRule& selected_rule = rules[matched_rule_indices[selected_match]];
        const Bindings& selected_bindings = matched_bindings[selected_match];

        for (const Symbol& successor_symbol : selected_rule.successor)
        {
            next_generation.push_back(instantiate_successor_symbol(successor_symbol, selected_bindings));
        }
    }

    return next_generation;
}

} // namespace

LSystemEvaluator::StepHistory LSystemEvaluator::evaluate_steps(
    const LSystem& system,
    std::size_t iterations,
    std::uint64_t seed)
{
    StepHistory steps;
    steps.reserve(iterations + 1);

    std::mt19937_64 rng(seed);
    std::vector<Symbol> generation = clone_generation(system.axiom_modules());
    steps.push_back(clone_generation(generation));

    for (std::size_t iteration = 0; iteration < iterations; ++iteration)
    {
        generation = derive_generation(generation, system.rules(), system.constants(), rng);
        steps.push_back(clone_generation(generation));
    }

    return steps;
}

LSystemEvaluationResult LSystemEvaluator::evaluate(
    const std::string& definition,
    const std::size_t iterations,
    const std::uint64_t seed)
{
    LSystem system;
    system.parse(definition);

    LSystemEvaluationResult result;
    result.materials = system.material_definitions();

    StepHistory steps = evaluate_steps(system, iterations, seed);
    if (!steps.empty())
    {
        result.generation = std::move(steps.back());
    }

    return result;
}
