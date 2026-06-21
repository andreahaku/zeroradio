/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "entity_store.h"

namespace toolkit {

void EntityStore::upsert(const std::string& id, const std::function<void(Entity&)>& patch) {
    if (id.empty() || !patch) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto& entity = entities_[id];
    if (entity.id.empty()) {
        entity.id = id; // freshly inserted
    }
    patch(entity);
    entity.updated = std::chrono::steady_clock::now();
}

std::vector<Entity> EntityStore::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Entity> out;
    out.reserve(entities_.size());
    for (const auto& [id, entity] : entities_) {
        out.push_back(entity);
    }
    return out;
}

void EntityStore::sweep(double ttl_seconds) {
    if (ttl_seconds <= 0.0) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = entities_.begin(); it != entities_.end();) {
        const double age =
            std::chrono::duration<double>(now - it->second.updated).count();
        if (age > ttl_seconds) {
            it = entities_.erase(it);
        } else {
            ++it;
        }
    }
}

std::size_t EntityStore::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entities_.size();
}

} // namespace toolkit
