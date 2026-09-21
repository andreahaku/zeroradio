/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "geo.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace toolkit::location {

// The user's position, shared by every app of the suite (ADS-B, AIS radar
// centre, distances). `label` is what the Settings row shows, e.g.
// "Valletta, MT", "35.900, 14.515" or "GPS 35.901, 14.514".
struct Place {
    geo::LatLon pos;
    std::string label;
};

// Load / save ~/.config/zeroradio/location (one line: "lat lon label").
// load() returns nothing when unset or unreadable.
std::optional<Place> load();
bool save(const Place& place);

// "35.9, 14.51" / "35.9 14.51" / "-33.87,151.21" -> a position, when both
// numbers parse and are in range; anything else is not coordinates.
std::optional<geo::LatLon> parse_coords(const std::string& text);

// Offline city list (assets/geodata/cities.tsv, GeoNames, most populous first).
class CityIndex {
public:
    static CityIndex load(const std::string& path);
    bool valid() const { return !cities_.empty(); }

    // Up to `max` cities for a case-insensitive query: names with a word that
    // starts with it ("york" -> New York City, York), then any substring; the
    // most populous first within each group.
    std::vector<Place> search(const std::string& query, size_t max) const;

private:
    struct City {
        std::string key;   // lower-case name, the search key
        std::string label; // "Name, CC"
        geo::LatLon pos;
    };
    std::vector<City> cities_;
};

// GNSS receiver on the Cap LoRa-1262-GPS shield (HAT port: /dev/ttyS0, 115200
// 8N1, powered by the HAT 5 V rail). While alive a background thread powers the
// rail if it was off, wakes the CASIC module, and parses NMEA ($--GGA for the
// satellite count, $--RMC for the fix). The destructor stops it and restores
// the rail to its previous state, as M5's own GPS app does. A cold start can
// take minutes, so the UI polls status() rather than waiting.
class GnssReader {
public:
    struct Status {
        bool port_ok = false; // serial port opened
        bool data = false;    // NMEA sentences arriving
        int satellites = 0;   // in use, from the last GGA
        std::optional<geo::LatLon> fix;
    };

    explicit GnssReader(std::string device = "/dev/ttyS0");
    ~GnssReader();
    GnssReader(const GnssReader&) = delete;
    GnssReader& operator=(const GnssReader&) = delete;

    Status status() const;

private:
    void run();

    std::string device_;
    mutable std::mutex mutex_;
    Status status_;
    std::atomic<bool> running_{true};
    std::thread thread_;
};

// Parsers, exposed for tests: position of a valid $--RMC (status A), and the
// satellites-in-use field of a $--GGA.
std::optional<geo::LatLon> parse_rmc(const std::string& sentence);
std::optional<int> parse_gga_satellites(const std::string& sentence);

} // namespace toolkit::location
