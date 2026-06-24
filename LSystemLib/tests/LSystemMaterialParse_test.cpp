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

TEST(LSystemMaterialParse, parses_named_albedo_rgb)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(0) = {albedo: {0.72, 0.68, 0.58}}", definitions));
    const MaterialEntry* e = find_material(definitions, "0");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(materialChannelR(e->albedo), 0.72f, 1e-5f);
    EXPECT_NEAR(materialChannelG(e->albedo), 0.68f, 1e-5f);
    EXPECT_NEAR(materialChannelB(e->albedo), 0.58f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(e->roughness, 0.5f), 0.5f, 1e-5f);
}

TEST(LSystemMaterialParse, rejects_positional_tuple)
{
    std::vector<MaterialDefinition> definitions;
    EXPECT_THROW(
        {
            const bool ignored = try_parse_material_line("Mat(0) = {0.72, 0.68, 0.58}", definitions);
            (void)ignored;
        },
        std::runtime_error);
}

TEST(LSystemMaterialParse, rejects_unknown_property)
{
    std::vector<MaterialDefinition> definitions;
    EXPECT_THROW(
        {
            const bool ignored = try_parse_material_line("Mat(0) = {thin: 1}", definitions);
            (void)ignored;
        },
        std::runtime_error);
    EXPECT_THROW(
        {
            const bool ignored = try_parse_material_line("Mat(0) = {transmission: 1}", definitions);
            (void)ignored;
        },
        std::runtime_error);
}

TEST(LSystemMaterialParse, parses_subsurface_material)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line(
        "Mat(Wax) = {type: Subsurface, albedo: {0.9, 0.95, 0.2}, roughness: 0.8, subsurface: 1, "
        "subsurfaceRadius: 2.0, ior: 1.5, specular: 0.2}",
        definitions));
    const MaterialEntry* e = find_material(definitions, "Wax");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->typeName, "Subsurface");
    EXPECT_NEAR(materialChannelScalar(e->subsurface), 1.f, 1e-5f);
    EXPECT_NEAR(materialChannelR(e->subsurfaceRadius), 2.f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(e->specular, 1.f), 0.2f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_glass_type)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(Glass) = {type: Glass, albedo: 1, roughness: 0, ior: 1.42}", definitions));
    const MaterialEntry* e = find_material(definitions, "Glass");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->typeName, "Glass");
    EXPECT_NEAR(materialChannelScalar(e->ior, 1.5f), 1.42f, 1e-5f);
}

TEST(LSystemMaterialParse, rejects_deprecated_sigmaS)
{
    std::vector<MaterialDefinition> definitions;
    EXPECT_THROW(
        {
            const bool ignored = try_parse_material_line(
                "Mat(0) = {sigmaS: {0.3, 0.25, 0.15}}", definitions);
            (void)ignored;
        },
        std::runtime_error);
}

TEST(LSystemMaterialParse, parses_empty_block_defaults)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(Default) = {}", definitions));
    const MaterialEntry* e = find_material(definitions, "Default");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(materialChannelR(e->albedo), 0.8f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(e->roughness, 0.5f), 0.5f, 1e-5f);
    EXPECT_EQ(e->typeName, "Opaque");
}

TEST(LSystemMaterialParse, surface_only_material_defaults_to_opaque)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line(
        "Mat(Diffuse) = {albedo: {0.9, 0.9, 0.9}, roughness: 0.9}",
        definitions));
    const MaterialEntry* e = find_material(definitions, "Diffuse");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->typeName, "Opaque");
}

TEST(LSystemMaterialParse, parses_metal_preset)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line(
        "Mat(Metal) = {albedo: {1.0, 0.85, 0.3}, roughness: 0.15, metallic: 1, ior: 1.5}",
        definitions));
    const MaterialEntry* e = find_material(definitions, "Metal");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(materialChannelScalar(e->metallic), 1.f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_leaf_subsurface_preset)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line(
        "Mat(Leaf) = {type: Subsurface, albedo: {0.2, 0.8, 0.1}, roughness: 0.5, specular: 1.0, "
        "subsurface: 0.6, subsurfaceRadius: {1.5, 3.0, 0.8}, ior: 1.5}",
        definitions));
    const MaterialEntry* e = find_material(definitions, "Leaf");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->typeName, "Subsurface");
    EXPECT_NEAR(materialChannelR(e->subsurfaceRadius), 1.5f, 1e-5f);
    EXPECT_NEAR(materialChannelG(e->subsurfaceRadius), 3.0f, 1e-5f);
    EXPECT_NEAR(materialChannelB(e->subsurfaceRadius), 0.8f, 1e-5f);
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
}

TEST(LSystemMaterialParse, duplicate_id_throws)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(1) = {albedo: {0.1, 0.2, 0.3}}", definitions));
    EXPECT_THROW(
        {
            const bool ignored = try_parse_material_line("Mat(1) = {albedo: {0.4, 0.5, 0.6}}", definitions);
            (void)ignored;
        },
        std::runtime_error);
}

TEST(LSystemMaterialParse, is_material_declaration_line)
{
    EXPECT_TRUE(is_material_declaration_line("Mat(0) = {albedo: {1,2,3}}"));
    EXPECT_FALSE(is_material_declaration_line("Mat(0) = Diffuse {1,2,3}"));
    EXPECT_FALSE(is_material_declaration_line("Mat(0)"));
}

TEST(LSystemMaterialParse, lsystem_parse_collects_parametric_materials)
{
    LSystem ls;
    ls.parse("Mat(0) = {albedo: {0.72, 0.68, 0.58}}\n"
             "Mat(0)\n"
             "F(1)\n"
             "A -> B\n");
    const MaterialEntry* e = find_material(ls.material_definitions(), "0");
    ASSERT_NE(e, nullptr);
    EXPECT_NEAR(materialChannelR(e->albedo), 0.72f, 1e-5f);
    ASSERT_EQ(ls.axiom_modules().size(), 2u);
    EXPECT_EQ(ls.axiom_modules()[0].name, "Mat");
}

TEST(LSystemMaterialParse, parses_grid_albedo_texture)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line(
        "Mat(GridMat) = {albedo: {Grid, on: {1,0,0}, off: {0,1,0}, freq: 8, thickness: 0.05}, roughness: 0.5}",
        definitions));
    const MaterialEntry* entry = find_material(definitions, "GridMat");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->albedo.mode, MaterialChannel::Mode::Texture);
    EXPECT_EQ(entry->albedo.texture.kind, "Grid");
}

TEST(LSystemMaterialParse, parses_named_emission_texture)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line(
        "Mat(Light) = {albedo: {0.9, 0.8, 0.7}, emission: {Stripe, freq:8, thickness:0.05, on:10, off:0}}",
        definitions));
    const MaterialEntry* e = find_material(definitions, "Light");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->emission.mode, MaterialChannel::Mode::Texture);
    EXPECT_EQ(e->emission.texture.kind, "Stripe");
}

TEST(LSystemMaterialParse, parses_grid_freqV_only)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line(
        "Mat(GridMat) = {albedo: {Grid, on: {1,0,0}, off: {0,1,0}, freqV: 400, thickness: 0.05}, roughness: 0.5}",
        definitions));
    const MaterialEntry* entry = find_material(definitions, "GridMat");
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->albedo.texture.params.size(), 11u);
    EXPECT_NEAR(entry->albedo.texture.params[8], 400.f, 1e-5f);
    EXPECT_NEAR(entry->albedo.texture.params[9], 400.f, 1e-5f);
    EXPECT_NEAR(entry->albedo.texture.params[10], 0.05f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_abbe_and_defaults)
{
    std::vector<MaterialDefinition> definitions;
    ASSERT_TRUE(try_parse_material_line("Mat(Glass) = {type: Glass, ior: 1.42, abbe: 59}", definitions));
    const MaterialEntry* glass = find_material(definitions, "Glass");
    ASSERT_NE(glass, nullptr);
    EXPECT_NEAR(materialChannelScalar(glass->ior, 1.5f), 1.42f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(glass->abbe, 58.f), 59.f, 1e-5f);

    definitions.clear();
    ASSERT_TRUE(try_parse_material_line("Mat(Default) = {albedo: 0.8}", definitions));
    const MaterialEntry* def = find_material(definitions, "Default");
    ASSERT_NE(def, nullptr);
    EXPECT_NEAR(materialChannelScalar(def->abbe, 58.f), 58.f, 1e-5f);
}

TEST(LSystemMaterialParse, parses_multiline_stem_subsurface_block)
{
    const char* text =
        "Mat(Stem) = {\n"
        "  albedo: {0.2, 0.5, 0.07},\n"
        "  subsurface: {\n"
        "    weight: 1.0,\n"
        "    scatterR: 0.01, scatterG: 0.1, scatterB: 0.02,\n"
        "    scatterScale: 0.005,\n"
        "    anisotropy: 0.0\n"
        "  }\n"
        "}\n";

    std::vector<std::string_view> lines;
    std::string storage(text);
    std::size_t start = 0;
    while (start < storage.size()) {
        const std::size_t nl = storage.find('\n', start);
        if (nl == std::string::npos) {
            lines.emplace_back(storage.data() + start, storage.size() - start);
            break;
        }
        lines.emplace_back(storage.data() + start, nl - start);
        start = nl + 1;
    }

    std::vector<MaterialDefinition> definitions;
    parse_materials_from_lines(lines, definitions);
    const MaterialEntry* entry = find_material(definitions, "Stem");
    ASSERT_NE(entry, nullptr);
    EXPECT_NEAR(materialChannelR(entry->albedo), 0.2f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(entry->subsurface), 1.0f, 1e-5f);
    EXPECT_NEAR(materialChannelR(entry->subsurfaceRadius), 0.01f, 1e-5f);
    EXPECT_NEAR(materialChannelG(entry->subsurfaceRadius), 0.1f, 1e-5f);
    EXPECT_NEAR(materialChannelB(entry->subsurfaceRadius), 0.02f, 1e-5f);
    EXPECT_NEAR(materialChannelScalar(entry->subsurfaceScatterScale), 0.005f, 1e-5f);
    EXPECT_EQ(entry->typeName, "Opaque");
}

TEST(LSystemMaterialParse, multiline_block_ignores_inline_comments)
{
    const char* text =
        "Mat(Leaf) = {\n"
        "  albedo: {0.2, 0.8, 0.1},  # leaf tint\n"
        "  # whole-line comment\n"
        "  subsurface: {weight: 0.8, scatterR: 1.5, scatterG: 3.0, scatterB: 0.8}\n"
        "}\n";

    std::vector<std::string_view> lines;
    std::string storage(text);
    std::size_t start = 0;
    while (start < storage.size()) {
        const std::size_t nl = storage.find('\n', start);
        if (nl == std::string::npos) {
            lines.emplace_back(storage.data() + start, storage.size() - start);
            break;
        }
        lines.emplace_back(storage.data() + start, nl - start);
        start = nl + 1;
    }

    std::vector<MaterialDefinition> definitions;
    parse_materials_from_lines(lines, definitions);
    const MaterialEntry* entry = find_material(definitions, "Leaf");
    ASSERT_NE(entry, nullptr);
    EXPECT_NEAR(materialChannelScalar(entry->subsurface), 0.8f, 1e-5f);
}

TEST(LSystemMaterialParse, is_line_skipped_for_axiom_multiline)
{
    const char* text =
        "Mat(A) = {\n"
        "  albedo: 0.5\n"
        "}\n"
        "F(1)\n";

    std::vector<std::string_view> lines;
    std::string storage(text);
    std::size_t start = 0;
    while (start < storage.size()) {
        const std::size_t nl = storage.find('\n', start);
        if (nl == std::string::npos) {
            lines.emplace_back(storage.data() + start, storage.size() - start);
            break;
        }
        lines.emplace_back(storage.data() + start, nl - start);
        start = nl + 1;
    }

    EXPECT_TRUE(is_line_skipped_for_axiom(lines, 0));
    EXPECT_TRUE(is_line_skipped_for_axiom(lines, 1));
    EXPECT_TRUE(is_line_skipped_for_axiom(lines, 2));
    EXPECT_FALSE(is_line_skipped_for_axiom(lines, 3));
}
