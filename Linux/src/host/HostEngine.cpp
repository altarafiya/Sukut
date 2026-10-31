#include "host/HostEngine.h"

#include <future>
#include <utility>

#include <malloc.h>

namespace sukut::host {

using Clock = std::chrono::steady_clock;

HostEngine::HostEngine(std::filesystem::path engine_dir) : engine_dir_(std::move(engine_dir)) {
  worker_ = std::thread([this] { Run(); });
}

HostEngine::~HostEngine() {
  Shutdown();
  {
    std::lock_guard lock(queue_mutex_);
    stopping_ = true;
  }
  queue_ready_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void HostEngine::Post(std::function<void()> task) {
  {
    std::lock_guard lock(queue_mutex_);
    queue_.push_back(std::move(task));
  }
  queue_ready_.notify_one();
}

void HostEngine::Run() {
  while (true) {
    std::function<void()> task;
    {
      std::unique_lock lock(queue_mutex_);
      queue_ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (queue_.empty()) return;
      task = std::move(queue_.front());
      queue_.pop_front();
    }
    task();
  }
}

void HostEngine::Update(const std::function<void(EngineState&)>& change) {
  std::lock_guard lock(state_mutex_);
  change(state_);
  ++state_.revision;
}

EngineState HostEngine::State() const {
  std::lock_guard lock(state_mutex_);
  return state_;
}

bool HostEngine::Select(int mode, const HostSettings& settings, std::string& error) {
  std::promise<std::pair<bool, std::string>> result;
  auto future = result.get_future();
  Post([&] {
    std::string e;
    const bool ok = Apply(mode, settings, true, e);
    result.set_value({ok, e});
  });
  auto [ok, message] = future.get();
  error = message;
  return ok;
}

void HostEngine::SelectAsync(int mode, HostSettings settings, std::function<void(bool, std::string)> done) {
  Post([this, mode, settings = std::move(settings), done = std::move(done)] {
    std::string error;
    const bool ok = Apply(mode, settings, true, error);
    if (done) done(ok, error);
  });
}

void HostEngine::SetAmplification(int db) {
  Post([this, db] {
    applied_.amplification_db = db;
    if (session_) session_->SetAmplification(db);
  });
}

bool HostEngine::Apply(int mode, const HostSettings& settings, bool by_user, std::string& error) {
  if (by_user) { attempts_ = 0; restart_at_.reset(); healthy_since_.reset(); }
  Update([](EngineState& s) { s.busy = true; });
  if (mode == 0) {
    if (session_) { session_->Stop(); session_.reset(); }
    applied_mode_ = 0;
    malloc_trim(0);  // the engine's memory goes back to the system while Off
    Update([](EngineState& s) { const auto revision = s.revision; s = EngineState{}; s.revision = revision; });
    return true;
  }
  if (settings.source_pid != 0 && !std::filesystem::exists("/proc/" + std::to_string(settings.source_pid))) {
    // The chosen app closed before Sukut could start on it: the closed-app rule.
    if (session_) { session_->Stop(); session_.reset(); }
    applied_mode_ = 0;
    applied_.source_pid = 0;
    error = "source app closed";
    {
      std::lock_guard lock(state_mutex_);
      facts_.push_back("source_closed");
    }
    Update([](EngineState& s) {
      const auto revision = s.revision;
      s = EngineState{};
      s.revision = revision;
      s.state = 2;
      s.attention = "source_closed";
    });
    return false;
  }
  // Same route and buffering: switch the mode live, as the Windows engine does.
  const bool same_route = session_ && settings.output == applied_.output && settings.source_pid == applied_.source_pid &&
                          settings.buffering == applied_.buffering;
  if (same_route) {
    session_->SetMode(mode, settings.optimized_background_db);
    session_->SetAmplification(settings.amplification_db);
    applied_ = settings;
    applied_mode_ = mode;
    Update([mode](EngineState& s) { s.mode = mode; s.state = 1; s.busy = false; s.attention.clear(); });
    return true;
  }
  if (session_) { session_->Stop(); session_.reset(); }
  auto session = std::make_unique<linux_audio::SukutSession>();
  linux_audio::SessionSettings start;
  start.engine_dir = engine_dir_;
  start.mode = mode;
  start.buffering = settings.buffering;
  start.optimized_background_db = settings.optimized_background_db;
  start.amplification_db = settings.amplification_db;
  start.output = settings.output;
  start.source_pid = settings.source_pid;
  if (!session->Start(start, error)) {
    applied_mode_ = 0;
    Update([&error](EngineState& s) {
      s.mode = 0; s.state = 2; s.busy = false; s.attention = error; s.output.clear(); s.source_pid = 0; s.buffering = -1;
    });
    return false;
  }
  const std::string output = session->Output();
  session_ = std::move(session);
  applied_ = settings;
  applied_mode_ = mode;
  Update([mode, &output, &settings](EngineState& s) {
    s.mode = mode; s.state = 1; s.busy = false; s.reconnecting = false; s.attention.clear(); s.output = output;
    s.source_pid = settings.source_pid; s.buffering = settings.buffering;
  });
  return true;
}

std::vector<std::string> HostEngine::Supervise() {
  bool queue = false;
  std::vector<std::string> facts;
  {
    std::lock_guard lock(state_mutex_);
    facts = std::exchange(facts_, {});
    if (!tick_queued_) { tick_queued_ = true; queue = true; }
  }
  if (queue) Post([this] { Tick(); });
  return facts;
}

void HostEngine::Tick() {
  {
    std::lock_guard lock(state_mutex_);
    tick_queued_ = false;
  }
  const auto now = Clock::now();
  if (!session_) {
    if (restart_at_ && now >= *restart_at_) {
      restart_at_.reset();
      ++attempts_;
      std::string error;
      if (!Apply(restart_mode_, applied_, false, error) && attempts_ >= 3)
        Update([&error](EngineState& s) { s.state = 2; s.reconnecting = false; s.attention = error; });
      else if (session_) Update([](EngineState& s) { s.reconnecting = false; });
      else restart_at_ = now + std::chrono::seconds(1 << attempts_);
    }
    return;
  }
  std::vector<std::string> facts;
  for (const auto& event : session_->TakeEvents()) {
    if (event == "SOURCE_CLOSED") {
      // Windows rule: the chosen app closed, so this session ends; the next
      // explicit mode uses all system audio.
      session_->Stop();
      session_.reset();
      applied_mode_ = 0;
      applied_.source_pid = 0;
      facts.push_back("source_closed");
      Update([](EngineState& s) {
        s.mode = 0; s.state = 2; s.attention = "source_closed"; s.output.clear(); s.standby = false;
        s.source_pid = 0; s.buffering = -1;
      });
      break;
    }
    if (event.rfind("OUTPUT_CHANGED to=", 0) == 0) {
      const auto name = event.substr(18, event.find(' ', 18) - 18);
      applied_.output = name;
      facts.push_back("output=" + name);
      Update([&name](EngineState& s) { s.output = name; });
    } else if (event == "OUTPUT_LOST") {
      Update([](EngineState& s) { s.output.clear(); });
    }
  }
  if (session_) {
    const auto snapshot = session_->Snapshot();
    const bool failed = !snapshot.pipeline.failure.empty() || !snapshot.pipeline.active;
    if (failed) {
      // Bounded automatic restart (1, 2, 4 s), then ask the user.
      const std::string failure = snapshot.pipeline.failure.empty() ? "audio stopped" : snapshot.pipeline.failure;
      restart_mode_ = applied_mode_;
      session_->Stop();
      session_.reset();
      healthy_since_.reset();
      if (attempts_ < 3) {
        restart_at_ = now + std::chrono::seconds(1 << attempts_);
        Update([](EngineState& s) { s.reconnecting = true; s.state = 1; });
      } else {
        Update([&failure](EngineState& s) {
          s.mode = 0; s.state = 2; s.reconnecting = false; s.attention = failure; s.source_pid = 0; s.buffering = -1;
        });
      }
    } else {
      if (!healthy_since_) healthy_since_ = now;
      if (now - *healthy_since_ >= std::chrono::minutes(1)) attempts_ = 0;
      const bool standby = snapshot.cleaner.standby;
      const auto underruns = snapshot.bridge.underruns;
      const auto fallbacks = snapshot.cleaner.fallbacks;
      const bool priority = snapshot.pipeline.processing_schedule.priority_set;
      std::lock_guard lock(state_mutex_);
      if (state_.standby != standby || state_.underruns != underruns || state_.fallbacks != fallbacks || state_.priority_set != priority) {
        state_.standby = standby; state_.underruns = underruns; state_.fallbacks = fallbacks; state_.priority_set = priority;
        ++state_.revision;
      }
    }
  }
  if (!facts.empty()) {
    std::lock_guard lock(state_mutex_);
    facts_.insert(facts_.end(), facts.begin(), facts.end());
  }
}

void HostEngine::Shutdown() {
  {
    std::lock_guard lock(queue_mutex_);
    if (shut_down_ || stopping_) return;
    shut_down_ = true;
  }
  std::promise<void> done;
  auto future = done.get_future();
  Post([this, &done] {
    if (session_) { session_->Stop(); session_.reset(); }
    applied_mode_ = 0;
    done.set_value();
  });
  future.wait();
}

}  // namespace sukut::host
