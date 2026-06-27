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

constexpr double kCoordScale = 600000.0;
constexpr int32_t kLonNaRaw = 181 * 600000;
constexpr int32_t kLatNaRaw = 91 * 600000;
constexpr int kSogNaRaw = 1023;
constexpr int kCogNaRaw = 3600;
constexpr int kHdgNa = 511;

constexpr int kPositionBits = 168; // types 1/2/3
constexpr int kStaticBits = 424;   // type 5

class BitReader {
public:
    explicit BitReader(const std::string& armored) {
        bits_.reserve(armored.size() * 6);
        for (char ch : armored) {
            const unsigned char c = static_cast<unsigned char>(ch);
            if (!((c >= 48 && c <= 87) || (c >= 96 && c <= 119))) {
                valid_ = false;
                return;
            }
            int v = c - 48;
            if (v > 40) {
                v -= 8;
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
            result -= (int64_t{1} << len);
        }
        return result;
    }

    // AIS 6-bit ASCII text of `nchars`, trailing '@' (padding) and spaces stripped.
    std::string text(std::size_t start, std::size_t nchars) const {
        std::string s;
        s.reserve(nchars);
        for (std::size_t i = 0; i < nchars; ++i) {
            const int v = static_cast<int>(ubits(start + i * 6, 6));
            s.push_back(static_cast<char>(v < 32 ? v + 64 : v));
        }
        while (!s.empty() && (s.back() == '@' || s.back() == ' ')) {
            s.pop_back();
        }
        return s;
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

bool checksum_ok(const std::string& sentence) {
    const auto bang = sentence.find('!');
    const auto star = sentence.find('*');
    if (bang == std::string::npos || star == std::string::npos || star <= bang + 1 ||
        star + 3 > sentence.size()) {
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

void decode_position(const BitReader& bits, Vessel& v) {
    v.mmsi = static_cast<unsigned>(bits.ubits(8, 30));
    v.nav_status = static_cast<int>(bits.ubits(38, 4));

    const int sog_raw = static_cast<int>(bits.ubits(50, 10));
    v.has_sog = sog_raw != kSogNaRaw;
    v.sog = v.has_sog ? sog_raw * 0.1 : 0.0;

    const int32_t lon_raw = static_cast<int32_t>(bits.sbits(61, 28));
    const int32_t lat_raw = static_cast<int32_t>(bits.sbits(89, 27));
    v.has_pos = (lon_raw != kLonNaRaw) && (lat_raw != kLatNaRaw);
    v.lon = v.has_pos ? lon_raw / kCoordScale : 0.0;
    v.lat = v.has_pos ? lat_raw / kCoordScale : 0.0;

    const int cog_raw = static_cast<int>(bits.ubits(116, 12));
    v.has_cog = cog_raw < kCogNaRaw;
    v.cog = v.has_cog ? cog_raw * 0.1 : 0.0;

    const int hdg = static_cast<int>(bits.ubits(128, 9));
    v.has_hdg = hdg <= 359;
    v.hdg = v.has_hdg ? hdg : 0;
}

void decode_static(const BitReader& bits, Vessel& v) {
    v.mmsi = static_cast<unsigned>(bits.ubits(8, 30));
    v.callsign = bits.text(70, 7);     // 7 chars
    v.name = bits.text(112, 20);       // 20 chars
    v.ship_type = static_cast<int>(bits.ubits(232, 8));
    v.destination = bits.text(302, 20); // 20 chars
}

// Build a BitReader over `payload`, validate the effective length for the message
// type, and dispatch to the right decoder. `fill` is the NMEA fill-bit count.
bool decode_message(const std::string& payload, int fill, Vessel& out) {
    const BitReader bits(payload);
    if (!bits.valid()) {
        return false;
    }
    const long effective = static_cast<long>(bits.size()) - fill;
    if (effective < 38) {
        return false; // not even type + MMSI
    }
    const int type = static_cast<int>(bits.ubits(0, 6));

    Vessel v;
    v.msg_type = type;
    if (type >= 1 && type <= 3) {
        if (effective != kPositionBits) {
            return false;
        }
        decode_position(bits, v);
        out = v;
        return true;
    }
    if (type == 5) {
        if (effective < kStaticBits) {
            return false;
        }
        decode_static(bits, v);
        out = v;
        return true;
    }
    return false;
}

// Parse the common AIVDM envelope. Returns false on a bad sentence; otherwise
// fills the fragment fields (fill is the count from field 6).
bool parse_envelope(const std::string& sentence, std::vector<std::string>& fields,
                    int& fragcount, int& fragnum, int& fill) {
    if (!checksum_ok(sentence)) {
        return false;
    }
    fields = split(sentence, ',');
    if (fields.size() < 7) {
        return false;
    }
    const std::string& tag = fields[0];
    if (tag.size() < 6) {
        return false;
    }
    const std::string formatter = tag.substr(tag.size() - 3);
    if (formatter != "VDM" && formatter != "VDO") {
        return false;
    }
    if (fields[1].size() != 1 || fields[1][0] < '1' || fields[1][0] > '9' ||
        fields[2].size() != 1 || fields[2][0] < '1' || fields[2][0] > '9') {
        return false;
    }
    fragcount = fields[1][0] - '0';
    fragnum = fields[2][0] - '0';
    if (fragnum > fragcount) {
        return false;
    }
    if (fields[6].empty() || fields[6][0] < '0' || fields[6][0] > '5') {
        return false;
    }
    fill = fields[6][0] - '0';
    return true;
}

} // namespace

bool parse_aivdm(const std::string& sentence, Vessel& out) {
    std::vector<std::string> fields;
    int fragcount = 0, fragnum = 0, fill = 0;
    if (!parse_envelope(sentence, fields, fragcount, fragnum, fill)) {
        return false;
    }
    if (fragcount != 1 || fragnum != 1) {
        return false; // single-fragment only; use AivdmReassembler otherwise
    }
    Vessel v;
    if (!decode_message(fields[5], fill, v) || v.msg_type < 1 || v.msg_type > 3) {
        return false; // this entry point is position reports only
    }
    out = v;
    return true;
}

bool AivdmReassembler::feed(const std::string& sentence, Vessel& out) {
    std::vector<std::string> fields;
    int fragcount = 0, fragnum = 0, fill = 0;
    if (!parse_envelope(sentence, fields, fragcount, fragnum, fill)) {
        return false;
    }

    if (fragcount == 1) {
        return decode_message(fields[5], fill, out);
    }

    const std::string key = fields[4] + "|" + fields[3]; // channel | sequence id

    if (fragnum == 1) {
        partials_[key] = Partial{fragcount, 2, fields[5], 0};
        if (partials_.size() > 32) {
            partials_.clear(); // bound the buffer against a flood of garbage
            partials_[key] = Partial{fragcount, 2, fields[5], 0};
        }
        return false;
    }

    auto it = partials_.find(key);
    if (it == partials_.end() || it->second.total != fragcount || it->second.next != fragnum) {
        partials_.erase(key); // out-of-order / lost fragment: drop the partial
        return false;
    }

    Partial& p = it->second;
    p.payload += fields[5];
    p.next = fragnum + 1;
    if (fragnum == fragcount) {
        p.last_fill = fill;
        const std::string payload = p.payload;
        const int last_fill = p.last_fill;
        partials_.erase(it);
        return decode_message(payload, last_fill, out);
    }
    return false; // more fragments to come
}

const char* ship_type_label(int code) {
    if (code >= 70 && code <= 79) return "Cargo";
    if (code >= 80 && code <= 89) return "Tanker";
    if (code >= 60 && code <= 69) return "Passenger";
    if (code >= 40 && code <= 49) return "HSC";          // high-speed craft
    if (code == 30) return "Fishing";
    if (code == 31 || code == 32) return "Towing";
    if (code == 35) return "Military";
    if (code == 36) return "Sailing";
    if (code == 37) return "Pleasure";
    if (code == 50) return "Pilot";
    if (code == 51) return "SAR";
    if (code == 52) return "Tug";
    if (code == 53) return "Port tender";
    if (code == 55) return "Law enforce";
    if (code == 58) return "Medical";
    if (code >= 90 && code <= 99) return "Other";
    return "";
}

} // namespace ais
