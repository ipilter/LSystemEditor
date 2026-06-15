#pragma once

#include "LSystemMaterials.h"

#include <string_view>
#include <vector>

/**
 * @brief True if `trimmed_line` (no leading/trailing ws, comment stripped) is `Mat(id) = Type { ... }`.
 */
[[nodiscard]] bool is_material_declaration_line(std::string_view trimmed_line);

/**
 * @brief Parses one `Mat(id) = Type { ... }` line into `definitions`. Throws std::runtime_error on syntax errors.
 * @return true if the line was a material declaration.
 */
[[nodiscard]] bool try_parse_material_line(std::string_view trimmed_line,
    std::vector<MaterialDefinition>& definitions);

/** @brief Scans all lines and merges material declarations into `definitions`. */
void parse_materials_from_lines(const std::vector<std::string_view>& lines,
    std::vector<MaterialDefinition>& definitions);
