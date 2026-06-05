#include "LSystem.h"
#include "LSystemMaterialParse.h"

#include <gtest/gtest.h>

#include <string>

namespace
{

const MaterialEntry* find_material(const std::vector<MaterialDefinition>& definitions, const std::uint32_t id)
{
    for (const MaterialDefinition& def : definitions)
    {
        if (def.id == id)
        {
            return &def.entry;
        }
    }
    return nullptr;
}

bool is_defined(const std::vector<MaterialDefinition>& definitions, const std::uint32_t id)
{
    return find_material(definitions, id) != nullptr;
}

} // namespace

TEST(LSystemMaterialParse, parses_mat_paren_brace_line)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(0) = {0.72, 0.68, 0.58, 0.85}", definitions));
    const MaterialEntry* e = find_material(definitions, 0);
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(e->r, 0.72f, 1e-5f);
    EXPECT_NEAR(e->g, 0.68f, 1e-5f);
    EXPECT_NEAR(e->b, 0.58f, 1e-5f);
    EXPECT_NEAR(e->roughness, 0.85f, 1e-5f);
    EXPECT_NEAR(e->metallic, 0.f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_no_spaces_around_equals)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(0)={1, 2, 3}", definitions));
    const MaterialEntry* e = find_material(definitions, 0);
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(e->r, 1.f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_six_components_with_emission)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(4) = {0.2, 0.3, 0.4, 0.5, 0.6, 2.0}", definitions));
    const MaterialEntry* e = find_material(definitions, 4);
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(e->roughness, 0.5f, 1e-5f);
    EXPECT_NEAR(e->metallic, 0.6f, 1e-5f);
    EXPECT_NEAR(e->emission, 2.0f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_three_and_five_components)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(2) = {1, 0, 0}", definitions));
    ASSERT_TRUE(try_parse_material_line("Mat(3) = {0.1, 0.2, 0.3, 0.4, 0.5}", definitions));
    const MaterialEntry* e3 = find_material(definitions, 3);
    ASSERT_NE(e3, nullptr);
    EXPECT_NEAR(e3->metallic, 0.5f, 1e-5f);
}

TEST(LSystemMaterialParse, duplicate_id_throws)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(1) = {0.1, 0.2, 0.3}", definitions));
    EXPECT_THROW(
        {
            const bool ignored = try_parse_material_line("Mat(1) = {0.4, 0.5, 0.6}", definitions);
            (void)ignored;
        },
        std::runtime_error);
}

TEST(LSystemMaterialParse, rejects_old_matn_syntax)
{
    std::vector<MaterialDefinition> definitions;
    EXPECT_FALSE(try_parse_material_line("Mat0 = {1, 2, 3}", definitions));
    EXPECT_FALSE(is_material_declaration_line("Mat0 = {1,2,3}"));
}

TEST(LSystemMaterialParse, lsystem_parse_collects_materials)
{
    LSystem ls;
    ls.parse("Mat(0) = {0.72, 0.68, 0.58}\n"
             "Mat(0)\n"
             "F(1)\n"
             "A -> B\n");
    const MaterialEntry* e = find_material(ls.material_definitions(), 0);
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(e->r, 0.72f, 1e-5f);
    ASSERT_EQ(ls.axiom_modules().size(), 2u);
    EXPECT_EQ(ls.axiom_modules()[0].name, "Mat");
    EXPECT_EQ(ls.axiom_modules()[1].name, "F");
}

TEST(LSystemMaterialParse, is_material_declaration_line)
{
    EXPECT_TRUE(is_material_declaration_line("Mat(0) = {1,2,3}"));
    EXPECT_FALSE(is_material_declaration_line("Mat(0)"));
    EXPECT_FALSE(is_material_declaration_line("Pitch(-90)"));
}

TEST(LSystemMaterialParse, mat0_mat7_mat1_in_order)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(0) = {1.0, 0.0, 0.0, 0.0}", definitions));
    ASSERT_TRUE(try_parse_material_line("Mat(7) = {0.0, 1.0, 0.0, 0.0}", definitions));
    ASSERT_TRUE(try_parse_material_line("Mat(1) = {0.0, 0.0, 1.0, 0.0}", definitions));
    const MaterialEntry* e0 = find_material(definitions, 0);
    const MaterialEntry* e7 = find_material(definitions, 7);
    const MaterialEntry* e1 = find_material(definitions, 1);
    ASSERT_NE(e0, nullptr);
    ASSERT_NE(e7, nullptr);
    ASSERT_NE(e1, nullptr);
    EXPECT_NEAR(e0->r, 1.f, 1e-5f);
    EXPECT_NEAR(e7->g, 1.f, 1e-5f);
    EXPECT_NEAR(e1->b, 1.f, 1e-5f);
    EXPECT_FALSE(is_defined(definitions, 2));
}
