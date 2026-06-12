#pragma once

#include "ModuleStringParser.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lsys::detail
{

enum class TokenKind
{
    Eof,
    Ident,
    Number,
    LParen,
    RParen,
    Comma,
    Plus,
    Minus,
    Star,
    Slash,
    Lt,
    Lte,
    Gt,
    Gte,
    Eq,
    Colon,
    Arrow,
    LBracket,
    RBracket
};

struct Token
{
    TokenKind kind = TokenKind::Eof;
    std::size_t begin = 0;
    std::string_view text;
    double number_value = 0.0;
};

bool is_ident_start(char c);
bool is_ident_char(char c);
std::pair<std::size_t, std::size_t> line_column(std::string_view sv, std::size_t index);
[[noreturn]] void parse_error(std::string_view sv, std::size_t at, const char* message);

struct Lexer
{
    std::string_view sv;
    std::size_t pos = 0;
    Token tok{};

    explicit Lexer(std::string_view in);
    void skip_ws();
    void bump();
    void expect(TokenKind k, const char* message);
};

class Parser
{
public:
    explicit Parser(std::string_view input);
    std::vector<Symbol> parse_string();
    ParsedRule parse_rule();

private:
    Lexer lex;

    Symbol parse_module(bool allow_subsystem_ref);
    std::vector<ExprPtr> parse_expr_list();
    ExprPtr parse_expr();
    ExprPtr parse_comparison();
    ExprPtr parse_additive();
    ExprPtr parse_term();
    ExprPtr parse_factor();
};

} // namespace lsys::detail
