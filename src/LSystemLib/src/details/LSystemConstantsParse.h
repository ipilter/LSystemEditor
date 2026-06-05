#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/**
 * @brief True if `trimmed_line` is `name = number` (not `Mat(id) = { ... }`).
 */
[[nodiscard]] bool is_global_constant_line(std::string_view trimmed_line);

/**
 * @brief Parses one global constant line. Throws std::runtime_error on syntax errors.
 * @return true if the line was a constant declaration.
 */
[[nodiscard]] bool try_parse_constant_line(std::string_view trimmed_line,
    std::unordered_map<std::string, double>& constants);

/** @brief Scans all lines and merges constant declarations. */
void parse_constants_from_lines(const std::vector<std::string_view>& lines,
    std::unordered_map<std::string, double>& constants);
