// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace lockdown {

// Lockdown messages have a four-byte big-endian body length. One TCP packet
// can contain part of either the prefix or the body; receipt alone is not success.
class QueryTypeReply {
public:
    static constexpr size_t capacity = 8192;
    enum class Result { incomplete, complete, invalid };

    void reset() { used_ = 0; expected_ = 0; invalid_ = false; }

    Result append(const uint8_t *data, size_t size) {
        if (invalid_ || (!data && size) || size > sizeof(bytes_) - used_) {
            invalid_ = true;
            return Result::invalid;
        }
        for (size_t i = 0; i < size; ++i) bytes_[used_ + i] = data[i];
        used_ += size;
        if (used_ < 4) return Result::incomplete;
        expected_ = (uint32_t(bytes_[0]) << 24) | (uint32_t(bytes_[1]) << 16) |
                    (uint32_t(bytes_[2]) << 8) | bytes_[3];
        if (!expected_ || expected_ > capacity || used_ > expected_ + 4) {
            invalid_ = true;
            return Result::invalid;
        }
        return used_ == expected_ + 4 ? Result::complete : Result::incomplete;
    }

    // This diagnostic recognizes the XML QueryType response, rather than
    // accepting arbitrary TCP data as a successful Lockdown exchange.
    bool is_lockdown_service() const {
        if (invalid_ || !expected_ || used_ != expected_ + 4) return false;
        const uint8_t *body = bytes_ + 4;
        if ((find(body, expected_, "<plist>") == expected_ &&
             find(body, expected_, "<plist ") == expected_) ||
            find(body, expected_, "</plist>") == expected_) return false;
        if (find(body, expected_, "<key>Error</key>") != expected_) return false;
        size_t type = find(body, expected_, "<key>Type</key>");
        if (type == expected_) return false;
        type += 15; // Length of <key>Type</key>.
        while (type < expected_ && whitespace(body[type])) ++type;
        return matches(body, expected_, type,
                       "<string>com.apple.mobile.lockdown</string>");
    }

    size_t body_size() const { return expected_; }

private:
    static bool whitespace(uint8_t c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }
    static bool matches(const uint8_t *bytes, size_t size, size_t offset, const char *text) {
        for (size_t i = 0; text[i]; ++i) {
            if (offset >= size || bytes[offset++] != uint8_t(text[i])) return false;
        }
        return true;
    }
    static size_t find(const uint8_t *bytes, size_t size, const char *text) {
        for (size_t i = 0; i < size; ++i) if (matches(bytes, size, i, text)) return i;
        return size;
    }
    uint8_t bytes_[capacity + 4] = {};
    size_t used_ = 0;
    uint32_t expected_ = 0;
    bool invalid_ = false;
};

} // namespace lockdown
