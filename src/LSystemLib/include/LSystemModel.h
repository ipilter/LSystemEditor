#pragma once

#include <memory>
#include <string>
#include <vector>

struct Expr
{
    enum class Kind
    {
        Add,
        Sub,
        Mul,
        Div,
        Lt,
        Lte,
        Gt,
        Gte,
        Eq,
        Number,
        Ident
    };

    Kind kind{};
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    double number_value = 0.0;
    std::string ident;
};

using ExprPtr = std::unique_ptr<Expr>;

enum class SymbolKind
{
    Module,
    SubsystemRef
};

struct Symbol
{
    SymbolKind kind = SymbolKind::Module;
    std::string name;
    std::string subsystem;
    std::vector<ExprPtr> args;
};

struct ParsedRule
{
    std::vector<Symbol> left_context;
    Symbol predecessor;
    std::vector<Symbol> right_context;
    ExprPtr condition;
    double probability = 1.0;
    std::vector<Symbol> successor;
};
