#include "EncodingHelpers.hpp"

namespace Helpers
{
    std::string EncodingHelpers::Latin1ToUtf8(const uint8_t *data, size_t len)
    {
        std::string result;
        result.reserve(len * 2); // worst case: every byte becomes 2 UTF-8 bytes
        for (size_t i = 0; i < len; i++)
        {
            uint8_t ch = data[i];
            if (ch == 0) // null terminator in source data
                break;
            if (ch < 0x80)
                result += static_cast<char>(ch);
            else
            {
                result += static_cast<char>(0xC0 | (ch >> 6));
                result += static_cast<char>(0x80 | (ch & 0x3F));
            }
        }
        return result;
    }

    std::string EncodingHelpers::Utf8ToLatin1(const std::string &utf8)
    {
        std::string result;
        result.reserve(utf8.size());
        for (size_t i = 0; i < utf8.size(); i++)
        {
            uint8_t ch = static_cast<uint8_t>(utf8[i]);
            if (ch < 0x80)
            {
                result += static_cast<char>(ch);
            }
            else if ((ch & 0xE0) == 0xC0 && i + 1 < utf8.size())
            {
                // 2-byte UTF-8 sequence: 110xxxxx 10xxxxxx
                uint16_t codepoint = ((ch & 0x1F) << 6) | (static_cast<uint8_t>(utf8[i + 1]) & 0x3F);
                if (codepoint <= 0xFF)
                    result += static_cast<char>(codepoint);
                else
                    result += '?'; // outside Latin-1 range
                i++; // skip continuation byte
            }
            else if ((ch & 0xF0) == 0xE0)
            {
                result += '?'; // 3-byte UTF-8, outside Latin-1
                i += 2;
            }
            else if ((ch & 0xF8) == 0xF0)
            {
                result += '?'; // 4-byte UTF-8, outside Latin-1
                i += 3;
            }
            // else: stray continuation byte, skip
        }
        return result;
    }
}
