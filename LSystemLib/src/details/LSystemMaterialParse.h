#pragma once

#include "LSystemMaterials.h"

#include <string_view>
#include <vector>

/**
 * @brief True if `trimmed_line` (no leading/trailing ws, comment stripped) is `Mat(id) = {`.
 * Block may continue on subsequent lines.
 */
[[nodiscard]] bool is_material_declaration_start_line(std::string_view trimmed_line);

/**
 * @brief True if `trimmed_line` is a complete single-line `Mat(id) = { ... }` declaration.
 * Multi-line blocks are only fully parsed via `parse_materials_from_lines`.
 */
[[nodiscard]] bool is_material_declaration_line(std::string_view trimmed_line);

/**
 * @brief True if line `index` is part of a material declaration (start, continuation, or closing).
 */
[[nodiscard]] bool is_line_skipped_for_axiom(
    const std::vector<std::string_view>& lines,
    std::size_t index);

/**
 * @brief Parses one complete `Mat(id) = { ... }` declaration into `definitions`.
 * @return true if the text was a material declaration.
 */
[[nodiscard]] bool try_parse_material_line(
    std::string_view trimmed_line,
    std::vector<MaterialDefinition>& definitions);

/** @brief Scans all lines and merges material declarations into `definitions`. */
void parse_materials_from_lines(
    const std::vector<std::string_view>& lines,
    std::vector<MaterialDefinition>& definitions);
