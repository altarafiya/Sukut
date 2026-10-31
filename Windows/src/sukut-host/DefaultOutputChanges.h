#pragma once
#include "sukut-host/Engine.h"
#include <algorithm>
#include <optional>
#include <utility>

namespace sukut::host {
// Notifications can precede endpoint enumeration. Coalesce the latest Windows
// choice and wait for an active, physical render endpoint before switching.
class DefaultOutputChanges {
 public:
  void Observe(const std::string& id, bool internal, const std::string& cable) {
    if (internal || id.empty() || id == cable) return;
    pending_ = id; reads_ = 0;
  }
  void ObserveArrival(const std::string& id, const std::string& cable) {
    if (id.empty() || id == cable) return;
    std::erase_if(arrivals_, [&](const auto& item) { return item.id == id; });
    if (arrivals_.size() == 8) arrivals_.erase(arrivals_.begin());
    arrivals_.push_back({id, 0});
  }
  [[nodiscard]] bool Pending() const { return !pending_.empty() || !arrivals_.empty(); }
  std::optional<std::string> Resolve(const std::vector<EngineStatus::Endpoint>& endpoints) {
    if (!Pending()) return {};
    const auto find = [&](const auto& id) { return std::find_if(endpoints.begin(), endpoints.end(), [&](const auto& e) { return e.id == id; }); };
    const auto take = [&](std::string id) { pending_.clear(); arrivals_.clear(); return std::optional<std::string>(std::move(id)); };
    const auto e = find(pending_);
    if (e != endpoints.end() && e->flow == 1 && !e->is_virtual) return take(e->id);
    if (e != endpoints.end() || ++reads_ >= 40) pending_.clear();
    // Connection/state notifications are independent of our own default-route
    // writes. A Bluetooth connection during a command must not be discarded.
    // Capture/virtual endpoints arriving in the same burst cannot hide it.
    for (auto item = arrivals_.rbegin(); item != arrivals_.rend(); ++item) {
      const auto endpoint = find(item->id);
      if (endpoint != endpoints.end() && endpoint->flow == 1 && !endpoint->is_virtual) return take(endpoint->id);
    }
    for (auto& item : arrivals_) ++item.reads;
    std::erase_if(arrivals_, [&](const auto& item) { return find(item.id) != endpoints.end() || item.reads >= 40; });
    return {};
  }
 private:
  std::string pending_;
  int reads_ = 0;
  struct Arrival { std::string id; int reads; };
  std::vector<Arrival> arrivals_;
};
}
