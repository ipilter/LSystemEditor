#include "ModuleStringParser.h"
#include "ModuleStringParserInternal.h"

#include "LSystemLineParse.h"

#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

std::string_view trim_rule_line(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
    {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    {
        s.remove_suffix(1);
    }
    return s;
}

std::string_view strip_hash_comment(std::string_view raw)
{
    const std::size_t hash = raw.find('#');
    if (hash == std::string_view::npos)
    {
        return raw;
    }
    return raw.substr(0, hash);
}

bool raw_line_looks_like_rule_continuation(std::string_view raw)
{
    return !raw.empty() && (raw.front() == ' ' || raw.front() == '\t');
}

} // namespace

namespace lsys::detail
{

namespace
{

bool is_subsystem_ref_token(std::string_view mod)
{
    return mod == "ref" || (mod.size() >= 4 && mod.compare(0, 4, "ref.") == 0);
}

bool subsystem_ref_target_ok(const std::string& target)
{
    if (target.empty() || target.front() == '.')
    {
        return false;
    }
    return target.find("..") == std::string::npos;
}

// Mantissa: digit-led (`12`, `12.34`) or fractional-only (`.5`). `pos` must point at
// the first body character. On success advances `pos` and sets `saw_dot`.
bool consume_number_mantissa(std::string_view sv, std::size_t& pos, bool& saw_dot)
{
    saw_dot = false;
    if (pos >= sv.size())
    {
        return false;
    }
    if (sv[pos] == '.')
    {
        if (pos + 1 >= sv.size() || !std::isdigit(static_cast<unsigned char>(sv[pos + 1])))
        {
            return false;
        }
        saw_dot = true;
        ++pos;
        while (pos < sv.size() && std::isdigit(static_cast<unsigned char>(sv[pos])))
        {
            ++pos;
        }
        return true;
    }
    if (!std::isdigit(static_cast<unsigned char>(sv[pos])))
    {
        return false;
    }
    while (pos < sv.size())
    {
        const char d = sv[pos];
        if (std::isdigit(static_cast<unsigned char>(d)))
        {
            ++pos;
            continue;
        }
        if (d == '.' && !saw_dot)
        {
            saw_dot = true;
            ++pos;
            continue;
        }
        break;
    }
    return true;
}

// True when '-' at `minus_pos` begins a signed numeric literal (unary position), not binary '-'.
bool minus_starts_signed_literal(std::string_view sv, std::size_t minus_pos)
{
    if (minus_pos == 0)
    {
        return true;
    }
    std::size_t p = minus_pos;
    while (p > 0 && std::isspace(static_cast<unsigned char>(sv[p - 1])))
    {
        --p;
    }
    if (p == 0)
    {
        return true;
    }
    const char prev = sv[p - 1];
    if (prev == '(' || prev == '[' || prev == ',' || prev == ':')
    {
        return true;
    }
    if (prev == '+' || prev == '-' || prev == '*' || prev == '/')
    {
        return true;
    }
    if (prev == '<' || prev == '>' || prev == '=')
    {
        return true;
    }
    return false;
}

void validate_emit_number(std::string_view sv, std::size_t start, std::size_t end, bool saw_dot, Token& tok)
{
    const std::string_view slice = sv.substr(start, end - start);
    if (slice.empty() || slice == ".")
    {
        parse_error(sv, start, "invalid number");
    }
    if (saw_dot)
    {
        const std::size_t dot = slice.find('.');
        if (dot == std::string_view::npos || dot + 1 >= slice.size())
        {
            parse_error(sv, start, "invalid number");
        }
    }
    try
    {
        const double v = std::stod(std::string(slice));
        tok = Token{TokenKind::Number, start, slice, v};
    }
    catch (const std::exception&)
    {
        parse_error(sv, start, "invalid number");
    }
}

} // namespace

bool is_ident_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_ident_char(char c)
{
    return is_ident_start(c) || (c >= '0' && c <= '9') || c == '_' || c == '.';
}

std::pair<std::size_t, std::size_t> line_column(std::string_view sv, std::size_t index)
{
    std::size_t line = 1;
    std::size_t col = 1;
    for (std::size_t i = 0; i < index && i < sv.size(); ++i)
    {
        if (sv[i] == '\n')
        {
            ++line;
            col = 1;
        }
        else
        {
            ++col;
        }
    }
    return {line, col};
}

void parse_error(std::string_view sv, std::size_t at, const char* message)
{
    const auto [line, col] = line_column(sv, at);
    throw std::runtime_error(
        std::string(message) + " at line " + std::to_string(line) + " column " + std::to_string(col));
}

Lexer::Lexer(std::string_view in)
    : sv(in)
{
    skip_ws();
    bump();
}

void Lexer::skip_ws()
{
    while (pos < sv.size() && std::isspace(static_cast<unsigned char>(sv[pos])))
    {
        ++pos;
    }
}

void Lexer::bump()
{
    skip_ws();
    if (pos >= sv.size())
    {
        tok = Token{TokenKind::Eof, pos, {}, 0.0};
        return;
    }

    const std::size_t start = pos;
    const char c = sv[pos];

    if (c == '(')
    {
        ++pos;
        tok = Token{TokenKind::LParen, start, sv.substr(start, 1), 0.0};
        return;
    }
    if (c == ')')
    {
        ++pos;
        tok = Token{TokenKind::RParen, start, sv.substr(start, 1), 0.0};
        return;
    }
    if (c == '[')
    {
        ++pos;
        tok = Token{TokenKind::LBracket, start, sv.substr(start, 1), 0.0};
        return;
    }
    if (c == ']')
    {
        ++pos;
        tok = Token{TokenKind::RBracket, start, sv.substr(start, 1), 0.0};
        return;
    }
    if (c == ',')
    {
        ++pos;
        tok = Token{TokenKind::Comma, start, sv.substr(start, 1), 0.0};
        return;
    }
    if (c == '^' || c == '&')
    {
        ++pos;
        tok = Token{TokenKind::Ident, start, sv.substr(start, 1), 0.0};
        return;
    }
    if (c == '+')
    {
        ++pos;
        tok = Token{TokenKind::Plus, start, sv.substr(start, 1), 0.0};
        return;
    }
    if (c == '-' && pos + 1 < sv.size() && sv[pos + 1] == '>')
    {
        pos += 2;
        tok = Token{TokenKind::Arrow, start, sv.substr(start, 2), 0.0};
        return;
    }
    if (c == '-')
    {
        if (pos + 1 < sv.size() && minus_starts_signed_literal(sv, pos))
        {
            std::size_t body = pos + 1;
            bool saw_dot = false;
            if (consume_number_mantissa(sv, body, saw_dot))
            {
                const std::size_t num_start = pos;
                pos = body;
                validate_emit_number(sv, num_start, pos, saw_dot, tok);
                return;
            }
        }
        ++pos;
        tok = Token{TokenKind::Minus, start, sv.substr(start, 1), 0.0};
        return;
    }
    if (c == '*')
    {
        ++pos;
        tok = Token{TokenKind::Star, start, sv.substr(start, 1), 0.0};
        return;
    }
    if (c == '/')
    {
        ++pos;
        tok = Token{TokenKind::Slash, start, sv.substr(start, 1), 0.0};
        return;
    }
    if (c == '<')
    {
        if (pos + 1 < sv.size() && sv[pos + 1] == '=')
        {
            pos += 2;
            tok = Token{TokenKind::Lte, start, sv.substr(start, 2), 0.0};
            return;
        }
        ++pos;
        tok = Token{TokenKind::Lt, start, sv.substr(start, 1), 0.0};
        return;
    }
    if (c == '>')
    {
        if (pos + 1 < sv.size() && sv[pos + 1] == '=')
        {
            pos += 2;
            tok = Token{TokenKind::Gte, start, sv.substr(start, 2), 0.0};
            return;
        }
        ++pos;
        tok = Token{TokenKind::Gt, start, sv.substr(start, 1), 0.0};
        return;
    }
    if (c == '=')
    {
        if (pos + 1 < sv.size() && sv[pos + 1] == '=')
        {
            pos += 2;
            tok = Token{TokenKind::Eq, start, sv.substr(start, 2), 0.0};
            return;
        }
        ++pos;
        tok = Token{TokenKind::Eq, start, sv.substr(start, 1), 0.0};
        return;
    }
    if (c == ':')
    {
        ++pos;
        tok = Token{TokenKind::Colon, start, sv.substr(start, 1), 0.0};
        return;
    }

    if (std::isdigit(static_cast<unsigned char>(c)))
    {
        bool saw_dot = false;
        std::size_t body = pos;
        if (!consume_number_mantissa(sv, body, saw_dot))
        {
            parse_error(sv, start, "invalid number");
        }
        pos = body;
        validate_emit_number(sv, start, pos, saw_dot, tok);
        return;
    }

    if (is_ident_start(c))
    {
        ++pos;
        while (pos < sv.size() && is_ident_char(sv[pos]))
        {
            ++pos;
        }
        tok = Token{TokenKind::Ident, start, sv.substr(start, pos - start), 0.0};
        return;
    }

    parse_error(sv, start, "unexpected character");
}

void Lexer::expect(TokenKind k, const char* message)
{
    if (tok.kind != k)
    {
        parse_error(sv, tok.begin, message);
    }
    bump();
}

Parser::Parser(std::string_view input)
    : lex(input)
{
}

std::vector<Symbol> Parser::parse_string()
{
    std::vector<Symbol> out;
    while (lex.tok.kind != TokenKind::Eof)
    {
        if (lex.tok.kind == TokenKind::LBracket || lex.tok.kind == TokenKind::RBracket)
        {
            Symbol sym;
            sym.kind = SymbolKind::Module;
            sym.name = std::string(lex.tok.text);
            lex.bump();
            out.push_back(std::move(sym));
        }
        else
        {
            out.push_back(parse_module(true));
        }
    }
    return out;
}

Symbol Parser::parse_module(bool allow_subsystem_ref)
{
    if (lex.tok.kind != TokenKind::Ident)
    {
        parse_error(lex.sv, lex.tok.begin, "expected identifier (module name)");
    }
    const std::size_t ident_begin = lex.tok.begin;
    const std::string mod = std::string(lex.tok.text);
    lex.bump();

    if (is_subsystem_ref_token(mod))
    {
        if (!allow_subsystem_ref)
        {
            parse_error(lex.sv, ident_begin, "subsystem reference is not allowed here");
        }
        if (mod == "ref")
        {
            parse_error(lex.sv, ident_begin, "invalid subsystem reference (expected ref.Name)");
        }
        if (mod.size() == 4)
        {
            parse_error(lex.sv, ident_begin, "invalid subsystem reference (empty name)");
        }
        std::string target = mod.substr(4);
        if (!subsystem_ref_target_ok(target))
        {
            parse_error(lex.sv, ident_begin, "invalid subsystem reference");
        }
        if (lex.tok.kind == TokenKind::LParen)
        {
            parse_error(lex.sv, lex.tok.begin, "subsystem reference does not support arguments");
        }
        Symbol sym;
        sym.kind = SymbolKind::SubsystemRef;
        sym.subsystem = std::move(target);
        return sym;
    }

    Symbol sym;
    sym.kind = SymbolKind::Module;
    sym.name = std::move(mod);

    if (lex.tok.kind == TokenKind::LParen)
    {
        lex.bump();
        sym.args = parse_expr_list();
        lex.expect(TokenKind::RParen, "expected ')' after argument list");
    }
    return sym;
}

std::vector<ExprPtr> Parser::parse_expr_list()
{
    std::vector<ExprPtr> args;
    if (lex.tok.kind == TokenKind::RParen)
    {
        return args;
    }
    for (;;)
    {
        args.push_back(parse_expr());
        if (lex.tok.kind != TokenKind::Comma)
        {
            break;
        }
        lex.bump();
    }
    return args;
}

ExprPtr Parser::parse_expr()
{
    return parse_comparison();
}

ExprPtr Parser::parse_comparison()
{
    ExprPtr node = parse_additive();
    if (lex.tok.kind == TokenKind::Lt || lex.tok.kind == TokenKind::Lte ||
        lex.tok.kind == TokenKind::Gt || lex.tok.kind == TokenKind::Gte ||
        lex.tok.kind == TokenKind::Eq)
    {
        Expr::Kind kind;
        switch (lex.tok.kind)
        {
        case TokenKind::Lt:  kind = Expr::Kind::Lt;  break;
        case TokenKind::Lte: kind = Expr::Kind::Lte; break;
        case TokenKind::Gt:  kind = Expr::Kind::Gt;  break;
        case TokenKind::Gte: kind = Expr::Kind::Gte; break;
        default:             kind = Expr::Kind::Eq;   break;
        }
        lex.bump();
        auto out = std::make_unique<Expr>();
        out->kind = kind;
        out->left = std::move(node);
        out->right = parse_additive();
        node = std::move(out);
    }
    return node;
}

ExprPtr Parser::parse_additive()
{
    ExprPtr node = parse_term();
    while (lex.tok.kind == TokenKind::Plus || lex.tok.kind == TokenKind::Minus)
    {
        const TokenKind op = lex.tok.kind;
        lex.bump();
        ExprPtr rhs = parse_term();
        auto out = std::make_unique<Expr>();
        out->kind = (op == TokenKind::Plus) ? Expr::Kind::Add : Expr::Kind::Sub;
        out->left = std::move(node);
        out->right = std::move(rhs);
        node = std::move(out);
    }
    return node;
}

ExprPtr Parser::parse_term()
{
    ExprPtr node = parse_factor();
    while (lex.tok.kind == TokenKind::Star || lex.tok.kind == TokenKind::Slash)
    {
        const TokenKind op = lex.tok.kind;
        lex.bump();
        ExprPtr rhs = parse_factor();
        auto out = std::make_unique<Expr>();
        out->kind = (op == TokenKind::Star) ? Expr::Kind::Mul : Expr::Kind::Div;
        out->left = std::move(node);
        out->right = std::move(rhs);
        node = std::move(out);
    }
    return node;
}

ExprPtr Parser::parse_factor()
{
    if (lex.tok.kind == TokenKind::Number)
    {
        auto n = std::make_unique<Expr>();
        n->kind = Expr::Kind::Number;
        n->number_value = lex.tok.number_value;
        lex.bump();
        return n;
    }
    if (lex.tok.kind == TokenKind::Ident)
    {
        if (is_subsystem_ref_token(lex.tok.text))
        {
            parse_error(lex.sv, lex.tok.begin, "subsystem reference is not allowed in expressions");
        }
        auto n = std::make_unique<Expr>();
        n->kind = Expr::Kind::Ident;
        n->ident = std::string(lex.tok.text);
        lex.bump();
        return n;
    }
    if (lex.tok.kind == TokenKind::LParen)
    {
        lex.bump();
        ExprPtr inner = parse_expr();
        lex.expect(TokenKind::RParen, "expected ')' after expression");
        return inner;
    }
    parse_error(lex.sv, lex.tok.begin, "expected number, identifier, or '('");
}

ParsedRule Parser::parse_rule()
{
    ParsedRule rule;
    std::vector<Symbol> leading;
    while (lex.tok.kind == TokenKind::Ident)
    {
        leading.push_back(parse_module(false));
    }

    if (lex.tok.kind == TokenKind::Lt)
    {
        rule.left_context = std::move(leading);
        lex.bump();
        rule.predecessor = parse_module(false);
        if (lex.tok.kind == TokenKind::Gt)
        {
            lex.bump();
            while (lex.tok.kind == TokenKind::Ident)
            {
                rule.right_context.push_back(parse_module(false));
            }
        }
    }
    else if (lex.tok.kind == TokenKind::Arrow || lex.tok.kind == TokenKind::Colon)
    {
        if (leading.empty())
        {
            parse_error(lex.sv, lex.tok.begin, "expected predecessor module");
        }
        if (leading.size() > 1)
        {
            parse_error(lex.sv, lex.tok.begin,
                "ambiguous rule: use '<' to separate context from predecessor");
        }
        rule.predecessor = std::move(leading[0]);
    }
    else
    {
        parse_error(lex.sv, lex.tok.begin,
            leading.empty()
                ? "expected module name or '<'"
                : "expected '<', ':', or '->' after module(s)");
    }

    if (lex.tok.kind == TokenKind::Arrow)
    {
        lex.bump();
        rule.condition = nullptr;
        rule.probability = 1.0;
    }
    else if (lex.tok.kind == TokenKind::Colon)
    {
        lex.bump();
        if (lex.tok.kind == TokenKind::Colon)
        {
            lex.bump();
            if (lex.tok.kind != TokenKind::Number)
            {
                parse_error(lex.sv, lex.tok.begin, "expected probability after '::'");
            }
            rule.condition = nullptr;
            rule.probability = lex.tok.number_value;
            lex.bump();
        }
        else if (lex.tok.kind == TokenKind::Arrow)
        {
            rule.condition = nullptr;
            rule.probability = 1.0;
        }
        else
        {
            rule.condition = parse_expr();
            rule.probability = 1.0;
            if (lex.tok.kind == TokenKind::Colon)
            {
                lex.bump();
                if (lex.tok.kind != TokenKind::Number)
                {
                    parse_error(lex.sv, lex.tok.begin, "expected probability after second ':'");
                }
                rule.probability = lex.tok.number_value;
                lex.bump();
            }
        }
        lex.expect(TokenKind::Arrow, "expected '->' before successor");
    }
    else
    {
        parse_error(lex.sv, lex.tok.begin, "expected ':', or '->' after context");
    }
    while (lex.tok.kind != TokenKind::Eof)
    {
        if (lex.tok.kind == TokenKind::LBracket || lex.tok.kind == TokenKind::RBracket)
        {
            Symbol sym;
            sym.kind = SymbolKind::Module;
            sym.name = std::string(lex.tok.text);
            lex.bump();
            rule.successor.push_back(std::move(sym));
        }
        else
        {
            rule.successor.push_back(parse_module(true));
        }
    }
    return rule;
}

} // namespace lsys::detail

std::vector<Symbol> ModuleStringParser::parse(std::string_view input)
{
    lsys::detail::Parser p(input);
    return p.parse_string();
}

ParsedRule ModuleStringParser::parse_rule(std::string_view input)
{
    lsys::detail::Parser p(input);
    return p.parse_rule();
}

std::vector<ParsedRule> ModuleStringParser::parse_rules_block(std::string_view rules_text)
{
    std::vector<ParsedRule> rules;
    std::string buffer;
    auto flush = [&]() {
        if (!buffer.empty())
        {
            rules.push_back(parse_rule(buffer));
            buffer.clear();
        }
    };

    std::size_t line_start = 0;
    for (;;)
    {
        const std::size_t nl = rules_text.find('\n', line_start);
        const std::string_view raw =
            (nl == std::string_view::npos) ? rules_text.substr(line_start)
                                           : rules_text.substr(line_start, nl - line_start);
        if (is_hash_comment_line(raw))
        {
            if (nl == std::string_view::npos)
            {
                break;
            }
            line_start = nl + 1;
            continue;
        }
        const std::string_view trimmed = trim_rule_line(strip_hash_comment(raw));

        if (trimmed.empty())
        {
            if (!buffer.empty())
            {
                if (nl == std::string_view::npos)
                {
                    break;
                }
                line_start = nl + 1;
                continue;
            }
            flush();
        }
        else if (raw_line_looks_like_rule_continuation(raw) && !buffer.empty())
        {
            buffer.push_back(' ');
            buffer.append(trimmed.data(), trimmed.size());
        }
        else
        {
            flush();
            buffer.assign(trimmed.begin(), trimmed.end());
        }

        if (nl == std::string_view::npos)
        {
            break;
        }
        line_start = nl + 1;
    }
    flush();
    return rules;
}
