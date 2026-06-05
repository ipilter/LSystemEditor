#include "LSystem.h"
#include "LSystemModel.h"

#include <gtest/gtest.h>

#include <stdexcept>

namespace
{

void expect_axiom_f10_a(const LSystem& sys)
{
    const auto& ax = sys.axiom_modules();
    ASSERT_EQ(ax.size(), 2u);
    EXPECT_EQ(ax[0].kind, SymbolKind::Module);
    EXPECT_EQ(ax[0].name, "F");
    ASSERT_EQ(ax[0].args.size(), 1u);
    ASSERT_TRUE(ax[0].args[0]);
    EXPECT_EQ(ax[0].args[0]->kind, Expr::Kind::Number);
    EXPECT_DOUBLE_EQ(ax[0].args[0]->number_value, 10.0);
    EXPECT_EQ(ax[1].kind, SymbolKind::Module);
    EXPECT_EQ(ax[1].name, "A");
    EXPECT_TRUE(ax[1].args.empty());
}

} // namespace

TEST(LSystemDefinition, EmptyInput_ClearsAxiomAndRules)
{
    LSystem sys;
    EXPECT_NO_THROW(sys.parse(""));
    EXPECT_TRUE(sys.axiom_modules().empty());
    EXPECT_TRUE(sys.rules().empty());
}

TEST(LSystemDefinition, WhitespaceOnly_ClearsAxiomAndRules)
{
    LSystem sys;
    sys.parse("   \n\t  \n  ");
    EXPECT_TRUE(sys.axiom_modules().empty());
    EXPECT_TRUE(sys.rules().empty());
}

TEST(LSystemDefinition, AxiomOnly_SingleModule)
{
  LSystem sys;
  ASSERT_NO_THROW(sys.parse("F\n"));
  ASSERT_EQ(sys.axiom_modules().size(), 1u);
  EXPECT_EQ(sys.axiom_modules()[0].name, "F");
  ASSERT_TRUE(sys.axiom_modules()[0].args.empty());
  EXPECT_TRUE(sys.rules().empty());
}

TEST(LSystemDefinition, AxiomOnly_SingleParametricModule)
{
    LSystem sys;
    ASSERT_NO_THROW(sys.parse("F(10)\n"));
    ASSERT_EQ(sys.axiom_modules().size(), 1u);
    EXPECT_EQ(sys.axiom_modules()[0].name, "F");
    ASSERT_EQ(sys.axiom_modules()[0].args.size(), 1u);
    ASSERT_TRUE(sys.axiom_modules()[0].args[0]);
    EXPECT_EQ(sys.axiom_modules()[0].args[0]->kind, Expr::Kind::Number);
    EXPECT_DOUBLE_EQ(sys.axiom_modules()[0].args[0]->number_value, 10.0);
    EXPECT_TRUE(sys.rules().empty());
}

TEST(LSystemDefinition, AxiomOnly_MultipleModulesWithMixedArgs)
{
    const char* def = R"(Move(1, 2, x) B C)";

    LSystem sys;
    ASSERT_NO_THROW(sys.parse(def));
    ASSERT_EQ(sys.axiom_modules().size(), 3u);
    EXPECT_EQ(sys.axiom_modules()[0].name, "Move");
    ASSERT_EQ(sys.axiom_modules()[0].args.size(), 3u);
    ASSERT_TRUE(sys.axiom_modules()[0].args[0]);
    EXPECT_EQ(sys.axiom_modules()[0].args[0]->kind, Expr::Kind::Number);
    EXPECT_DOUBLE_EQ(sys.axiom_modules()[0].args[0]->number_value, 1.0);
    ASSERT_TRUE(sys.axiom_modules()[0].args[2]);
    EXPECT_EQ(sys.axiom_modules()[0].args[2]->kind, Expr::Kind::Ident);
    EXPECT_EQ(sys.axiom_modules()[0].args[2]->ident, "x");
    EXPECT_EQ(sys.axiom_modules()[1].name, "B");
    EXPECT_TRUE(sys.axiom_modules()[1].args.empty());
    EXPECT_EQ(sys.axiom_modules()[2].name, "C");
    EXPECT_TRUE(sys.rules().empty());
}

TEST(LSystemDefinition, Full_TwoRules_LeftContextAndRightContextCondition)
{
    const char* def = R"(F(10) A
L < P -> S
< P > R : x -> D)";

    LSystem sys;
    ASSERT_NO_THROW(sys.parse(def));
    expect_axiom_f10_a(sys);

    const auto& rules = sys.rules();
    ASSERT_EQ(rules.size(), 2u);

    EXPECT_EQ(rules[0].left_context.size(), 1u);
    EXPECT_EQ(rules[0].left_context[0].name, "L");
    EXPECT_TRUE(rules[0].right_context.empty());
    EXPECT_EQ(rules[0].predecessor.name, "P");
    EXPECT_FALSE(rules[0].condition);
    EXPECT_DOUBLE_EQ(rules[0].probability, 1.0);
    ASSERT_EQ(rules[0].successor.size(), 1u);
    EXPECT_EQ(rules[0].successor[0].name, "S");

    EXPECT_TRUE(rules[1].left_context.empty());
    EXPECT_EQ(rules[1].predecessor.name, "P");
    ASSERT_EQ(rules[1].right_context.size(), 1u);
    EXPECT_EQ(rules[1].right_context[0].name, "R");
    ASSERT_TRUE(rules[1].condition);
    EXPECT_EQ(rules[1].condition->kind, Expr::Kind::Ident);
    EXPECT_EQ(rules[1].condition->ident, "x");
    EXPECT_DOUBLE_EQ(rules[1].probability, 1.0);
    ASSERT_EQ(rules[1].successor.size(), 1u);
    EXPECT_EQ(rules[1].successor[0].name, "D");
}

TEST(LSystemDefinition, Full_ManyRules_AllParsedInOrder)
{
    const char* def = R"(X
< A -> B
B < C -> D
L < P > R : n + 1 -> E F)";

    LSystem sys;
    ASSERT_NO_THROW(sys.parse(def));
    ASSERT_EQ(sys.axiom_modules().size(), 1u);
    EXPECT_EQ(sys.axiom_modules()[0].name, "X");
    ASSERT_EQ(sys.rules().size(), 3u);
    EXPECT_EQ(sys.rules()[0].predecessor.name, "A");
    EXPECT_EQ(sys.rules()[1].predecessor.name, "C");
    EXPECT_EQ(sys.rules()[2].predecessor.name, "P");
    ASSERT_EQ(sys.rules()[2].successor.size(), 2u);
    EXPECT_EQ(sys.rules()[2].successor[0].name, "E");
    EXPECT_EQ(sys.rules()[2].successor[1].name, "F");
    ASSERT_TRUE(sys.rules()[2].condition);
    EXPECT_EQ(sys.rules()[2].condition->kind, Expr::Kind::Add);
}

TEST(LSystemDefinition, RuleEdge_BothContextsNoCondition)
{
    const char* def = R"(S
L < P > R -> M)";

    LSystem sys;
    ASSERT_NO_THROW(sys.parse(def));
    ASSERT_EQ(sys.rules().size(), 1u);
    EXPECT_EQ(sys.rules()[0].left_context.size(), 1u);
    EXPECT_EQ(sys.rules()[0].left_context[0].name, "L");
    EXPECT_EQ(sys.rules()[0].right_context.size(), 1u);
    EXPECT_EQ(sys.rules()[0].right_context[0].name, "R");
    EXPECT_FALSE(sys.rules()[0].condition);
    ASSERT_EQ(sys.rules()[0].successor.size(), 1u);
    EXPECT_EQ(sys.rules()[0].successor[0].name, "M");
}

TEST(LSystemDefinition, RuleEdge_StochasticProbabilityWithoutCondition)
{
    const char* def = R"(A
X < Y > :: 0.5 -> Z)";

    LSystem sys;
    ASSERT_NO_THROW(sys.parse(def));
    ASSERT_EQ(sys.rules().size(), 1u);
    EXPECT_FALSE(sys.rules()[0].condition);
    EXPECT_DOUBLE_EQ(sys.rules()[0].probability, 0.5);
}

TEST(LSystemDefinition, RuleEdge_0LSystemCase)
{
    const char* def = R"(A
A -> B
B -> C
C -> A)";

    LSystem sys;
    ASSERT_NO_THROW(sys.parse(def));
}


TEST(LSystemDefinition, RuleEdge_ConditionWithExplicitProbability)
{
    const char* def = R"(A
X < Y : ok : 0.25 -> W)";

    LSystem sys;
    ASSERT_NO_THROW(sys.parse(def));
    ASSERT_EQ(sys.rules().size(), 1u);
    ASSERT_TRUE(sys.rules()[0].condition);
    EXPECT_EQ(sys.rules()[0].condition->ident, "ok");
    EXPECT_DOUBLE_EQ(sys.rules()[0].probability, 0.25);
    ASSERT_EQ(sys.rules()[0].successor.size(), 1u);
    EXPECT_EQ(sys.rules()[0].successor[0].name, "W");
}

TEST(LSystemDefinition, Layout_SkipsBlankLinesBetweenSections)
{
    const char* def = R"(F(10) A

L < P -> S

)";

    LSystem sys;
    ASSERT_NO_THROW(sys.parse(def));
    expect_axiom_f10_a(sys);
    ASSERT_EQ(sys.rules().size(), 1u);
    EXPECT_EQ(sys.rules()[0].predecessor.name, "P");
}

TEST(LSystemDefinition, Layout_CarriageReturnLineEndings)
{
    const char* def = "F(1)\r\nR < Q -> T\r\n";
    LSystem sys;
    ASSERT_NO_THROW(sys.parse(def));
    ASSERT_EQ(sys.axiom_modules().size(), 1u);
    EXPECT_EQ(sys.axiom_modules()[0].name, "F");
    ASSERT_EQ(sys.rules().size(), 1u);
    EXPECT_EQ(sys.rules()[0].predecessor.name, "Q");
}

/** First non-empty line may be a rule (`->`); axiom is then empty. */
TEST(LSystemDefinition, RuleOnlyFirstLine_EmptyAxiom)
{
    LSystem sys;
    ASSERT_NO_THROW(sys.parse("L < P -> S"));
    EXPECT_TRUE(sys.axiom_modules().empty());
    ASSERT_EQ(sys.rules().size(), 1u);
    EXPECT_EQ(sys.rules()[0].predecessor.name, "P");
}

/** After the first `->` line, every line must be a valid rule. */
TEST(LSystemDefinition, Invalid_LineAfterFirstRule_Throws)
{
    LSystem sys;
    EXPECT_THROW(sys.parse("A -> B\nnot a rule"), std::runtime_error);
}

TEST(LSystemDefinition, MultilineAxiomSameAsSingleLineJoined)
{
    const char* single_line_axiom = R"(Pitch(10) X F(3,0.5)
F(h,r) -> G)";

    const char* multiline_axiom = R"(Pitch(10) X
F(3,0.5)
F(h,r) -> G)";

    LSystem compact;
    LSystem wrapped;
    ASSERT_NO_THROW(compact.parse(single_line_axiom));
    ASSERT_NO_THROW(wrapped.parse(multiline_axiom));

    ASSERT_EQ(compact.axiom_modules().size(), wrapped.axiom_modules().size());
    for (std::size_t i = 0; i < compact.axiom_modules().size(); ++i)
    {
        EXPECT_EQ(compact.axiom_modules()[i].name, wrapped.axiom_modules()[i].name);
    }
    ASSERT_EQ(compact.rules().size(), wrapped.rules().size());
    EXPECT_EQ(compact.rules()[0].predecessor.name, wrapped.rules()[0].predecessor.name);
}

TEST(LSystemDefinition, AxiomOnly_SubsystemRef)
{
    LSystem sys;
    ASSERT_NO_THROW(sys.parse("ref.Leaf\n"));
    ASSERT_EQ(sys.axiom_modules().size(), 1u);
    EXPECT_EQ(sys.axiom_modules()[0].kind, SymbolKind::SubsystemRef);
    EXPECT_EQ(sys.axiom_modules()[0].subsystem, "Leaf");
    EXPECT_TRUE(sys.axiom_modules()[0].name.empty());
    EXPECT_TRUE(sys.rules().empty());
}

TEST(LSystemDefinition, RuleSuccessor_ContainsSubsystemRef)
{
    const char* def = R"(X
< A -> F ref.Leaf G)";

    LSystem sys;
    ASSERT_NO_THROW(sys.parse(def));
    ASSERT_EQ(sys.rules().size(), 1u);
    EXPECT_EQ(sys.rules()[0].predecessor.name, "A");
    ASSERT_EQ(sys.rules()[0].successor.size(), 3u);
    EXPECT_EQ(sys.rules()[0].successor[0].kind, SymbolKind::Module);
    EXPECT_EQ(sys.rules()[0].successor[0].name, "F");
    EXPECT_EQ(sys.rules()[0].successor[1].kind, SymbolKind::SubsystemRef);
    EXPECT_EQ(sys.rules()[0].successor[1].subsystem, "Leaf");
    EXPECT_EQ(sys.rules()[0].successor[2].kind, SymbolKind::Module);
    EXPECT_EQ(sys.rules()[0].successor[2].name, "G");
}

TEST(LSystemDefinition, Invalid_SubsystemRef_InPredecessor_Throws)
{
    LSystem sys;
    EXPECT_THROW(sys.parse("X\n< ref.Leaf -> S"), std::runtime_error);
}

TEST(LSystemDefinition, Invalid_SubsystemRef_InLeftContext_Throws)
{
    LSystem sys;
    EXPECT_THROW(sys.parse("X\nref.Leaf < P -> S"), std::runtime_error);
}

TEST(LSystemDefinition, Invalid_SubsystemRef_InRightContext_Throws)
{
    LSystem sys;
    EXPECT_THROW(sys.parse("X\n< P > ref.Leaf -> S"), std::runtime_error);
}

TEST(LSystemDefinition, Layout_BlankLines_WithSubsystemRef)
{
    const char* def = R"(ref.Leaf

< A -> ref.Leaf

)";

    LSystem sys;
    ASSERT_NO_THROW(sys.parse(def));
    ASSERT_EQ(sys.axiom_modules().size(), 1u);
    EXPECT_EQ(sys.axiom_modules()[0].kind, SymbolKind::SubsystemRef);
    ASSERT_EQ(sys.rules().size(), 1u);
    ASSERT_EQ(sys.rules()[0].successor.size(), 1u);
    EXPECT_EQ(sys.rules()[0].successor[0].subsystem, "Leaf");
}

TEST(LSystemDefinition, Layout_CarriageReturn_WithSubsystemRef)
{
    const char* def = "ref.Leaf\r\n< A -> B\r\n";
    LSystem sys;
    ASSERT_NO_THROW(sys.parse(def));
    EXPECT_EQ(sys.axiom_modules()[0].kind, SymbolKind::SubsystemRef);
}

TEST(LSystemDefinition, ModuleName_Preference_NotParsedAsSubsystemRef)
{
    LSystem sys;
    ASSERT_NO_THROW(sys.parse("Preference\n"));
    ASSERT_EQ(sys.axiom_modules().size(), 1u);
    EXPECT_EQ(sys.axiom_modules()[0].kind, SymbolKind::Module);
    EXPECT_EQ(sys.axiom_modules()[0].name, "Preference");
}

TEST(LSystemDefinition, Rules_MultilineContinuation_IndentedSuccessor)
{
    const char* def = R"(F
A -> B
  C D
E -> G)";

    LSystem sys;
    ASSERT_NO_THROW(sys.parse(def));
    ASSERT_EQ(sys.axiom_modules().size(), 1u);
    EXPECT_EQ(sys.axiom_modules()[0].name, "F");
    ASSERT_EQ(sys.rules().size(), 2u);
    EXPECT_EQ(sys.rules()[0].predecessor.name, "A");
    ASSERT_EQ(sys.rules()[0].successor.size(), 3u);
    EXPECT_EQ(sys.rules()[0].successor[0].name, "B");
    EXPECT_EQ(sys.rules()[0].successor[1].name, "C");
    EXPECT_EQ(sys.rules()[0].successor[2].name, "D");
    EXPECT_EQ(sys.rules()[1].predecessor.name, "E");
    ASSERT_EQ(sys.rules()[1].successor.size(), 1u);
    EXPECT_EQ(sys.rules()[1].successor[0].name, "G");
}

TEST(LSystemDefinition, Rules_HashComment_InAxiomSection_AndInlineRule)
{
    const char* def = R"(F
# note: A -> B is not a rule start marker here
A -> B  # side
C -> D)";

    LSystem sys;
    ASSERT_NO_THROW(sys.parse(def));
    ASSERT_EQ(sys.axiom_modules().size(), 1u);
    EXPECT_EQ(sys.axiom_modules()[0].name, "F");
    ASSERT_EQ(sys.rules().size(), 2u);
    EXPECT_EQ(sys.rules()[0].predecessor.name, "A");
    ASSERT_EQ(sys.rules()[0].successor.size(), 1u);
    EXPECT_EQ(sys.rules()[0].successor[0].name, "B");
    EXPECT_EQ(sys.rules()[1].predecessor.name, "C");
    ASSERT_EQ(sys.rules()[1].successor.size(), 1u);
    EXPECT_EQ(sys.rules()[1].successor[0].name, "D");
}

TEST(LSystemDefinition, Rules_HashCommentIndentedMidRuleWithBracket)
{
    const char* def = R"(F
Pedicel() ->
    F(0.5, 0.45, 0.50)
    # inner branch
    [
        F(0.4, 0.06, 0.05)
    ])";

    LSystem sys;
    ASSERT_NO_THROW(sys.parse(def));
    ASSERT_EQ(sys.rules().size(), 1u);
    EXPECT_EQ(sys.rules()[0].predecessor.name, "Pedicel");
    bool has_bracket = false;
    for (const Symbol& sym : sys.rules()[0].successor)
    {
        if (sym.name == "[")
        {
            has_bracket = true;
            break;
        }
    }
    EXPECT_TRUE(has_bracket);
}
