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
    EXPECT_NEAR(materialChannelR(e->albedo), 0.72f, 1e-5f);
    EXPECT_NEAR(materialChannelG(e->albedo), 0.68f, 1e-5f);
    EXPECT_NEAR(materialChannelB(e->albedo), 0.58f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(e->roughness, 0.5f), 0.5f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(e->metallic), 0.f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(e->transmission), 0.f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_no_spaces_around_equals)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(0)={1,0,0}", definitions));
    const MaterialEntry* e = find_material(definitions, "0");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(materialChannelR(e->albedo), 1.f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_full_parametric_line)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line(
        "Mat(1) = {0.9, 0.9, 0.9, 0.15, 1.0, 0.0, 0.0, 1.5, 0.0, 1.5}",
        definitions));
    const MaterialEntry* e = find_material(definitions, "1");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(materialChannelScalar(e->metallic), 1.f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(e->roughness, 0.5f), 0.15f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(e->emission), 1.5f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_transmissive_params)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(2) = {1, 1, 1, 0, 0, 0.95, 0, 1.5, 0, 0}", definitions));
    const MaterialEntry* e = find_material(definitions, "2");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(materialChannelScalar(e->transmission), 0.95f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(e->thin), 0.f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_thin_transmissive_params)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(3) = {0.2, 0.8, 0.1, 0.5, 0, 1, 1, 1.5, 0, 0}", definitions));
    const MaterialEntry* e = find_material(definitions, "3");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(materialChannelScalar(e->transmission), 1.f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(e->thin), 1.f, 1e-5f);
}

TEST(LSystemMaterialParse, clamps_transmission_above_one)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(7) = {1, 1, 1, 0, 0, 1.5, 0, 1.5, 0, 0}", definitions));
    const MaterialEntry* e = find_material(definitions, "7");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(materialChannelScalar(e->transmission), 1.0f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_subsurface_params)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(8) = {0.9, 0.85, 0.7, 0.9, 0, 0, 0, 1.5, 0.8, 0}", definitions));
    const MaterialEntry* e = find_material(definitions, "8");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(materialChannelScalar(e->subsurface), 0.8f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(e->roughness, 0.5f), 0.9f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_extended_v3_params)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line(
        "Mat(Wax) = {0.9, 0.85, 0.7, 0.9, 0, 0, 0, 1.5, 0.8, 0, 0.7, 0.05, 0.04, 0.02, 1.0}",
        definitions));
    const MaterialEntry* e = find_material(definitions, "Wax");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(materialChannelScalar(e->subsurface), 0.8f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(e->diffuseRoughness), 0.7f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(e->scatterRadiusR), 0.05f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(e->scatterRadiusG), 0.04f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(e->scatterRadiusB), 0.02f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(e->specular, 1.f), 1.f, 1e-5f);
}

TEST(LSystemMaterialParse, default_diffuse_roughness_matches_roughness)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(0) = {0.5, 0.5, 0.5, 0.3}", definitions));
    const MaterialEntry* e = find_material(definitions, "0");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(materialChannelScalar(e->diffuseRoughness, -1.f), -1.f, 1e-5f);
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
            const bool ignored = try_parse_material_line("Mat(1) = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}", definitions);
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
    EXPECT_NEAR(materialChannelR(e->albedo), 0.72f, 1e-5f);
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
    EXPECT_NEAR(materialChannelR(e0->albedo), 1.f, 1e-5f);
    EXPECT_NEAR(materialChannelG(e7->albedo), 1.f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(e1->metallic), 1.f, 1e-5f);
    EXPECT_NEAR(materialChannelB(e1->albedo), 1.f, 1e-5f);
    EXPECT_FALSE(is_defined(definitions, "2"));
}

TEST(LSystemMaterialParse, parses_named_material_id)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(Glass) = {1, 1, 1, 0, 0, 0.95, 0, 1.5, 0, 0}", definitions));
    const MaterialEntry* glass = find_material(definitions, "Glass");
    ASSERT_NE(glass, nullptr);
    EXPECT_NEAR(materialChannelScalar(glass->transmission), 0.95f, 1e-5f);

    ASSERT_TRUE(try_parse_material_line("Mat(GreenPlastic) = {0.2, 0.8, 0.1, 0.5}", definitions));
    const MaterialEntry* plastic = find_material(definitions, "GreenPlastic");
    ASSERT_NE(plastic, nullptr);
    EXPECT_NEAR(materialChannelG(plastic->albedo), 0.8f, 1e-5f);
}

TEST(LSystemMaterialParse, is_material_declaration_line_named_id)
{
    EXPECT_TRUE(is_material_declaration_line("Mat(Glass) = {1,2,3}"));
    EXPECT_TRUE(is_material_declaration_line("Mat(GreenPlastic) = {1,2,3}"));
}

TEST(LSystemMaterialParse, parses_grid_albedo_texture)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line(
        "Mat(GridMat) = {{Grid, 1,0,0, 0,1,0, 8, 0.05}, 0.5, 0, 0, 0, 1.5, 0, 0}",
        definitions));
    const MaterialEntry* entry = find_material(definitions, "GridMat");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->albedo.mode, MaterialChannel::Mode::Texture);
    EXPECT_EQ(entry->albedo.texture.kind, "Grid");
    ASSERT_EQ(entry->albedo.texture.params.size(), 8u);
    EXPECT_NEAR(entry->albedo.texture.params[6], 8.f, 1e-5f);
    EXPECT_NEAR(entry->albedo.texture.params[7], 0.05f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(entry->roughness, 0.5f), 0.5f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_grid_split_frequency_texture)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line(
        "Mat(GridSplit) = {{Grid, 0.85,0.85,0.85, 0.05,0.05,0.05, 48, 0.2, 0.015}, 0.6}",
        definitions));
    const MaterialEntry* entry = find_material(definitions, "GridSplit");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->albedo.texture.kind, "Grid");
    ASSERT_EQ(entry->albedo.texture.params.size(), 9u);
    EXPECT_NEAR(entry->albedo.texture.params[6], 48.f, 1e-5f);
    EXPECT_NEAR(entry->albedo.texture.params[7], 0.2f, 1e-5f);
    EXPECT_NEAR(entry->albedo.texture.params[8], 0.015f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_stripe_transmission_texture)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line(
        "Mat(Striped) = {0.8, 0.8, 0.8, 0.5, 0, {Stripe, 10, 0.1}, 0, 1.5, 0, 0}",
        definitions));
    const MaterialEntry* entry = find_material(definitions, "Striped");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->transmission.mode, MaterialChannel::Mode::Texture);
    EXPECT_EQ(entry->transmission.texture.kind, "Stripe");
    ASSERT_EQ(entry->transmission.texture.params.size(), 2u);
    EXPECT_NEAR(entry->transmission.texture.params[0], 10.f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(entry->transmission), 0.f, 1e-5f);
}

TEST(LSystemMaterialParse, rejects_unknown_texture_kind)
{
    std::vector<MaterialDefinition> definitions;
    EXPECT_THROW(
        {
            const bool ignored = try_parse_material_line(
                "Mat(0) = {{Checker, 1, 0, 0, 0, 1, 0}, 0.5}",
                definitions);
            (void)ignored;
        },
        std::runtime_error);
}

TEST(LSystemMaterialParse, parses_grid_emission_texture)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line(
        "Mat(GridLamp) = {0.1, 0.1, 0.1, 0.4, 0, 0, 0, 1.5, 0, {Grid, 1,1,1, 1,1,1, 24, 24, 0.02}}",
        definitions));
    const MaterialEntry* entry = find_material(definitions, "GridLamp");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->emission.mode, MaterialChannel::Mode::Texture);
    EXPECT_EQ(entry->emission.texture.kind, "Grid");
    EXPECT_NEAR(materialChannelScalar(entry->emission), 0.0f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_noise_roughness_texture)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line(
        "Mat(NoisyPlastic) = {0.9, 0.7, 0.94, {Noise, 16, 3, 42, 0.2, 0.9}}",
        definitions));
    const MaterialEntry* entry = find_material(definitions, "NoisyPlastic");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->roughness.mode, MaterialChannel::Mode::Texture);
    EXPECT_EQ(entry->roughness.texture.kind, "Noise");
    ASSERT_EQ(entry->roughness.texture.params.size(), 5u);
    EXPECT_NEAR(entry->roughness.texture.params[0], 16.f, 1e-5f);
    EXPECT_NEAR(entry->roughness.texture.params[4], 0.9f, 1e-5f);
}

TEST(LSystemMaterialParse, rejects_invalid_noise_params)
{
    std::vector<MaterialDefinition> definitions;
    EXPECT_THROW(
        {
            const bool ignored = try_parse_material_line(
                "Mat(0) = {0.5, {Noise}}",
                definitions);
            (void)ignored;
        },
        std::runtime_error);
}
