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

TEST(LSystemMaterialParse, parses_diffuse_typed)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(0) = Diffuse {0.72, 0.68, 0.58, 0.85}", definitions));
    const MaterialEntry* e = find_material(definitions, 0);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->kind, MaterialKind::Diffuse);
    EXPECT_NEAR(e->r, 0.72f, 1e-5f);
    EXPECT_NEAR(e->g, 0.68f, 1e-5f);
    EXPECT_NEAR(e->b, 0.58f, 1e-5f);
    EXPECT_NEAR(e->roughness, 0.85f, 1e-5f);
    EXPECT_NEAR(e->metallic, 0.f, 1e-5f);
    EXPECT_NEAR(e->transmission, 0.f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_no_spaces_around_equals)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(0)=Diffuse{1, 0, 0}", definitions));
    const MaterialEntry* e = find_material(definitions, 0);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->kind, MaterialKind::Diffuse);
    EXPECT_NEAR(e->r, 1.f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_metal_typed)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(1) = Metal {0.9, 0.9, 0.9, 0.15}", definitions));
    const MaterialEntry* e = find_material(definitions, 1);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->kind, MaterialKind::Metal);
    EXPECT_NEAR(e->metallic, 1.f, 1e-5f);
    EXPECT_NEAR(e->transmission, 0.f, 1e-5f);
    EXPECT_NEAR(e->roughness, 0.15f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_glass_typed)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(2) = Glass {1, 1, 1, 1.5}", definitions));
    const MaterialEntry* e = find_material(definitions, 2);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->kind, MaterialKind::Glass);
    EXPECT_NEAR(e->ior, 1.5f, 1e-5f);
    EXPECT_NEAR(e->transmission, 1.f, 1e-5f);
    EXPECT_NEAR(e->metallic, 0.f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_transparent_alias)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(3) = Transparent {0.9, 0.95, 1.0, 1.45}", definitions));
    const MaterialEntry* e = find_material(definitions, 3);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->kind, MaterialKind::Glass);
    EXPECT_NEAR(e->ior, 1.45f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_glass_with_roughness)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(5) = Glass {0.95, 0.98, 1.00, 1.45, 0.02}", definitions));
    const MaterialEntry* e = find_material(definitions, 5);
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(e->roughness, 0.02f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_emission_on_typed)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(4) = Diffuse {0.2, 0.3, 0.4, 0.5, 2.0}", definitions));
    const MaterialEntry* e = find_material(definitions, 4);
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(e->emission, 2.0f, 1e-5f);
    ASSERT_TRUE(try_parse_material_line("Mat(6) = Metal {0.9, 0.9, 0.9, 0.15, 1.5}", definitions));
    const MaterialEntry* metal = find_material(definitions, 6);
    ASSERT_NE(metal, nullptr);
    EXPECT_NEAR(metal->emission, 1.5f, 1e-5f);
}

TEST(LSystemMaterialParse, rejects_numeric_only_syntax)
{
    std::vector<MaterialDefinition> definitions;
    EXPECT_THROW(
        {
            const bool ignored = try_parse_material_line("Mat(0) = {1, 2, 3}", definitions);
            (void)ignored;
        },
        std::runtime_error);
    EXPECT_FALSE(is_material_declaration_line("Mat(0) = {1,2,3}"));
}

TEST(LSystemMaterialParse, rejects_unknown_type)
{
    std::vector<MaterialDefinition> definitions;
    EXPECT_THROW(
        {
            const bool ignored = try_parse_material_line("Mat(0) = Wood {1, 1, 1}", definitions);
            (void)ignored;
        },
        std::runtime_error);
}

TEST(LSystemMaterialParse, rejects_unimplemented_sss)
{
    std::vector<MaterialDefinition> definitions;
    EXPECT_THROW(
        {
            const bool ignored = try_parse_material_line("Mat(0) = SSS {1, 1, 1, 0.1}", definitions);
            (void)ignored;
        },
        std::runtime_error);
}

TEST(LSystemMaterialParse, rejects_wrong_arity)
{
    std::vector<MaterialDefinition> definitions;
    EXPECT_THROW(
        {
            const bool ignored = try_parse_material_line("Mat(0) = Metal {1, 1, 1, 1, 1, 1}", definitions);
            (void)ignored;
        },
        std::runtime_error);
    EXPECT_THROW(
        {
            const bool ignored = try_parse_material_line("Mat(1) = Glass {1, 1, 1}", definitions);
            (void)ignored;
        },
        std::runtime_error);
}

TEST(LSystemMaterialParse, case_insensitive_type)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(0) = metal {0.5, 0.5, 0.5, 0.2}", definitions));
    ASSERT_TRUE(try_parse_material_line("Mat(1) = GLASS {1, 1, 1, 1.5}", definitions));
    EXPECT_EQ(find_material(definitions, 0)->kind, MaterialKind::Metal);
    EXPECT_EQ(find_material(definitions, 1)->kind, MaterialKind::Glass);
}

TEST(LSystemMaterialParse, duplicate_id_throws)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(1) = Diffuse {0.1, 0.2, 0.3}", definitions));
    EXPECT_THROW(
        {
            const bool ignored = try_parse_material_line("Mat(1) = Diffuse {0.4, 0.5, 0.6}", definitions);
            (void)ignored;
        },
        std::runtime_error);
}

TEST(LSystemMaterialParse, rejects_old_matn_syntax)
{
    std::vector<MaterialDefinition> definitions;
    EXPECT_FALSE(try_parse_material_line("Mat0 = Diffuse {1, 2, 3}", definitions));
    EXPECT_FALSE(is_material_declaration_line("Mat0 = Diffuse {1,2,3}"));
}

TEST(LSystemMaterialParse, lsystem_parse_collects_typed_materials)
{
    LSystem ls;
    ls.parse("Mat(0) = Diffuse {0.72, 0.68, 0.58}\n"
             "Mat(0)\n"
             "F(1)\n"
             "A -> B\n");
    const MaterialEntry* e = find_material(ls.material_definitions(), 0);
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(e->r, 0.72f, 1e-5f);
    EXPECT_EQ(e->kind, MaterialKind::Diffuse);
    ASSERT_EQ(ls.axiom_modules().size(), 2u);
    EXPECT_EQ(ls.axiom_modules()[0].name, "Mat");
    EXPECT_EQ(ls.axiom_modules()[1].name, "F");
}

TEST(LSystemMaterialParse, is_material_declaration_line)
{
    EXPECT_TRUE(is_material_declaration_line("Mat(0) = Diffuse {1,2,3}"));
    EXPECT_FALSE(is_material_declaration_line("Mat(0) = {1,2,3}"));
    EXPECT_FALSE(is_material_declaration_line("Mat(0)"));
    EXPECT_FALSE(is_material_declaration_line("Pitch(-90)"));
}

TEST(LSystemMaterialParse, mat0_mat7_mat1_in_order)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(0) = Diffuse {1.0, 0.0, 0.0, 0.0}", definitions));
    ASSERT_TRUE(try_parse_material_line("Mat(7) = Diffuse {0.0, 1.0, 0.0, 0.0}", definitions));
    ASSERT_TRUE(try_parse_material_line("Mat(1) = Metal {0.0, 0.0, 1.0, 0.0}", definitions));
    const MaterialEntry* e0 = find_material(definitions, 0);
    const MaterialEntry* e7 = find_material(definitions, 7);
    const MaterialEntry* e1 = find_material(definitions, 1);
    ASSERT_NE(e0, nullptr);
    ASSERT_NE(e7, nullptr);
    ASSERT_NE(e1, nullptr);
    EXPECT_NEAR(e0->r, 1.f, 1e-5f);
    EXPECT_NEAR(e7->g, 1.f, 1e-5f);
    EXPECT_EQ(e1->kind, MaterialKind::Metal);
    EXPECT_NEAR(e1->b, 1.f, 1e-5f);
    EXPECT_FALSE(is_defined(definitions, 2));
}
