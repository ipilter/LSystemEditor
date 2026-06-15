#include "LSystem.h"
#include "LSystemConstantsParse.h"
#include "LSystemEvaluator.h"

#include <gtest/gtest.h>

TEST(LSystemConstantsParse, parses_threshold_line)
{
    std::unordered_map<std::string, double> constants;
    ASSERT_TRUE(try_parse_constant_line("threshold = 0.4", constants));
    ASSERT_EQ(constants.size(), 1u);
    EXPECT_NEAR(constants.at("threshold"), 0.4, 1e-9);
}

TEST(LSystemConstantsParse, duplicate_constant_throws)
{
    std::unordered_map<std::string, double> constants;
    ASSERT_TRUE(try_parse_constant_line("threshold = 0.4", constants));
    EXPECT_THROW(
        {
            const bool ignored = try_parse_constant_line("threshold = 0.5", constants);
            (void)ignored;
        },
        std::runtime_error);
}

TEST(LSystemConstantsParse, not_material_or_module_line)
{
    EXPECT_FALSE(is_global_constant_line("Mat(0) = Diffuse {1,2,3}"));
    EXPECT_FALSE(is_global_constant_line("Mat(0)"));
    EXPECT_TRUE(is_global_constant_line("threshold = 0.5"));
}

TEST(LSystemConstantsParse, evaluator_uses_threshold_in_condition)
{
    LSystem sys;
    sys.parse("threshold = 0.4\n"
              "F(1, 0.5)\n"
              "F(h, r) : r > threshold -> X\n"
              "F(h, r) : r <= threshold -> Y\n");

    const auto steps = LSystemEvaluator::evaluate_steps(sys, 1);
    ASSERT_EQ(steps.size(), 2u);
    ASSERT_EQ(steps[1].size(), 1u);
    EXPECT_EQ(steps[1][0].name, "X");
}
