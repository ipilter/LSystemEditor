#include "LSystem.h"
#include "LSystemMaterialParse.h"

#include <gtest/gtest.h>

#include <string>

namespace
{

const MaterialEntry* find_material(const std::vector<MaterialDefinition>& definitions, const std::string& id)
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

bool is_defined(const std::vector<MaterialDefinition>& definitions, const std::string& id)
{
    return find_material(definitions, id) != nullptr;
}

} // namespace

TEST(LSystemMaterialParse, parses_rgb_only)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(0) = {0.72, 0.68, 0.58}", definitions));
    const MaterialEntry* e = find_material(definitions, "0");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(e->r, 0.72f, 1e-5f);
    EXPECT_NEAR(e->g, 0.68f, 1e-5f);
    EXPECT_NEAR(e->b, 0.58f, 1e-5f);
    EXPECT_NEAR(e->roughness, 0.5f, 1e-5f);
    EXPECT_NEAR(e->metallic, 0.f, 1e-5f);
    EXPECT_NEAR(e->transmission, 0.f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_no_spaces_around_equals)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(0)={1,0,0}", definitions));
    const MaterialEntry* e = find_material(definitions, "0");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(e->r, 1.f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_full_parametric_line)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line(
        "Mat(1) = {0.9, 0.9, 0.9, 0.15, 1.0, 0.0, 0.0, 1.5, 0.0, 1.5}",
        definitions));
    const MaterialEntry* e = find_material(definitions, "1");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(e->metallic, 1.f, 1e-5f);
    EXPECT_NEAR(e->roughness, 0.15f, 1e-5f);
    EXPECT_NEAR(e->emission, 1.5f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_transmissive_params)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(2) = {1, 1, 1, 0, 0, 0.95, 0, 1.5, 0, 0}", definitions));
    const MaterialEntry* e = find_material(definitions, "2");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(e->transmission, 0.95f, 1e-5f);
    EXPECT_NEAR(e->thin, 0.f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_thin_transmissive_params)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(3) = {0.2, 0.8, 0.1, 0.5, 0, 1, 1, 1.5, 0, 0}", definitions));
    const MaterialEntry* e = find_material(definitions, "3");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(e->transmission, 1.f, 1e-5f);
    EXPECT_NEAR(e->thin, 1.f, 1e-5f);
}

TEST(LSystemMaterialParse, clamps_transmission_above_one)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(7) = {1, 1, 1, 0, 0, 1.5, 0, 1.5, 0, 0}", definitions));
    const MaterialEntry* e = find_material(definitions, "7");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(e->transmission, 1.0f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_subsurface_params)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(8) = {0.9, 0.85, 0.7, 0.9, 0, 0, 0, 1.5, 0.8, 0}", definitions));
    const MaterialEntry* e = find_material(definitions, "8");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(e->subsurface, 0.8f, 1e-5f);
    EXPECT_NEAR(e->roughness, 0.9f, 1e-5f);
}

TEST(LSystemMaterialParse, rejects_typed_syntax)
{
    std::vector<MaterialDefinition> definitions;
    EXPECT_THROW(
        {
            const bool ignored = try_parse_material_line("Mat(0) = Diffuse {1, 2, 3}", definitions);
            (void)ignored;
        },
        std::runtime_error);
    EXPECT_FALSE(is_material_declaration_line("Mat(0) = Diffuse {1,2,3}"));
}

TEST(LSystemMaterialParse, rejects_unknown_type)
{
    std::vector<MaterialDefinition> definitions;
    EXPECT_THROW(
        {
            const bool ignored = try_parse_material_line("Mat(0) = Wax {1, 1, 1, 0.1}", definitions);
            (void)ignored;
        },
        std::runtime_error);
}

TEST(LSystemMaterialParse, rejects_wrong_arity)
{
    std::vector<MaterialDefinition> definitions;
    EXPECT_THROW(
        {
            const bool ignored = try_parse_material_line("Mat(0) = {1, 1}", definitions);
            (void)ignored;
        },
        std::runtime_error);
    EXPECT_THROW(
        {
            const bool ignored = try_parse_material_line("Mat(1) = {1,1,1,1,1,1,1,1,1,1,1}", definitions);
            (void)ignored;
        },
        std::runtime_error);
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

TEST(LSystemMaterialParse, lsystem_parse_collects_parametric_materials)
{
    LSystem ls;
    ls.parse("Mat(0) = {0.72, 0.68, 0.58}\n"
             "Mat(0)\n"
             "F(1)\n"
             "A -> B\n");
    const MaterialEntry* e = find_material(ls.material_definitions(), "0");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(e->r, 0.72f, 1e-5f);
    ASSERT_EQ(ls.axiom_modules().size(), 2u);
    EXPECT_EQ(ls.axiom_modules()[0].name, "Mat");
    EXPECT_EQ(ls.axiom_modules()[1].name, "F");
}

TEST(LSystemMaterialParse, is_material_declaration_line)
{
    EXPECT_TRUE(is_material_declaration_line("Mat(0) = {1,2,3}"));
    EXPECT_FALSE(is_material_declaration_line("Mat(0) = Diffuse {1,2,3}"));
    EXPECT_FALSE(is_material_declaration_line("Mat(0)"));
    EXPECT_FALSE(is_material_declaration_line("Pitch(-90)"));
}

TEST(LSystemMaterialParse, mat0_mat7_mat1_in_order)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(0) = {1.0, 0.0, 0.0, 0.0}", definitions));
    ASSERT_TRUE(try_parse_material_line("Mat(7) = {0.0, 1.0, 0.0, 0.0}", definitions));
    ASSERT_TRUE(try_parse_material_line("Mat(1) = {0.0, 0.0, 1.0, 0.0, 1.0}", definitions));
    const MaterialEntry* e0 = find_material(definitions, "0");
    const MaterialEntry* e7 = find_material(definitions, "7");
    const MaterialEntry* e1 = find_material(definitions, "1");
    ASSERT_NE(e0, nullptr);
    ASSERT_NE(e7, nullptr);
    ASSERT_NE(e1, nullptr);
    EXPECT_NEAR(e0->r, 1.f, 1e-5f);
    EXPECT_NEAR(e7->g, 1.f, 1e-5f);
    EXPECT_NEAR(e1->metallic, 1.f, 1e-5f);
    EXPECT_NEAR(e1->b, 1.f, 1e-5f);
    EXPECT_FALSE(is_defined(definitions, "2"));
}

TEST(LSystemMaterialParse, parses_named_material_id)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(Glass) = {1, 1, 1, 0, 0, 0.95, 0, 1.5, 0, 0}", definitions));
    const MaterialEntry* glass = find_material(definitions, "Glass");
    ASSERT_NE(glass, nullptr);
    EXPECT_NEAR(glass->transmission, 0.95f, 1e-5f);

    ASSERT_TRUE(try_parse_material_line("Mat(GreenPlastic) = {0.2, 0.8, 0.1, 0.5}", definitions));
    const MaterialEntry* plastic = find_material(definitions, "GreenPlastic");
    ASSERT_NE(plastic, nullptr);
    EXPECT_NEAR(plastic->g, 0.8f, 1e-5f);
}

TEST(LSystemMaterialParse, is_material_declaration_line_named_id)
{
    EXPECT_TRUE(is_material_declaration_line("Mat(Glass) = {1,2,3}"));
    EXPECT_TRUE(is_material_declaration_line("Mat(GreenPlastic) = {1,2,3}"));
}
