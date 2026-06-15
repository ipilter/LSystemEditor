#include "LSystem.h"
#include "LSystemEvaluator.h"
#include "LSystemModel.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

namespace
{

std::vector<std::string> symbol_names(const LSystemEvaluator::Generation& generation)
{
    std::vector<std::string> out;
    out.reserve(generation.size());
    for (const Symbol& sym : generation)
    {
        if (sym.kind == SymbolKind::SubsystemRef)
        {
            out.push_back("ref." + sym.subsystem);
        }
        else
        {
            out.push_back(sym.name);
        }
    }
    return out;
}

double number_arg(const Symbol& sym, std::size_t arg_index)
{
    EXPECT_EQ(sym.kind, SymbolKind::Module);
    EXPECT_LT(arg_index, sym.args.size());
    EXPECT_TRUE(sym.args[arg_index]);
    EXPECT_EQ(sym.args[arg_index]->kind, Expr::Kind::Number);
    return sym.args[arg_index]->number_value;
}

} // namespace

TEST(LSystemEvaluatorTest, IterationsZeroReturnsOnlyAxiom)
{
    LSystem sys;
    sys.parse("A B C\nA -> B");

    const auto steps = LSystemEvaluator::evaluate_steps(sys, 0);
    ASSERT_EQ(steps.size(), 1u);
    EXPECT_EQ(symbol_names(steps[0]), (std::vector<std::string>{"A", "B", "C"}));
}

TEST(LSystemEvaluatorTest, DeterministicPredecessorRewriteAcrossMultipleIterations)
{
    LSystem sys;
    sys.parse(R"(A
A -> A B
B -> A)");

    const auto steps = LSystemEvaluator::evaluate_steps(sys, 3);
    ASSERT_EQ(steps.size(), 4u);

    EXPECT_EQ(symbol_names(steps[1]), (std::vector<std::string>{"A", "B"}));
    EXPECT_EQ(symbol_names(steps[2]), (std::vector<std::string>{"A", "B", "A"}));
    EXPECT_EQ(symbol_names(steps[3]), (std::vector<std::string>{"A", "B", "A", "A", "B"}));
}

TEST(LSystemEvaluatorTest, LeftAndRightContextRuleMatchesExpectedPredecessor)
{
    LSystem sys;
    sys.parse(R"(L P R
L < P > R -> X)");

    const auto steps = LSystemEvaluator::evaluate_steps(sys, 1);
    ASSERT_EQ(steps.size(), 2u);
    EXPECT_EQ(symbol_names(steps[1]), (std::vector<std::string>{"L", "X", "R"}));
}

TEST(LSystemEvaluatorTest, ConditionControlsRuleEligibility)
{
    LSystem sys;
    sys.parse(R"(A(0)
A(n) : n -> B
A(n) -> C)");

    const auto steps = LSystemEvaluator::evaluate_steps(sys, 1);
    ASSERT_EQ(steps.size(), 2u);
    EXPECT_EQ(symbol_names(steps[1]), (std::vector<std::string>{"C"}));
}

TEST(LSystemEvaluatorTest, ConditionEqualitySelectsRule)
{
    LSystem sys_zero;
    sys_zero.parse(R"lsys(A(0)
A(d) : d = 0 -> B)lsys");
    const auto steps_zero = LSystemEvaluator::evaluate_steps(sys_zero, 1);
    ASSERT_EQ(steps_zero.size(), 2u);
    EXPECT_EQ(symbol_names(steps_zero[1]), (std::vector<std::string>{"B"}));

    LSystem sys_nonzero;
    sys_nonzero.parse(R"lsys(A(1)
A(d) : d = 0 -> B)lsys");
    const auto steps_nonzero = LSystemEvaluator::evaluate_steps(sys_nonzero, 1);
    ASSERT_EQ(steps_nonzero.size(), 2u);
    EXPECT_EQ(symbol_names(steps_nonzero[1]), (std::vector<std::string>{"A"}));
}

TEST(LSystemEvaluatorTest, ConditionEqualityDoubleEqualsSelectsRule)
{
    LSystem sys;
    sys.parse(R"lsys(A(0)
A(d) : d == 0 -> B)lsys");
    const auto steps = LSystemEvaluator::evaluate_steps(sys, 1);
    ASSERT_EQ(steps.size(), 2u);
    EXPECT_EQ(symbol_names(steps[1]), (std::vector<std::string>{"B"}));
}

TEST(LSystemEvaluatorTest, ProbabilitySelectionIsReproducibleWithFixedSeed)
{
    LSystem sys;
    sys.parse(R"(A
A :: 0.8 -> B
A :: 0.2 -> C)");

    const auto first = LSystemEvaluator::evaluate_steps(sys, 1, 42);
    const auto second = LSystemEvaluator::evaluate_steps(sys, 1, 42);

    ASSERT_EQ(first.size(), 2u);
    ASSERT_EQ(second.size(), 2u);
    EXPECT_EQ(symbol_names(first[1]), symbol_names(second[1]));
}

TEST(LSystemEvaluatorTest, NoMatchingRulePreservesSymbol)
{
    LSystem sys;
    sys.parse(R"(A B
C -> D)");

    const auto steps = LSystemEvaluator::evaluate_steps(sys, 1);
    ASSERT_EQ(steps.size(), 2u);
    EXPECT_EQ(symbol_names(steps[1]), (std::vector<std::string>{"A", "B"}));
}

TEST(LSystemEvaluatorTest, ContextBindingsAndConditionInstantiateSuccessorArguments)
{
    LSystem sys;
    sys.parse(R"(A(2) P(2) B(3)
A(x) < P(x) > B(y) : y - x : 1 -> Q(x + y))");

    const auto steps = LSystemEvaluator::evaluate_steps(sys, 1, 11);
    ASSERT_EQ(steps.size(), 2u);
    ASSERT_EQ(steps[1].size(), 3u);
    EXPECT_EQ(symbol_names(steps[1]), (std::vector<std::string>{"A", "Q", "B"}));
    EXPECT_DOUBLE_EQ(number_arg(steps[1][0], 0), 2.0);
    EXPECT_DOUBLE_EQ(number_arg(steps[1][1], 0), 5.0);
    EXPECT_DOUBLE_EQ(number_arg(steps[1][2], 0), 3.0);
}

TEST(LSystemEvaluatorTest, ZeroLExampleMatchesFirstThreeIterations)
{
    LSystem sys;
    sys.parse(R"(A
A -> A B
B -> A)");

    const auto steps = LSystemEvaluator::evaluate_steps(sys, 3);
    ASSERT_EQ(steps.size(), 4u);

    EXPECT_EQ(symbol_names(steps[1]), (std::vector<std::string>{"A", "B"}));
    EXPECT_EQ(symbol_names(steps[2]), (std::vector<std::string>{"A", "B", "A"}));
    EXPECT_EQ(symbol_names(steps[3]), (std::vector<std::string>{"A", "B", "A", "A", "B"}));
}

TEST(LSystemEvaluatorTest, TwoLExampleMatchesFirstThreeIterations)
{
    LSystem sys;
    sys.parse(R"(A B C
A < B > C -> X
X -> B)");

    const auto steps = LSystemEvaluator::evaluate_steps(sys, 3);
    ASSERT_EQ(steps.size(), 4u);

    EXPECT_EQ(symbol_names(steps[1]), (std::vector<std::string>{"A", "X", "C"}));
    EXPECT_EQ(symbol_names(steps[2]), (std::vector<std::string>{"A", "B", "C"}));
    EXPECT_EQ(symbol_names(steps[3]), (std::vector<std::string>{"A", "X", "C"}));
}

TEST(LSystemEvaluatorTest, ParamTest)
{
  LSystem sys;
  sys.parse(R"(F(1)
              F(x) -> F(x+1))");

  const auto steps = LSystemEvaluator::evaluate_steps(sys, 3);
  ASSERT_EQ(steps.size(), 4u);
  ASSERT_EQ(steps[3].size(), 1u);
  EXPECT_EQ(symbol_names(steps[3]), (std::vector<std::string>{"F"}));
  EXPECT_DOUBLE_EQ(number_arg(steps[3][0], 0), 4.0);
}

TEST(LSystemEvaluatorTest, ParamTest2)
{
  LSystem sys;
  sys.parse("F(0)\n"
            "F(x) : x  < 2 : 1 -> F(x + 1)\n"
            "F(x) : x >= 2 : 1 -> F(x + 10)\n");

  const auto steps = LSystemEvaluator::evaluate_steps(sys, 4);
  ASSERT_EQ(steps.size(), 5u);
  ASSERT_EQ(steps[3].size(), 1u);
  EXPECT_EQ(symbol_names(steps[4]), (std::vector<std::string>{"F"}));
  EXPECT_DOUBLE_EQ(number_arg(steps[4][0], 0), 22.0);
}

TEST(LSystemEvaluatorTest, SimpleBranching)
{
    LSystem sys;
    sys.parse(R"(F
F -> F [ G ] F)");

    const auto steps = LSystemEvaluator::evaluate_steps(sys, 1);
    ASSERT_EQ(steps.size(), 2u);
    EXPECT_EQ(symbol_names(steps[0]), (std::vector<std::string>{"F"}));
    EXPECT_EQ(symbol_names(steps[1]),
        (std::vector<std::string>{"F", "[", "G", "]", "F"}));
}

TEST(LSystemEvaluatorTest, BranchingTwoIterations)
{
    LSystem sys;
    sys.parse(R"(A
A -> F [ B ] A
B -> G)");

    const auto steps = LSystemEvaluator::evaluate_steps(sys, 2);
    ASSERT_EQ(steps.size(), 3u);

    EXPECT_EQ(symbol_names(steps[1]),
        (std::vector<std::string>{"F", "[", "B", "]", "A"}));

    EXPECT_EQ(symbol_names(steps[2]),
        (std::vector<std::string>{"F", "[", "G", "]", "F", "[", "B", "]", "A"}));
}

TEST(LSystemEvaluatorTest, BracketsPassThroughUnchanged)
{
    LSystem sys;
    sys.parse(R"(F [ G ] H
G -> X)");

    const auto steps = LSystemEvaluator::evaluate_steps(sys, 1);
    ASSERT_EQ(steps.size(), 2u);
    EXPECT_EQ(symbol_names(steps[1]),
        (std::vector<std::string>{"F", "[", "X", "]", "H"}));
}

TEST(LSystemEvaluatorTest, TreeTest)
{
  LSystem sys;
  sys.parse("F\nF->F[F]F");

  const auto steps = LSystemEvaluator::evaluate_steps(sys, 3);
  ASSERT_EQ(steps.size(), 4u);
  EXPECT_EQ(symbol_names(steps[0]), (std::vector<std::string>{"F"}));
  EXPECT_EQ(symbol_names(steps[1]), (std::vector<std::string>{"F", "[", "F", "]", "F"}));
  EXPECT_EQ(steps[2].size(), 17u);
  EXPECT_GT(steps[3].size(), steps[2].size());
}

/** Parametric F(h,r) with Yaw branches (editor-style) and expressions in successor args. */
TEST(LSystemEvaluatorTest, ParametricYawBranchingOneStep)
{
    LSystem sys;
    sys.parse(R"(F(3,0.5)
F(h,r) -> [Yaw(22.5)F(h*0.7,r*0.55)][Yaw(0-22.5)F(h*0.7,r*0.55)])");

    const auto steps = LSystemEvaluator::evaluate_steps(sys, 1);
    ASSERT_EQ(steps.size(), 2u);
    EXPECT_EQ(symbol_names(steps[1]),
        (std::vector<std::string>{"[", "Yaw", "F", "]", "[", "Yaw", "F", "]"}));

    const Symbol& left_f = steps[1][2];
    EXPECT_EQ(left_f.name, "F");
    EXPECT_DOUBLE_EQ(number_arg(left_f, 0), 3.0 * 0.7);
    EXPECT_DOUBLE_EQ(number_arg(left_f, 1), 0.5 * 0.55);

    const Symbol& right_f = steps[1][6];
    EXPECT_EQ(right_f.name, "F");
    EXPECT_DOUBLE_EQ(number_arg(right_f, 0), 3.0 * 0.7);
    EXPECT_DOUBLE_EQ(number_arg(right_f, 1), 0.5 * 0.55);
}

/** Parametric F(h,r) with Pitch branches and expressions in successor args. */
TEST(LSystemEvaluatorTest, ParametricPitchBranchingOneStep)
{
    LSystem sys;
    sys.parse(R"(F(3,0.5)
F(h,r) -> [Pitch(0-22.5)F(h*0.7,r*0.55)][Pitch(22.5)F(h*0.7,r*0.55)])");

    const auto steps = LSystemEvaluator::evaluate_steps(sys, 1);
    ASSERT_EQ(steps.size(), 2u);
    EXPECT_EQ(symbol_names(steps[1]),
        (std::vector<std::string>{"[", "Pitch", "F", "]", "[", "Pitch", "F", "]"}));

    const Symbol& left_f = steps[1][2];
    EXPECT_EQ(left_f.name, "F");
    EXPECT_DOUBLE_EQ(number_arg(left_f, 0), 3.0 * 0.7);
    EXPECT_DOUBLE_EQ(number_arg(left_f, 1), 0.5 * 0.55);

    const Symbol& right_f = steps[1][6];
    EXPECT_EQ(right_f.name, "F");
    EXPECT_DOUBLE_EQ(number_arg(right_f, 0), 3.0 * 0.7);
    EXPECT_DOUBLE_EQ(number_arg(right_f, 1), 0.5 * 0.55);
}

/** Unary minus is not a number token; `0-x` in parentheses is valid. */
TEST(LSystemEvaluatorTest, PitchAngleBinaryMinusInModuleArg)
{
    LSystem sys;
    sys.parse(R"(F
F -> [Pitch(0-45)F(1)][Pitch(45)F(1)])");

    const auto steps = LSystemEvaluator::evaluate_steps(sys, 1);
    ASSERT_EQ(steps.size(), 2u);
    const Symbol& pitch0 = steps[1][1];
    EXPECT_EQ(pitch0.name, "Pitch");
    EXPECT_DOUBLE_EQ(number_arg(pitch0, 0), -45.0);
}

TEST(LSystemEvaluatorTest, EditorStyleMultilineInitRuleParsesAndEvaluates)
{
    LSystem sys;
    ASSERT_NO_THROW(sys.parse(
        R"(Init
Init -> Pitch(10) F(3,0.5)
F(h,r) -> [Yaw(22.5)F(h*0.7,r*0.55)][Yaw(0-22.5)F(h*0.7,r*0.55)])"));
    const auto steps = LSystemEvaluator::evaluate_steps(sys, 1);
    ASSERT_EQ(steps.size(), 2u);
    ASSERT_EQ(steps[1].size(), 2u);
    EXPECT_EQ(steps[1][0].name, "Pitch");
    EXPECT_EQ(steps[1][1].name, "F");
}

TEST(LSystemEvaluatorTest, EvaluateFromDefinition_ReturnsGenerationAndMaterials)
{
    const LSystemEvaluationResult result = LSystemEvaluator::evaluate(
        "Mat(0) = Diffuse {0.5, 0.6, 0.7, 0.8}\n"
        "F\n"
        "F -> F F\n",
        1);

    ASSERT_EQ(result.materials.size(), 1u);
    EXPECT_EQ(result.materials[0].id, 0u);
    EXPECT_NEAR(result.materials[0].entry.r, 0.5f, 1e-5f);
    ASSERT_EQ(result.generation.size(), 2u);
    EXPECT_EQ(result.generation[0].name, "F");
    EXPECT_EQ(result.generation[1].name, "F");
}
