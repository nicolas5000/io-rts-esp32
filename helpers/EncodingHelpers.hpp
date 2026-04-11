#pragma once

#include <stdint.h>
#include <string>

namespace Helpers
{
    class EncodingHelpers
    {
    public:
        /// @brief Convert Latin-1 (ISO-8859-1) encoded bytes to a UTF-8 std::string.
        /// IO-Homecontrol devices transmit names in Latin-1 encoding.
        /// @param data Pointer to Latin-1 encoded bytes
        /// @param len Number of bytes to convert
        /// @return UTF-8 encoded std::string
        static std::string Latin1ToUtf8(const uint8_t *data, size_t len);

        /// @brief Convert a UTF-8 std::string to Latin-1 bytes for sending to a device.
        /// Characters outside Latin-1 range (U+0100+) are replaced with '?'.
        /// @param utf8 UTF-8 encoded string
        /// @return Latin-1 encoded std::string
        static std::string Utf8ToLatin1(const std::string &utf8);
    };
}
