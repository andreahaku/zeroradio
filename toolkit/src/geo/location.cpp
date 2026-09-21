/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "location.h"

#include "persisted_state.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <termios.h>
#include <unistd.h>

namespace toolkit::location {

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

std::filesystem::path location_file() {
    return config_file("zeroradio", "location");
}

// NMEA ddmm.mmmm + hemisphere -> signed decimal degrees.
bool nmea_degrees(const std::string& v, const std::string& hemi, int deg_digits, double& out) {
    if (v.size() < static_cast<size_t>(deg_digits) + 2) return false;
    char* end = nullptr;
    const double deg = std::strtod(v.substr(0, deg_digits).c_str(), &end);
    const double min = std::strtod(v.c_str() + deg_digits, &end);
    if (!std::isfinite(deg) || !std::isfinite(min)) return false;
    out = deg + min / 60.0;
    if (hemi == "S" || hemi == "W") out = -out;
    return true;
}

std::vector<std::string> nmea_fields(const std::string& sentence) {
    std::vector<std::string> f;
    std::stringstream ss(sentence.substr(0, sentence.find('*')));
    for (std::string item; std::getline(ss, item, ',');) f.push_back(item);
    return f;
}

bool is_sentence(const std::string& s, const char* type) {
    return s.size() >= 6 && s[0] == '$' && s.compare(3, 3, type) == 0;
}

constexpr const char* kExt5v = "/sys/class/leds/ext_5v_out/brightness";

int read_int_file(const char* path) {
    std::ifstream in(path);
    int v = -1;
    in >> v;
    return v;
}

void write_int_file(const char* path, int v) {
    std::ofstream out(path);
    out << v << '\n';
}

} // namespace

std::optional<Place> load() {
    std::ifstream in(location_file());
    Place p;
    if (!(in >> p.pos.lat >> p.pos.lon)) return {};
    std::getline(in, p.label);
    p.label = trim(p.label);
    if (std::fabs(p.pos.lat) > 90.0 || std::fabs(p.pos.lon) > 180.0) return {};
    return p;
}

bool save(const Place& place) {
    const auto path = location_file();
    if (!ensure_parent_dir(path)) return false;
    std::ofstream out(path, std::ios::trunc);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6f %.6f ", place.pos.lat, place.pos.lon);
    out << buf << place.label << '\n';
    return static_cast<bool>(out);
}

std::optional<geo::LatLon> parse_coords(const std::string& text) {
    std::string s = text;
    std::replace(s.begin(), s.end(), ',', ' ');
    std::istringstream in(s);
    double lat = 0.0, lon = 0.0;
    std::string rest;
    if (!(in >> lat >> lon) || (in >> rest)) return {};
    if (std::fabs(lat) > 90.0 || std::fabs(lon) > 180.0) return {};
    return geo::LatLon{lat, lon};
}

CityIndex CityIndex::load(const std::string& path) {
    CityIndex index;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string name, cc, lat, lon;
        if (!std::getline(ls, name, '\t') || !std::getline(ls, cc, '\t') ||
            !std::getline(ls, lat, '\t') || !std::getline(ls, lon, '\t')) {
            continue;
        }
        index.cities_.push_back({lower(name), name + ", " + cc,
                                 {std::atof(lat.c_str()), std::atof(lon.c_str())}});
    }
    return index;
}

std::vector<Place> CityIndex::search(const std::string& query, size_t max) const {
    const std::string q = lower(trim(query));
    std::vector<Place> out;
    if (q.empty() || max == 0) return out;

    // Two passes over the population-ordered list: a word of the name starts
    // with the query ("york" -> New York City, then York), then any substring.
    auto word_start = [&](const std::string& key) {
        for (size_t at = key.find(q); at != std::string::npos; at = key.find(q, at + 1)) {
            if (at == 0 || key[at - 1] == ' ' || key[at - 1] == '-') return true;
        }
        return false;
    };
    std::vector<bool> taken(cities_.size(), false);
    for (int pass = 0; pass < 2 && out.size() < max; ++pass) {
        for (size_t i = 0; i < cities_.size() && out.size() < max; ++i) {
            if (taken[i]) continue;
            const auto& key = cities_[i].key;
            if (pass == 0 ? word_start(key) : key.find(q) != std::string::npos) {
                taken[i] = true;
                out.push_back({cities_[i].pos, cities_[i].label});
            }
        }
    }
    return out;
}

// $GNRMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,... -> position when status is A.
std::optional<geo::LatLon> parse_rmc(const std::string& sentence) {
    if (!is_sentence(sentence, "RMC")) return {};
    const auto f = nmea_fields(sentence);
    if (f.size() < 7 || f[2] != "A") return {};
    geo::LatLon p;
    if (!nmea_degrees(f[3], f[4], 2, p.lat) || !nmea_degrees(f[5], f[6], 3, p.lon)) return {};
    return p;
}

// $GNGGA,time,lat,N,lon,E,quality,satellites,... -> satellites in use.
std::optional<int> parse_gga_satellites(const std::string& sentence) {
    if (!is_sentence(sentence, "GGA")) return {};
    const auto f = nmea_fields(sentence);
    if (f.size() < 8 || f[7].empty()) return {};
    return std::atoi(f[7].c_str());
}

GnssReader::GnssReader(std::string shield_device) : shield_device_(std::move(shield_device)) {
    thread_ = std::thread([this] { run(); });
}

GnssReader::~GnssReader() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

GnssReader::Status GnssReader::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

namespace {

speed_t baud_constant(int baud) {
    switch (baud) {
        case 4800: return B4800;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        default: return B9600;
    }
}

int open_serial(const std::string& path, int baud) {
    const int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    termios tio{};
    if (::tcgetattr(fd, &tio) == 0) {
        ::cfmakeraw(&tio);
        ::cfsetispeed(&tio, baud_constant(baud));
        ::cfsetospeed(&tio, baud_constant(baud));
        tio.c_cflag |= CLOCAL | CREAD;
        ::tcsetattr(fd, TCSANOW, &tio);
    }
    return fd;
}

std::vector<std::string> usb_serial_ports() {
    std::vector<std::string> ports;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator("/dev", ec)) {
        const std::string name = e.path().filename().string();
        if (name.rfind("ttyACM", 0) == 0 || name.rfind("ttyUSB", 0) == 0) {
            ports.push_back(e.path().string());
        }
    }
    std::sort(ports.begin(), ports.end());
    return ports;
}

} // namespace

void GnssReader::consume(const std::string& sentence) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.data = true;
    if (const auto sats = parse_gga_satellites(sentence)) status_.satellites = *sats;
    if (const auto fix = parse_rmc(sentence)) status_.fix = fix;
}

bool GnssReader::probe(int fd, int timeout_ms) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    char buf[256];
    bool nmea = false;
    line_.clear();
    while (running_.load() && std::chrono::steady_clock::now() < deadline) {
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        for (ssize_t i = 0; i < n; ++i) {
            if (buf[i] != '\n') {
                if (line_.size() < 256) line_.push_back(buf[i]);
                continue;
            }
            const std::string s = trim(line_.substr(0, line_.find('\r')));
            line_.clear();
            if (s.size() > 6 && s[0] == '$' && s[1] == 'G') {
                nmea = true;
                consume(s);
            }
        }
        if (nmea) return true;
    }
    return false;
}

void GnssReader::read_loop(int fd) {
    char buf[256];
    while (running_.load()) {
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        for (ssize_t i = 0; i < n; ++i) {
            if (buf[i] != '\n') {
                if (line_.size() < 256) line_.push_back(buf[i]);
                continue;
            }
            consume(trim(line_.substr(0, line_.find('\r'))));
            line_.clear();
        }
    }
}

void GnssReader::run() {
    auto found = [this](const char* source) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.searching = false;
        status_.port_ok = true;
        status_.source = source;
    };

    // 1. Explicit device (power users, tests).
    if (const char* dev = std::getenv("ZERORADIO_GPS_DEVICE"); dev && dev[0] != '\0') {
        const char* baud = std::getenv("ZERORADIO_GPS_BAUD");
        const int fd = open_serial(dev, baud ? std::atoi(baud) : 9600);
        if (fd >= 0) {
            found("USB");
            read_loop(fd);
            ::close(fd);
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.searching = false;
        }
        return;
    }

    // 2. A USB GPS: listen only, never write to an unknown serial device.
    for (const auto& port : usb_serial_ports()) {
        if (!running_.load()) return;
        const int fd = open_serial(port, 9600);
        if (fd < 0) continue;
        if (probe(fd, 3000)) {
            found("USB");
            read_loop(fd);
            ::close(fd);
            return;
        }
        ::close(fd);
    }

    // 3. The HAT shield, powered from the HAT 5 V rail.
    const int rail_before = read_int_file(kExt5v);
    const bool powered_here = rail_before == 0;
    if (powered_here) {
        write_int_file(kExt5v, 1);
        // Module boot time, in short steps so a quick close never stalls the UI.
        for (int i = 0; i < 30 && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    const int fd = running_.load() ? open_serial(shield_device_, 115200) : -1;
    if (fd >= 0) {
        // Harmless CASIC query: wakes a module the Meshtastic firmware may have
        // put to sleep with $PCAS12 (see docs/cap-lora-1262.md).
        static const char kWake[] = "$PCAS06,0*1B\r\n";
        (void)!::write(fd, kWake, sizeof(kWake) - 1);
        if (probe(fd, 5000)) {
            found("shield");
            read_loop(fd);
        }
        ::close(fd);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.searching = false;
    }
    if (powered_here) write_int_file(kExt5v, rail_before);
}

} // namespace toolkit::location
