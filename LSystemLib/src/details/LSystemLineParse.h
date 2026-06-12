#pragma once

#include <string_view>

/** @brief True if the first non-whitespace character in @a raw is `#` (full-line comment). */
[[nodiscard]] bool is_hash_comment_line(std::string_view raw) noexcept;
