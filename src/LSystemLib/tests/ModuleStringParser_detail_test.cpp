#include "ModuleStringParser.h"
#include "ModuleStringParserInternal.h"
#include "LSystemModel.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

using lsys::detail::Lexer;
using lsys::detail::Parser;
using lsys::detail::TokenKind;
using lsys::detail::is_ident_char;
using lsys::detail::is_ident_start;
using lsys::detail::line_column;

TEST(ModuleStringParserDetail, LineColumnStart)
{
    const auto [line, col] = line_column("hello", 0);
    EXPECT_EQ(line, 1u);
    EXPECT_EQ(col, 1u);
}

TEST(ModuleStringParserDetail, LineColumnAfterNewline)
{
    const std::string_view s = "a\nb";
    const auto p0 = line_column(s, 0);
    EXPECT_EQ(p0.first, 1u);
    EXPECT_EQ(p0.second, 1u);
    const auto p2 = line_column(s, 2);
    EXPECT_EQ(p2.first, 2u);
    EXPECT_EQ(p2.second, 1u);
}

TEST(ModuleStringParserDetail, IdentPredicates)
{
    EXPECT_TRUE(is_ident_start('a'));
    EXPECT_TRUE(is_ident_start('Z'));
    EXPECT_FALSE(is_ident_start('0'));
    EXPECT_TRUE(is_ident_char('_'));
    EXPECT_TRUE(is_ident_char('9'));
    EXPECT_TRUE(is_ident_char('.'));
}

TEST(ModuleStringParserDetail, LexerSkipsLeadingWhitespace)
{
    Lexer lex("  Foo");
    ASSERT_EQ(lex.tok.kind, TokenKind::Ident);
    EXPECT_EQ(lex.tok.text, "Foo");
}

TEST(ModuleStringParserDetail, LexerNumber)
{
    Lexer lex("3.14");
    ASSERT_EQ(lex.tok.kind, TokenKind::Number);
    EXPECT_DOUBLE_EQ(lex.tok.number_value, 3.14);
}

TEST(ModuleStringParserDetail, LexerNegativeAndFloatLiterals)
{
    {
        Lexer lex("-90");
        ASSERT_EQ(lex.tok.kind, TokenKind::Number);
        EXPECT_DOUBLE_EQ(lex.tok.number_value, -90.0);
    }
    {
        Lexer lex("90.1");
        ASSERT_EQ(lex.tok.kind, TokenKind::Number);
        EXPECT_DOUBLE_EQ(lex.tok.number_value, 90.1);
    }
    {
        Lexer lex("-99.9");
        ASSERT_EQ(lex.tok.kind, TokenKind::Number);
        EXPECT_DOUBLE_EQ(lex.tok.number_value, -99.9);
    }
    {
        Lexer lex("-.5");
        ASSERT_EQ(lex.tok.kind, TokenKind::Number);
        EXPECT_DOUBLE_EQ(lex.tok.number_value, -0.5);
    }
}

TEST(ModuleStringParserDetail, LexerSubtractionNotSingleNumber)
{
    Lexer lex("3-2");
    ASSERT_EQ(lex.tok.kind, TokenKind::Number);
    EXPECT_DOUBLE_EQ(lex.tok.number_value, 3.0);
    lex.bump();
    ASSERT_EQ(lex.tok.kind, TokenKind::Minus);
    lex.bump();
    ASSERT_EQ(lex.tok.kind, TokenKind::Number);
    EXPECT_DOUBLE_EQ(lex.tok.number_value, 2.0);
}

TEST(ModuleStringParserDetail, ParsePitchNegativeAndF)
{
    const auto syms = ModuleStringParser::parse("Pitch(-90)F(3,0.5)");
    ASSERT_EQ(syms.size(), 2u);
    EXPECT_EQ(syms[0].name, "Pitch");
    ASSERT_EQ(syms[0].args.size(), 1u);
    ASSERT_TRUE(syms[0].args[0]);
    EXPECT_EQ(syms[0].args[0]->kind, Expr::Kind::Number);
    EXPECT_DOUBLE_EQ(syms[0].args[0]->number_value, -90.0);
    EXPECT_EQ(syms[1].name, "F");
    ASSERT_EQ(syms[1].args.size(), 2u);
    ASSERT_TRUE(syms[1].args[0]);
    ASSERT_TRUE(syms[1].args[1]);
    EXPECT_EQ(syms[1].args[0]->kind, Expr::Kind::Number);
    EXPECT_DOUBLE_EQ(syms[1].args[0]->number_value, 3.0);
    EXPECT_EQ(syms[1].args[1]->kind, Expr::Kind::Number);
    EXPECT_DOUBLE_EQ(syms[1].args[1]->number_value, 0.5);
}

TEST(ModuleStringParserDetail, ParseErrorIncludesLineColumn)
{
    try
    {
        Lexer lex("@");
        FAIL() << "expected exception";
    }
    catch (const std::runtime_error& e)
    {
        EXPECT_NE(std::string_view(e.what()).find("line 1"), std::string_view::npos);
        EXPECT_NE(std::string_view(e.what()).find("column"), std::string_view::npos);
    }
}

TEST(ModuleStringParserDetail, ParserMatchesPublicParse)
{
    const std::string_view input = "A(x+1)";
    const auto from_detail = Parser(input).parse_string();
    const auto from_public = ModuleStringParser::parse(input);
    ASSERT_EQ(from_detail.size(), from_public.size());
    ASSERT_EQ(from_detail.size(), 1u);
    EXPECT_EQ(from_detail[0].name, from_public[0].name);
    ASSERT_EQ(from_detail[0].args.size(), from_public[0].args.size());
    ASSERT_TRUE(from_detail[0].args[0]);
    ASSERT_TRUE(from_public[0].args[0]);
    EXPECT_EQ(from_detail[0].args[0]->kind, from_public[0].args[0]->kind);
}

TEST(ModuleStringParserDetail, ParseRuleLeftContextOnly)
{
    const auto rule = ModuleStringParser::parse_rule("L < P -> S");
    ASSERT_EQ(rule.left_context.size(), 1u);
    EXPECT_EQ(rule.left_context[0].name, "L");
    EXPECT_TRUE(rule.right_context.empty());
    EXPECT_EQ(rule.predecessor.name, "P");
    ASSERT_EQ(rule.successor.size(), 1u);
    EXPECT_EQ(rule.successor[0].name, "S");
}

TEST(ModuleStringParserDetail, ParseRuleRightContextOnly)
{
    const auto rule = ModuleStringParser::parse_rule("< P > R -> S");
    EXPECT_TRUE(rule.left_context.empty());
    EXPECT_EQ(rule.predecessor.name, "P");
    ASSERT_EQ(rule.right_context.size(), 1u);
    EXPECT_EQ(rule.right_context[0].name, "R");
    ASSERT_EQ(rule.successor.size(), 1u);
    EXPECT_EQ(rule.successor[0].name, "S");
}

TEST(ModuleStringParserDetail, ParseRuleBothContexts)
{
    const auto rule = ModuleStringParser::parse_rule("L < P > R -> S");
    ASSERT_EQ(rule.left_context.size(), 1u);
    EXPECT_EQ(rule.left_context[0].name, "L");
    EXPECT_EQ(rule.predecessor.name, "P");
    ASSERT_EQ(rule.right_context.size(), 1u);
    EXPECT_EQ(rule.right_context[0].name, "R");
    ASSERT_EQ(rule.successor.size(), 1u);
    EXPECT_EQ(rule.successor[0].name, "S");
}

TEST(ModuleStringParserDetail, ParseRuleNoConditionMeansAlwaysTrue)
{
    const auto arrow_only = ModuleStringParser::parse_rule("A < B -> C");
    EXPECT_FALSE(arrow_only.condition);
    EXPECT_DOUBLE_EQ(arrow_only.probability, 1.0);

    const auto colon_arrow = ModuleStringParser::parse_rule("A < B : -> C");
    EXPECT_FALSE(colon_arrow.condition);
    EXPECT_DOUBLE_EQ(colon_arrow.probability, 1.0);
}

TEST(ModuleStringParserDetail, ParseRuleConditionWhenPresent)
{
    const auto rule = ModuleStringParser::parse_rule("A < B > C : n + 1 -> D");
    ASSERT_TRUE(rule.condition);
    EXPECT_EQ(rule.condition->kind, Expr::Kind::Add);
    EXPECT_DOUBLE_EQ(rule.probability, 1.0);
}

TEST(ModuleStringParserDetail, ParseRuleCondition_EqualitySingleEquals)
{
    const auto rule = ModuleStringParser::parse_rule("A(d) : d = 0 -> B");
    ASSERT_TRUE(rule.condition);
    EXPECT_EQ(rule.condition->kind, Expr::Kind::Eq);
    ASSERT_TRUE(rule.condition->left);
    EXPECT_EQ(rule.condition->left->kind, Expr::Kind::Ident);
    EXPECT_EQ(rule.condition->left->ident, "d");
    ASSERT_TRUE(rule.condition->right);
    EXPECT_EQ(rule.condition->right->kind, Expr::Kind::Number);
    EXPECT_DOUBLE_EQ(rule.condition->right->number_value, 0.0);
}

TEST(ModuleStringParserDetail, ParseRuleCondition_EqualityDoubleEquals)
{
    const auto rule = ModuleStringParser::parse_rule("A(d) : d == 0 -> B");
    ASSERT_TRUE(rule.condition);
    EXPECT_EQ(rule.condition->kind, Expr::Kind::Eq);
    ASSERT_TRUE(rule.condition->left);
    EXPECT_EQ(rule.condition->left->kind, Expr::Kind::Ident);
    EXPECT_EQ(rule.condition->left->ident, "d");
    ASSERT_TRUE(rule.condition->right);
    EXPECT_EQ(rule.condition->right->kind, Expr::Kind::Number);
    EXPECT_DOUBLE_EQ(rule.condition->right->number_value, 0.0);
}

TEST(ModuleStringParserDetail, ParseRuleDefaultProbabilityOne)
{
    const auto rule = ModuleStringParser::parse_rule("X < Y > Z : flag -> W");
    ASSERT_TRUE(rule.condition);
    EXPECT_EQ(rule.condition->ident, "flag");
    EXPECT_DOUBLE_EQ(rule.probability, 1.0);
}

TEST(ModuleStringParserDetail, ParseRuleExplicitProbability)
{
    const auto rule = ModuleStringParser::parse_rule("A < B : ok : 0.25 -> C");
    ASSERT_TRUE(rule.condition);
    EXPECT_EQ(rule.condition->ident, "ok");
    EXPECT_DOUBLE_EQ(rule.probability, 0.25);
}

TEST(ModuleStringParserDetail, ParseRuleProbabilityWithoutCondition)
{
    const auto rule = ModuleStringParser::parse_rule("A < B > :: 0.5 -> C");
    EXPECT_FALSE(rule.condition);
    EXPECT_DOUBLE_EQ(rule.probability, 0.5);
}

TEST(ModuleStringParserDetail, ParseRuleMatchesPublicParseRule)
{
    const std::string_view input = "L < P > R : x -> S";
    const auto from_detail = Parser(input).parse_rule();
    const auto from_public = ModuleStringParser::parse_rule(input);
    EXPECT_EQ(from_detail.left_context.size(), from_public.left_context.size());
    EXPECT_EQ(from_detail.predecessor.name, from_public.predecessor.name);
    EXPECT_EQ(from_detail.right_context.size(), from_public.right_context.size());
    EXPECT_EQ(static_cast<bool>(from_detail.condition), static_cast<bool>(from_public.condition));
    EXPECT_DOUBLE_EQ(from_detail.probability, from_public.probability);
    EXPECT_EQ(from_detail.successor.size(), from_public.successor.size());
}

TEST(ModuleStringParserDetail, ParseSubsystemRef_SingleSymbol)
{
    const auto syms = ModuleStringParser::parse("ref.Leaf");
    ASSERT_EQ(syms.size(), 1u);
    EXPECT_EQ(syms[0].kind, SymbolKind::SubsystemRef);
    EXPECT_EQ(syms[0].subsystem, "Leaf");
}

TEST(ModuleStringParserDetail, ParseDottedModule_NotSubsystemRef)
{
    const auto syms = ModuleStringParser::parse("foo.bar");
    ASSERT_EQ(syms.size(), 1u);
    EXPECT_EQ(syms[0].kind, SymbolKind::Module);
    EXPECT_EQ(syms[0].name, "foo.bar");
}

TEST(ModuleStringParserDetail, ParseSubsystemRef_InvalidStandaloneRef_Throws)
{
    EXPECT_THROW(ModuleStringParser::parse("ref"), std::runtime_error);
}

TEST(ModuleStringParserDetail, ParseSubsystemRef_InvalidEmptyTarget_Throws)
{
    EXPECT_THROW(ModuleStringParser::parse("ref."), std::runtime_error);
}

TEST(ModuleStringParserDetail, ParseSubsystemRef_InvalidDoubleDot_Throws)
{
    EXPECT_THROW(ModuleStringParser::parse("ref..X"), std::runtime_error);
}

TEST(ModuleStringParserDetail, ParseSubsystemRef_ParensNotSupported_Throws)
{
    EXPECT_THROW(ModuleStringParser::parse("ref.Leaf(1)"), std::runtime_error);
}

TEST(ModuleStringParserDetail, ParseRuleSuccessor_MixedModulesAndRef)
{
    const auto rule = ModuleStringParser::parse_rule("A < B -> F ref.Leaf G");
    ASSERT_EQ(rule.successor.size(), 3u);
    EXPECT_EQ(rule.successor[0].kind, SymbolKind::Module);
    EXPECT_EQ(rule.successor[0].name, "F");
    EXPECT_EQ(rule.successor[1].kind, SymbolKind::SubsystemRef);
    EXPECT_EQ(rule.successor[1].subsystem, "Leaf");
    EXPECT_EQ(rule.successor[2].kind, SymbolKind::Module);
    EXPECT_EQ(rule.successor[2].name, "G");
}

TEST(ModuleStringParserDetail, ParseModuleNames_DigitsAndUnderscore)
{
    const auto syms = ModuleStringParser::parse("F1 a_b");
    ASSERT_EQ(syms.size(), 2u);
    EXPECT_EQ(syms[0].name, "F1");
    EXPECT_EQ(syms[1].name, "a_b");
}

TEST(ModuleStringParserDetail, SubsystemRef_DisallowedInExpression_Throws)
{
    EXPECT_THROW(ModuleStringParser::parse("F(ref.x)"), std::runtime_error);
}

TEST(ModuleStringParserDetail, SubsystemRef_DisallowedAsPredecessor_Throws)
{
    EXPECT_THROW(ModuleStringParser::parse_rule("< ref.Leaf -> S"), std::runtime_error);
}

TEST(ModuleStringParserDetail, ParseRule0L_SimplePredecessorAndSuccessor)
{
    const auto rule = ModuleStringParser::parse_rule("A -> B");
    EXPECT_TRUE(rule.left_context.empty());
    EXPECT_TRUE(rule.right_context.empty());
    EXPECT_EQ(rule.predecessor.name, "A");
    EXPECT_FALSE(rule.condition);
    EXPECT_DOUBLE_EQ(rule.probability, 1.0);
    ASSERT_EQ(rule.successor.size(), 1u);
    EXPECT_EQ(rule.successor[0].name, "B");
}

TEST(ModuleStringParserDetail, ParseRule0L_MultipleSuccessorModules)
{
    const auto rule = ModuleStringParser::parse_rule("A -> B C D");
    EXPECT_EQ(rule.predecessor.name, "A");
    EXPECT_TRUE(rule.left_context.empty());
    EXPECT_TRUE(rule.right_context.empty());
    ASSERT_EQ(rule.successor.size(), 3u);
    EXPECT_EQ(rule.successor[0].name, "B");
    EXPECT_EQ(rule.successor[1].name, "C");
    EXPECT_EQ(rule.successor[2].name, "D");
}

TEST(ModuleStringParserDetail, ParseRule0L_WithCondition)
{
    const auto rule = ModuleStringParser::parse_rule("A : ok -> B");
    EXPECT_EQ(rule.predecessor.name, "A");
    EXPECT_TRUE(rule.left_context.empty());
    ASSERT_TRUE(rule.condition);
    EXPECT_EQ(rule.condition->ident, "ok");
    EXPECT_DOUBLE_EQ(rule.probability, 1.0);
}

TEST(ModuleStringParserDetail, ParseRule0L_WithProbability)
{
    const auto rule = ModuleStringParser::parse_rule("A :: 0.5 -> B");
    EXPECT_EQ(rule.predecessor.name, "A");
    EXPECT_FALSE(rule.condition);
    EXPECT_DOUBLE_EQ(rule.probability, 0.5);
}

TEST(ModuleStringParserDetail, ParseRule0L_AmbiguousMultipleModules_Throws)
{
    EXPECT_THROW(ModuleStringParser::parse_rule("A B -> C"), std::runtime_error);
}

TEST(ModuleStringParserDetail, ParseRule0L_EmptyPredecessor_Throws)
{
    EXPECT_THROW(ModuleStringParser::parse_rule("-> B"), std::runtime_error);
}

TEST(ModuleStringParserDetail, LexerTokenizesBrackets)
{
    Lexer lex("[");
    EXPECT_EQ(lex.tok.kind, TokenKind::LBracket);
    lex.bump();
    EXPECT_EQ(lex.tok.kind, TokenKind::Eof);

    Lexer lex2("]");
    EXPECT_EQ(lex2.tok.kind, TokenKind::RBracket);
}

TEST(ModuleStringParserDetail, ParseBracketsInModuleString)
{
    const auto syms = ModuleStringParser::parse("F [ G ] H");
    ASSERT_EQ(syms.size(), 5u);
    EXPECT_EQ(syms[0].name, "F");
    EXPECT_EQ(syms[1].name, "[");
    EXPECT_EQ(syms[2].name, "G");
    EXPECT_EQ(syms[3].name, "]");
    EXPECT_EQ(syms[4].name, "H");
    for (const auto& s : syms)
    {
        EXPECT_EQ(s.kind, SymbolKind::Module);
    }
}

TEST(ModuleStringParserDetail, ParseNestedBracketsInModuleString)
{
    const auto syms = ModuleStringParser::parse("F [ G [ H ] ] I");
    ASSERT_EQ(syms.size(), 8u);
    EXPECT_EQ(syms[0].name, "F");
    EXPECT_EQ(syms[1].name, "[");
    EXPECT_EQ(syms[2].name, "G");
    EXPECT_EQ(syms[3].name, "[");
    EXPECT_EQ(syms[4].name, "H");
    EXPECT_EQ(syms[5].name, "]");
    EXPECT_EQ(syms[6].name, "]");
    EXPECT_EQ(syms[7].name, "I");
}

TEST(ModuleStringParserDetail, ParseRuleSuccessorWithBrackets)
{
    const auto rule = ModuleStringParser::parse_rule("A -> F [ G ] H");
    EXPECT_EQ(rule.predecessor.name, "A");
    ASSERT_EQ(rule.successor.size(), 5u);
    EXPECT_EQ(rule.successor[0].name, "F");
    EXPECT_EQ(rule.successor[1].name, "[");
    EXPECT_EQ(rule.successor[2].name, "G");
    EXPECT_EQ(rule.successor[3].name, "]");
    EXPECT_EQ(rule.successor[4].name, "H");
}

TEST(ModuleStringParserDetail, ParseBracketsWithParametricModules)
{
    const auto syms = ModuleStringParser::parse("F(1) [ G(2) ] H(3)");
    ASSERT_EQ(syms.size(), 5u);
    EXPECT_EQ(syms[0].name, "F");
    ASSERT_EQ(syms[0].args.size(), 1u);
    EXPECT_EQ(syms[1].name, "[");
    EXPECT_TRUE(syms[1].args.empty());
    EXPECT_EQ(syms[2].name, "G");
    ASSERT_EQ(syms[2].args.size(), 1u);
    EXPECT_EQ(syms[3].name, "]");
    EXPECT_TRUE(syms[3].args.empty());
    EXPECT_EQ(syms[4].name, "H");
    ASSERT_EQ(syms[4].args.size(), 1u);
}

TEST(ModuleStringParserDetail, ParseRulesBlock_ContinuationMergesSuccessor)
{
    const char* block = "A -> B\n  C D";
    const auto rules = ModuleStringParser::parse_rules_block(block);
    ASSERT_EQ(rules.size(), 1u);
    EXPECT_EQ(rules[0].predecessor.name, "A");
    ASSERT_EQ(rules[0].successor.size(), 3u);
    EXPECT_EQ(rules[0].successor[0].name, "B");
    EXPECT_EQ(rules[0].successor[1].name, "C");
    EXPECT_EQ(rules[0].successor[2].name, "D");
}

TEST(ModuleStringParserDetail, ParseRulesBlock_EmptyLineSeparatesRules)
{
    const char* block = "A -> B\n\nC -> D";
    const auto rules = ModuleStringParser::parse_rules_block(block);
    ASSERT_EQ(rules.size(), 2u);
    EXPECT_EQ(rules[0].predecessor.name, "A");
    ASSERT_EQ(rules[0].successor.size(), 1u);
    EXPECT_EQ(rules[0].successor[0].name, "B");
    EXPECT_EQ(rules[1].predecessor.name, "C");
    ASSERT_EQ(rules[1].successor.size(), 1u);
    EXPECT_EQ(rules[1].successor[0].name, "D");
}

TEST(ModuleStringParserDetail, ParseRulesBlock_NonIndentedLineStartsNewRule)
{
    const char* block = "A -> B\nC -> D";
    const auto rules = ModuleStringParser::parse_rules_block(block);
    ASSERT_EQ(rules.size(), 2u);
    EXPECT_EQ(rules[0].predecessor.name, "A");
    EXPECT_EQ(rules[1].predecessor.name, "C");
}

TEST(ModuleStringParserDetail, ParseRulesBlock_WhitespaceOnlyLineSeparates)
{
    const char* block = "A -> B\n   \nC -> D";
    const auto rules = ModuleStringParser::parse_rules_block(block);
    ASSERT_EQ(rules.size(), 2u);
    EXPECT_EQ(rules[0].predecessor.name, "A");
    EXPECT_EQ(rules[1].predecessor.name, "C");
}

TEST(ModuleStringParserDetail, ParseRulesBlock_TabContinuation)
{
    const char* block = "X -> Y\n\tZ";
    const auto rules = ModuleStringParser::parse_rules_block(block);
    ASSERT_EQ(rules.size(), 1u);
    ASSERT_EQ(rules[0].successor.size(), 2u);
    EXPECT_EQ(rules[0].successor[0].name, "Y");
    EXPECT_EQ(rules[0].successor[1].name, "Z");
}

TEST(ModuleStringParserDetail, ParseRulesBlock_HashCommentFullLineBetweenRules)
{
    const char* block = "A -> B\n# note\nC -> D";
    const auto rules = ModuleStringParser::parse_rules_block(block);
    ASSERT_EQ(rules.size(), 2u);
    EXPECT_EQ(rules[0].predecessor.name, "A");
    ASSERT_EQ(rules[0].successor.size(), 1u);
    EXPECT_EQ(rules[0].successor[0].name, "B");
    EXPECT_EQ(rules[1].predecessor.name, "C");
    ASSERT_EQ(rules[1].successor.size(), 1u);
    EXPECT_EQ(rules[1].successor[0].name, "D");
}

TEST(ModuleStringParserDetail, ParseRulesBlock_HashCommentInline)
{
    const char* block = "A -> B  # explanation";
    const auto rules = ModuleStringParser::parse_rules_block(block);
    ASSERT_EQ(rules.size(), 1u);
    EXPECT_EQ(rules[0].predecessor.name, "A");
    ASSERT_EQ(rules[0].successor.size(), 1u);
    EXPECT_EQ(rules[0].successor[0].name, "B");
}

TEST(ModuleStringParserDetail, ParseRulesBlock_HashCommentOnContinuationLine)
{
    const char* block = "A -> B\n  C  # tail";
    const auto rules = ModuleStringParser::parse_rules_block(block);
    ASSERT_EQ(rules.size(), 1u);
    ASSERT_EQ(rules[0].successor.size(), 2u);
    EXPECT_EQ(rules[0].successor[0].name, "B");
    EXPECT_EQ(rules[0].successor[1].name, "C");
}

TEST(ModuleStringParserDetail, ParseRulesBlock_HashCommentIndentedOnlyCommentFlushes)
{
    const char* block = "A -> B\n  # only comment";
    const auto rules = ModuleStringParser::parse_rules_block(block);
    ASSERT_EQ(rules.size(), 1u);
    EXPECT_EQ(rules[0].predecessor.name, "A");
    ASSERT_EQ(rules[0].successor.size(), 1u);
    EXPECT_EQ(rules[0].successor[0].name, "B");
}

TEST(ModuleStringParserDetail, ParseRulesBlock_BlankLineIndentedMidRuleDoesNotFlush)
{
    const char* block = "A -> B\n\n  C\n\n  D";
    const auto rules = ModuleStringParser::parse_rules_block(block);
    ASSERT_EQ(rules.size(), 1u);
    EXPECT_EQ(rules[0].predecessor.name, "A");
    ASSERT_EQ(rules[0].successor.size(), 3u);
    EXPECT_EQ(rules[0].successor[0].name, "B");
    EXPECT_EQ(rules[0].successor[1].name, "C");
    EXPECT_EQ(rules[0].successor[2].name, "D");
}

TEST(ModuleStringParserDetail, ParseRulesBlock_HashCommentIndentedMidRuleDoesNotFlush)
{
    const char* block = "A -> B\n  C\n  # section\n  D";
    const auto rules = ModuleStringParser::parse_rules_block(block);
    ASSERT_EQ(rules.size(), 1u);
    EXPECT_EQ(rules[0].predecessor.name, "A");
    ASSERT_EQ(rules[0].successor.size(), 3u);
    EXPECT_EQ(rules[0].successor[0].name, "B");
    EXPECT_EQ(rules[0].successor[1].name, "C");
    EXPECT_EQ(rules[0].successor[2].name, "D");
}

TEST(ModuleStringParserDetail, ParseRulesBlock_HashCommentBetweenRules)
{
    const char* block = "A -> B\n# note\nC -> D";
    const auto rules = ModuleStringParser::parse_rules_block(block);
    ASSERT_EQ(rules.size(), 2u);
    EXPECT_EQ(rules[0].predecessor.name, "A");
    ASSERT_EQ(rules[0].successor.size(), 1u);
    EXPECT_EQ(rules[0].successor[0].name, "B");
    EXPECT_EQ(rules[1].predecessor.name, "C");
    ASSERT_EQ(rules[1].successor.size(), 1u);
    EXPECT_EQ(rules[1].successor[0].name, "D");
}

TEST(ModuleStringParserDetail, ParseRulesBlock_HashCommentIndentedMidRuleWithBracket)
{
    const char* block = R"(Pedicel() ->
    F(0.5)
    # inner tube
    [
        F(0.4)
    ])";
    const auto rules = ModuleStringParser::parse_rules_block(block);
    ASSERT_EQ(rules.size(), 1u);
    EXPECT_EQ(rules[0].predecessor.name, "Pedicel");
    ASSERT_GE(rules[0].successor.size(), 3u);
    EXPECT_EQ(rules[0].successor[0].name, "F");
    bool has_bracket = false;
    for (const Symbol& sym : rules[0].successor)
    {
        if (sym.name == "[")
        {
            has_bracket = true;
            break;
        }
    }
    EXPECT_TRUE(has_bracket);
}
