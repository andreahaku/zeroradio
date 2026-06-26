/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "ais_decoder.h"

#include <cstdint>
#include <vector>

namespace ais {
namespace {

// AIS lat/lon are in 1/10000 minute == 1/600000 degree, signed two's-complement.
constexpr double kCoordScale = 600000.0;
// "Not available" sentinels (raw fixed-point values, per ITU-R M.1371).
constexpr int32_t kLonNaRaw = 181 * 600000; // 108600000
constexpr int32_t kLatNaRaw = 91 * 600000;  //  54600000
constexpr int kSogNaRaw = 1023;             // 1/10 kt units
constexpr int kCogNaRaw = 3600;             // 1/10 deg units
constexpr int kHdgNa = 511;                 // degrees

// 6-bit-per-character payload, addressable bit by bit (MSB first within a char).
class BitReader {
public:
    explicit BitReader(const std::string& armored) {
        bits_.reserve(armored.size() * 6);
        for (char ch : armored) {
            const unsigned char c = static_cast<unsigned char>(ch);
            // Valid 6-bit armor is exactly 0x30-0x57 ('0'-'W') and 0x60-0x77
            // ('`'-'w'); the 0x58-0x5F gap ('X'-'_') and anything else is illegal
            // and must be rejected, not silently folded into a valid code.
            if (!((c >= 48 && c <= 87) || (c >= 96 && c <= 119))) {
                valid_ = false;
                return;
            }
            int v = c - 48;
            if (v > 40) {
                v -= 8; // the 0x60-0x77 block sits 8 above 0x30-0x57
            }
            for (int b = 5; b >= 0; --b) {
                bits_.push_back(static_cast<uint8_t>((v >> b) & 1));
            }
        }
    }

    bool valid() const { return valid_; }
    std::size_t size() const { return bits_.size(); }

    uint64_t ubits(std::size_t start, std::size_t len) const {
        uint64_t result = 0;
        for (std::size_t i = 0; i < len; ++i) {
            result = (result << 1) | bits_[start + i];
        }
        return result;
    }

    int64_t sbits(std::size_t start, std::size_t len) const {
        int64_t result = static_cast<int64_t>(ubits(start, len));
        if (len > 0 && (result & (int64_t{1} << (len - 1)))) {
            result -= (int64_t{1} << len); // two's-complement sign extension
        }
        return result;
    }

private:
    std::vector<uint8_t> bits_;
    bool valid_{true};
};

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

// XOR of every character between '!' and '*' must equal the trailing hex byte.
bool checksum_ok(const std::string& sentence) {
    const auto bang = sentence.find('!');
    const auto star = sentence.find('*');
    if (bang == std::string::npos || star == std::string::npos || star <= bang + 1 ||
        star + 2 >= sentence.size() + 1 || star + 3 > sentence.size()) {
        return false;
    }
    uint8_t sum = 0;
    for (std::size_t i = bang + 1; i < star; ++i) {
        sum ^= static_cast<uint8_t>(sentence[i]);
    }
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    const int hi = hex(sentence[star + 1]);
    const int lo = hex(sentence[star + 2]);
    if (hi < 0 || lo < 0) {
        return false;
    }
    return sum == static_cast<uint8_t>((hi << 4) | lo);
}

} // namespace

bool parse_aivdm(const std::string& sentence, Vessel& out) {
    if (!checksum_ok(sentence)) {
        return false;
    }

    const auto fields = split(sentence, ',');
    // 0:!xxVDM 1:fragcount 2:fragnum 3:seqid 4:channel 5:payload 6:fill*checksum
    if (fields.size() < 7) {
        return false;
    }

    const std::string& tag = fields[0];
    if (tag.size() < 6) {
        return false;
    }
    const std::string formatter = tag.substr(tag.size() - 3); // "VDM" / "VDO"
    if (formatter != "VDM" && formatter != "VDO") {
        return false;
    }

    // Position reports (1/2/3) are always single-fragment; reject the rest.
    if (fields[1] != "1" || fields[2] != "1") {
        return false;
    }

    const BitReader bits(fields[5]);
    if (!bits.valid()) {
        return false;
    }
    // Field 6 is "<fill>*<checksum>": the fill count (0-5) is the padding bits in
    // the last armored character. The effective payload must be exactly 168 bits
    // for a type 1/2/3 frame (ITU-R M.1371-6 Table 46); reject anything else
    // (truncated payloads, or a bogus fill that under/over-declares the length).
    if (fields[6].empty() || fields[6][0] < '0' || fields[6][0] > '5') {
        return false;
    }
    const int fill = fields[6][0] - '0';
    if (static_cast<int>(bits.size()) - fill != 168) {
        return false;
    }

    const int type = static_cast<int>(bits.ubits(0, 6));
    if (type < 1 || type > 3) {
        return false;
    }

    Vessel v;
    v.msg_type = type;
    v.mmsi = static_cast<unsigned>(bits.ubits(8, 30));
    v.nav_status = static_cast<int>(bits.ubits(38, 4));

    const int sog_raw = static_cast<int>(bits.ubits(50, 10));
    v.has_sog = sog_raw != kSogNaRaw;
    v.sog = sog_raw * 0.1;

    const int32_t lon_raw = static_cast<int32_t>(bits.sbits(61, 28));
    const int32_t lat_raw = static_cast<int32_t>(bits.sbits(89, 27));
    v.has_pos = (lon_raw != kLonNaRaw) && (lat_raw != kLatNaRaw);
    v.lon = lon_raw / kCoordScale;
    v.lat = lat_raw / kCoordScale;

    const int cog_raw = static_cast<int>(bits.ubits(116, 12));
    // 3600 is "not available"; 3601-4095 are reserved -> treat as absent too.
    v.has_cog = cog_raw < kCogNaRaw;
    v.cog = cog_raw * 0.1;

    const int hdg = static_cast<int>(bits.ubits(128, 9));
    // Valid heading is 0-359; 360-510 are reserved and 511 is "not available".
    v.has_hdg = hdg <= 359;
    v.hdg = hdg;

    if (!v.has_sog) v.sog = 0.0;
    if (!v.has_cog) v.cog = 0.0;
    if (!v.has_hdg) v.hdg = 0;
    if (!v.has_pos) { v.lat = 0.0; v.lon = 0.0; }

    out = v;
    return true;
}

} // namespace ais
