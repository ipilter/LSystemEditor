#include "LSystemController.h"
#include "LSystem.h"
#include "LSystemModel.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

TEST(LSystemController, Find_ReturnsNullWhenMissing)
{
    LSystemController ctrl;
    EXPECT_EQ(ctrl.find("Leaf"), nullptr);
}

TEST(LSystemController, RegisterAndFind_ReturnsPointer)
{
    LSystem leaf;
    leaf.parse("L\n");

    LSystemController ctrl;
    ctrl.register_system("Leaf", std::move(leaf));

    const LSystem* p = ctrl.find("Leaf");
    ASSERT_NE(p, nullptr);
    ASSERT_EQ(p->axiom_modules().size(), 1u);
    EXPECT_EQ(p->axiom_modules()[0].name, "L");
}

TEST(LSystemController, Validate_SucceedsWhenSubsystemRegistered)
{
    LSystem leaf;
    leaf.parse("X\n");

    LSystem root;
    root.parse("ref.Leaf\n< A -> ref.Leaf\n");

    LSystemController ctrl;
    ctrl.register_system("Leaf", std::move(leaf));
    EXPECT_NO_THROW(ctrl.validate(root));
}

TEST(LSystemController, Validate_ThrowsWhenSubsystemMissing)
{
    LSystem root;
    root.parse("ref.Missing\n");

    LSystemController ctrl;
    try
    {
        ctrl.validate(root);
        FAIL() << "expected exception";
    }
    catch (const std::runtime_error& e)
    {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("Missing"), std::string::npos);
    }
}

TEST(LSystemController, Validate_TransitiveSubsystemRefs)
{
    LSystem leaf;
    leaf.parse("Y\n");

    LSystem branch;
    branch.parse("ref.Leaf\n< B -> Z\n");

    LSystem root;
    root.parse("ref.Branch\n");

    LSystemController ctrl;
    ctrl.register_system("Leaf", std::move(leaf));
    ctrl.register_system("Branch", std::move(branch));
    EXPECT_NO_THROW(ctrl.validate(root));
}

TEST(LSystemController, Validate_Cycle_DoesNotThrow)
{
    LSystem a;
    a.parse("ref.B\n");

    LSystem b;
    b.parse("ref.A\n");

    LSystemController ctrl;
    ctrl.register_system("A", std::move(a));
    ctrl.register_system("B", std::move(b));

    LSystem a2;
    a2.parse("ref.B\n");
    EXPECT_NO_THROW(ctrl.validate(a2));
}

TEST(LSystemController, DuplicateRegister_LastWins)
{
    LSystem first;
    first.parse("First\n");

    LSystem second;
    second.parse("Second\n");

    LSystemController ctrl;
    ctrl.register_system("X", std::move(first));
    ctrl.register_system("X", std::move(second));

    const LSystem* p = ctrl.find("X");
    ASSERT_NE(p, nullptr);
    ASSERT_EQ(p->axiom_modules().size(), 1u);
    EXPECT_EQ(p->axiom_modules()[0].name, "Second");
}
