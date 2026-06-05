#include "LSystemLineParse.h"

#include <cctype>

bool is_hash_comment_line(const std::string_view raw) noexcept
{
    for (const char c : raw)
    {
        if (!std::isspace(static_cast<unsigned char>(c)))
        {
            return c == '#';
        }
    }
    return false;
}
