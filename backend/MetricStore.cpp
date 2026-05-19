#include "MetricStore.h"
#include <mutex>
#include <shared_mutex>

void MetricStore::update(const TelemetrySnapshot& snap) {
    std::unique_lock lock(mu_);
    snap_ = snap;
    state_.store(snap.state, std::memory_order_release);
}

TelemetrySnapshot MetricStore::snapshot() const {
    std::shared_lock lock(mu_);
    return snap_;
}
