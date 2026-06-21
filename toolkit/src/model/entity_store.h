/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "geo.h"

#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace toolkit {

// A generic tracked entity: a stable id, an optional position, an age (`seen`),
// and an app-defined bag of string fields. Radio protocols spread fields across
// message types, so updates merge into the existing record rather than replace.
struct Entity {
    std::string id;
    bool has_pos{false};
    toolkit::geo::LatLon pos{};
    double seen{0.0}; // age in seconds, as reported by the decoder
    std::map<std::string, std::string> fields;
    std::chrono::steady_clock::time_point updated{};
};

// Thread-safe table of entities keyed by id. The reader thread upserts; the UI
// thread takes a snapshot on a timer. Stale entries are dropped by sweep().
class EntityStore {
public:
    EntityStore() = default;

    EntityStore(const EntityStore&) = delete;
    EntityStore& operator=(const EntityStore&) = delete;

    // Merge a patch into the entity with `id` (creating it if missing) and stamp
    // its `updated` time to now. The patch receives the live Entity by reference.
    void upsert(const std::string& id, const std::function<void(Entity&)>& patch);

    // Copy of all entities, under the lock (safe to iterate on the UI thread).
    std::vector<Entity> snapshot() const;

    // Drop entries whose wall-clock age (now - updated) exceeds ttl_seconds.
    void sweep(double ttl_seconds);

    // Current entity count (under the lock).
    std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, Entity> entities_;
};

} // namespace toolkit
