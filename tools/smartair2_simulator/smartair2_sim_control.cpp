#include "smartair2_sim_control.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>

using namespace esphome::haier::smartair2_protocol;

namespace {

enum class Phase { IDLE, COOLING, HEATING, HOLDING, PASSIVE_DRIFT };

struct Runtime {
  bool initialized = false;
  bool dynamics = true;
  double room = 20.0;
  double humidity = 45.0;
  double room_drift_temperature = 28.0;
  double seconds_to_target = 60.0;
  double hold_seconds = 20.0;
  double drift_delta = 1.0;
  double drift_seconds = 40.0;
  double hysteresis = 0.7;
  double passive_seconds_per_degree = 70.0;
  bool compressor_active = false;
  Phase phase = Phase::IDLE;
  std::chrono::steady_clock::time_point hold_until{};
  std::chrono::steady_clock::time_point last_tick{};
  std::chrono::steady_clock::time_point last_status{};
  time_t control_mtime = 0;
  int last_set = -1;
  int last_mode = -1;
  int last_power = -1;
  int last_fan = -1;
  int last_swing_both = -1;
  int last_horizontal = -1;
  int last_vertical = -1;
} rt;

std::string env_or(const char *key, const char *fallback) {
  const char *value = getenv(key);
  return value && *value ? value : fallback;
}

const std::string &control_file() {
  static std::string path = env_or("SMARTAIR2_SIM_CONTROL_FILE", "/tmp/haier-smartair2-runtime/smartair2-control.env");
  return path;
}

const std::string &status_file() {
  static std::string path = env_or("SMARTAIR2_SIM_STATUS_FILE", "/tmp/haier-smartair2-runtime/smartair2-status.json");
  return path;
}

const std::string &event_file() {
  static std::string path = env_or("SMARTAIR2_SIM_EVENT_LOG", "/tmp/haier-smartair2-runtime/smartair2-events.jsonl");
  return path;
}

std::string phase_name() {
  switch (rt.phase) {
    case Phase::IDLE:
      return "idle";
    case Phase::COOLING:
      return "cooling";
    case Phase::HEATING:
      return "heating";
    case Phase::HOLDING:
      return "holding";
    case Phase::PASSIVE_DRIFT:
      return "passive_drift";
  }
  return "unknown";
}

std::string mode_name(uint8_t mode) {
  switch (mode) {
    case 0:
      return "auto";
    case 1:
      return "cool";
    case 2:
      return "heat";
    case 3:
      return "fan";
    case 4:
      return "dry";
  }
  return "unknown";
}

uint8_t mode_from(const std::string &value, uint8_t fallback) {
  if (value == "auto")
    return 0;
  if (value == "cool")
    return 1;
  if (value == "heat")
    return 2;
  if (value == "fan")
    return 3;
  if (value == "dry")
    return 4;
  return fallback;
}

std::string fan_name(uint8_t fan) {
  switch (fan) {
    case 0:
      return "high";
    case 1:
      return "medium";
    case 2:
      return "low";
    case 3:
      return "auto";
  }
  return "unknown";
}

uint8_t fan_from(const std::string &value, uint8_t fallback) {
  if (value == "high")
    return 0;
  if (value == "medium")
    return 1;
  if (value == "low")
    return 2;
  if (value == "auto")
    return 3;
  return fallback;
}

void ensure_parent_dir(const std::string &path) {
  auto pos = path.find_last_of('/');
  if (pos == std::string::npos || pos == 0)
    return;
  mkdir(path.substr(0, pos).c_str(), 0755);
}

std::string swing_name(const HaierPacketControl &state) {
  switch (state.swing_both) {
    case 0:
      break;
    case 1:
      return "vertical";
    case 2:
      return "horizontal";
    case 3:
      return "both";
    default:
      return "unknown_" + std::to_string(state.swing_both);
  }
  if (!state.use_swing_bits)
    return "off";
  if (state.horizontal_swing && state.vertical_swing)
    return "both";
  if (state.horizontal_swing)
    return "horizontal";
  if (state.vertical_swing)
    return "vertical";
  return "off";
}

void apply_swing(HaierPacketControl &state, const std::string &value) {
  state.swing_both = 0;
  state.use_swing_bits = 0;
  state.horizontal_swing = 0;
  state.vertical_swing = 0;
  if (value == "both") {
    state.swing_both = 3;
  } else if (value == "horizontal") {
    state.swing_both = 2;
  } else if (value == "vertical") {
    state.swing_both = 1;
  }
}

double target(const HaierPacketControl &state) { return 16.0 + state.set_point; }

void event(const std::string &type, const std::string &details) {
  ensure_parent_dir(event_file());
  std::ofstream out(event_file(), std::ios::app);
  if (out)
    out << "{\"ts\":" << time(nullptr) << ",\"type\":\"" << type << "\",\"details\":" << details << "}\n";
}

std::map<std::string, std::string> read_cfg() {
  std::map<std::string, std::string> result;
  std::ifstream in(control_file());
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    auto pos = line.find('=');
    if (pos != std::string::npos)
      result[line.substr(0, pos)] = line.substr(pos + 1);
  }
  return result;
}

double dbl(const std::map<std::string, std::string> &cfg, const char *key, double fallback) {
  auto it = cfg.find(key);
  if (it == cfg.end())
    return fallback;
  try {
    return std::stod(it->second);
  } catch (...) {
    return fallback;
  }
}

int integer(const std::map<std::string, std::string> &cfg, const char *key, int fallback) {
  auto it = cfg.find(key);
  if (it == cfg.end())
    return fallback;
  try {
    return std::stoi(it->second);
  } catch (...) {
    return fallback;
  }
}

bool boolean(const std::map<std::string, std::string> &cfg, const char *key, bool fallback) {
  auto it = cfg.find(key);
  if (it == cfg.end())
    return fallback;
  return it->second == "1" || it->second == "true" || it->second == "on" || it->second == "yes";
}

double move_towards(double current, double goal, double max_step) {
  if (current < goal)
    return std::min(goal, current + max_step);
  if (current > goal)
    return std::max(goal, current - max_step);
  return current;
}

void sync_room(HaierPacketControl &state) {
  rt.room = std::max(0.0, std::min(60.0, rt.room));
  rt.humidity = std::max(0.0, std::min(100.0, rt.humidity));
  state.room_temperature = static_cast<uint8_t>(std::lround(rt.room));
  state.room_humidity = static_cast<uint8_t>(std::lround(rt.humidity));
  state.compressor = rt.compressor_active ? 1 : 0;
}

void note_state_change(const HaierPacketControl &state, const char *source) {
  std::ostringstream details;
  details << "{\"source\":\"" << source << "\",\"power\":" << (state.ac_power ? "true" : "false")
          << ",\"mode\":\"" << mode_name(state.ac_mode) << "\",\"target\":" << target(state)
          << ",\"fan\":\"" << fan_name(state.fan_mode) << "\",\"swing\":\"" << swing_name(state) << "\"}";
  event("state_changed", details.str());
}

void load_control(HaierPacketControl &state, bool force = false) {
  struct stat st {};
  if (stat(control_file().c_str(), &st) != 0)
    return;
  if (!force && st.st_mtime == rt.control_mtime)
    return;

  rt.control_mtime = st.st_mtime;
  auto cfg = read_cfg();
  bool changed = false;

  rt.dynamics = boolean(cfg, "dynamics", rt.dynamics);
  rt.seconds_to_target = std::max(1.0, dbl(cfg, "seconds_to_target", rt.seconds_to_target));
  rt.hold_seconds = std::max(0.0, dbl(cfg, "hold_seconds", rt.hold_seconds));
  rt.drift_delta = std::max(0.0, dbl(cfg, "drift_delta", rt.drift_delta));
  rt.drift_seconds = std::max(1.0, dbl(cfg, "drift_seconds", rt.drift_seconds));
  rt.hysteresis = std::max(0.1, dbl(cfg, "hysteresis", rt.hysteresis));
  rt.passive_seconds_per_degree = std::max(5.0, dbl(cfg, "passive_seconds_per_degree", rt.passive_seconds_per_degree));
  rt.room_drift_temperature = dbl(cfg, "ambient_temperature", rt.room_drift_temperature);
  rt.room_drift_temperature = dbl(cfg, "room_drift_temperature", rt.room_drift_temperature);
  rt.humidity = dbl(cfg, "humidity", rt.humidity);

  if (cfg.count("room_temperature")) {
    rt.room = dbl(cfg, "room_temperature", rt.room);
    changed = true;
  }
  if (cfg.count("target_temperature")) {
    int temp = std::max(16, std::min(30, integer(cfg, "target_temperature", static_cast<int>(target(state)))));
    auto old = state.set_point;
    state.set_point = temp - 16;
    changed = changed || old != state.set_point;
  }
  if (cfg.count("mode")) {
    auto old = state.ac_mode;
    state.ac_mode = mode_from(cfg["mode"], state.ac_mode);
    changed = changed || old != state.ac_mode;
  }
  if (cfg.count("fan_mode")) {
    auto old = state.fan_mode;
    state.fan_mode = fan_from(cfg["fan_mode"], state.fan_mode);
    changed = changed || old != state.fan_mode;
  }
  if (cfg.count("power")) {
    auto old = state.ac_power;
    state.ac_power = boolean(cfg, "power", state.ac_power == 1) ? 1 : 0;
    changed = changed || old != state.ac_power;
  }
  if (cfg.count("swing_mode")) {
    std::string old = swing_name(state);
    apply_swing(state, cfg["swing_mode"]);
    changed = changed || old != swing_name(state);
  }
  if (cfg.count("turbo_mode"))
    state.turbo_mode = boolean(cfg, "turbo_mode", state.turbo_mode == 1) ? 1 : 0;
  if (cfg.count("quiet_mode"))
    state.quiet_mode = boolean(cfg, "quiet_mode", state.quiet_mode == 1) ? 1 : 0;
  if (cfg.count("display_status"))
    state.display_status = boolean(cfg, "display_status", state.display_status == 1) ? 1 : 0;

  sync_room(state);
  event("control_file_applied", "{\"path\":\"" + control_file() + "\"}");
  if (changed)
    note_state_change(state, "console");
}

void simulate(HaierPacketControl &state) {
  auto now = std::chrono::steady_clock::now();
  if (rt.last_tick.time_since_epoch().count() == 0)
    rt.last_tick = now;
  double dt = std::chrono::duration<double>(now - rt.last_tick).count();
  rt.last_tick = now;
  dt = std::max(0.0, std::min(5.0, dt));

  if ((int) state.set_point != rt.last_set || (int) state.ac_mode != rt.last_mode || (int) state.ac_power != rt.last_power ||
      (int) state.fan_mode != rt.last_fan || (int) state.swing_both != rt.last_swing_both ||
      (int) state.horizontal_swing != rt.last_horizontal || (int) state.vertical_swing != rt.last_vertical) {
    rt.last_set = state.set_point;
    rt.last_mode = state.ac_mode;
    rt.last_power = state.ac_power;
    rt.last_fan = state.fan_mode;
    rt.last_swing_both = state.swing_both;
    rt.last_horizontal = state.horizontal_swing;
    rt.last_vertical = state.vertical_swing;
    note_state_change(state, "device");
  }

  rt.compressor_active = false;
  if (!rt.dynamics) {
    sync_room(state);
    return;
  }

  const double t = target(state);
  const bool auto_mode = state.ac_power && state.ac_mode == 0;
  const bool cool_mode = state.ac_power && state.ac_mode == 1;
  const bool heat_mode = state.ac_power && state.ac_mode == 2;
  bool active_cool = false;
  bool active_heat = false;

  if (cool_mode) {
    active_cool = rt.phase == Phase::PASSIVE_DRIFT ? rt.room >= t + rt.hysteresis : rt.room > t;
  } else if (heat_mode) {
    active_heat = rt.phase == Phase::PASSIVE_DRIFT ? rt.room <= t - rt.hysteresis : rt.room < t;
  } else if (auto_mode) {
    if (rt.room < t) {
      active_heat = rt.phase == Phase::PASSIVE_DRIFT ? rt.room <= t - rt.hysteresis : true;
    } else if (rt.room > t) {
      active_cool = rt.phase == Phase::PASSIVE_DRIFT ? rt.room >= t + rt.hysteresis : true;
    }
  }

  if (active_cool || active_heat) {
    rt.compressor_active = true;
    rt.phase = active_cool ? Phase::COOLING : Phase::HEATING;
    double delta = std::max(0.1, std::abs(rt.room - t));
    double step = std::max(0.01, delta / rt.seconds_to_target) * dt;
    rt.room = move_towards(rt.room, t, step);
    if (std::abs(rt.room - t) < 0.02) {
      rt.room = t;
      rt.phase = Phase::HOLDING;
      rt.hold_until = now + std::chrono::milliseconds(static_cast<int>(rt.hold_seconds * 1000.0));
      event("target_reached", "{\"room\":" + std::to_string(rt.room) + "}");
    }
  } else {
    if (state.ac_power && now < rt.hold_until) {
      rt.phase = Phase::HOLDING;
      rt.room = t;
    } else {
      rt.phase = state.ac_power ? Phase::PASSIVE_DRIFT : Phase::IDLE;
      double drift_goal = rt.room_drift_temperature;
      if (state.ac_power && rt.phase == Phase::PASSIVE_DRIFT && rt.drift_delta > 0.0) {
        if (cool_mode)
          drift_goal = std::max(drift_goal, t + rt.drift_delta);
        else if (heat_mode)
          drift_goal = std::min(drift_goal, t - rt.drift_delta);
      }
      double delta = std::abs(drift_goal - rt.room);
      double step = std::max(0.001, delta / rt.passive_seconds_per_degree) * dt;
      rt.room = move_towards(rt.room, drift_goal, step);
    }
  }

  sync_room(state);
}

void write_status(const HaierPacketControl &state, bool force = false) {
  auto now = std::chrono::steady_clock::now();
  if (!force && std::chrono::duration_cast<std::chrono::milliseconds>(now - rt.last_status).count() < 1000)
    return;
  rt.last_status = now;

  ensure_parent_dir(status_file());
  std::ofstream out(status_file() + ".tmp");
  if (!out)
    return;
  out << "{\n"
      << "  \"ts\": " << time(nullptr) << ",\n"
      << "  \"protocol\": \"haier-v1-smartair2\",\n"
      << "  \"room_temperature\": " << rt.room << ",\n"
      << "  \"reported_room_temperature\": " << (int) state.room_temperature << ",\n"
      << "  \"target_temperature\": " << target(state) << ",\n"
      << "  \"ambient_temperature\": " << rt.room_drift_temperature << ",\n"
      << "  \"room_drift_temperature\": " << rt.room_drift_temperature << ",\n"
      << "  \"humidity\": " << (int) state.room_humidity << ",\n"
      << "  \"power\": " << (state.ac_power ? "true" : "false") << ",\n"
      << "  \"mode\": \"" << mode_name(state.ac_mode) << "\",\n"
      << "  \"fan_mode\": \"" << fan_name(state.fan_mode) << "\",\n"
      << "  \"swing_mode\": \"" << swing_name(state) << "\",\n"
      << "  \"turbo_mode\": " << (state.turbo_mode ? "true" : "false") << ",\n"
      << "  \"quiet_mode\": " << (state.quiet_mode ? "true" : "false") << ",\n"
      << "  \"display_status\": " << (state.display_status ? "true" : "false") << ",\n"
      << "  \"compressor_active\": " << (rt.compressor_active ? "true" : "false") << ",\n"
      << "  \"phase\": \"" << phase_name() << "\",\n"
      << "  \"dynamics\": " << (rt.dynamics ? "true" : "false") << ",\n"
      << "  \"seconds_to_target\": " << rt.seconds_to_target << ",\n"
      << "  \"hold_seconds\": " << rt.hold_seconds << ",\n"
      << "  \"drift_delta\": " << rt.drift_delta << ",\n"
      << "  \"drift_seconds\": " << rt.drift_seconds << ",\n"
      << "  \"hysteresis\": " << rt.hysteresis << ",\n"
      << "  \"passive_seconds_per_degree\": " << rt.passive_seconds_per_degree << ",\n"
      << "  \"control_file\": \"" << control_file() << "\",\n"
      << "  \"event_log\": \"" << event_file() << "\"\n"
      << "}\n";
  out.close();
  rename((status_file() + ".tmp").c_str(), status_file().c_str());
}

}  // namespace

void smartair2_control_init(HaierPacketControl &state) {
  rt.room = state.room_temperature;
  rt.humidity = state.room_humidity;
  rt.last_tick = std::chrono::steady_clock::now();
  rt.initialized = true;
  load_control(state, true);
  write_status(state, true);
}

void smartair2_control_tick(HaierPacketControl &state) {
  if (!rt.initialized)
    smartair2_control_init(state);
  load_control(state);
  simulate(state);
  write_status(state);
}

void smartair2_control_note_change(const char *source, const char *field, unsigned int old_value, unsigned int new_value) {
  std::ostringstream details;
  details << "{\"source\":\"" << source << "\",\"field\":\"" << field << "\",\"old\":" << old_value << ",\"new\":" << new_value << "}";
  event("command", details.str());
}

void smartair2_control_note_group_change(unsigned int byte_index, unsigned int old_value, unsigned int new_value) {
  std::ostringstream details;
  details << "{\"source\":\"lytko\",\"byte\":" << byte_index << ",\"old\":" << old_value << ",\"new\":" << new_value << "}";
  event("group_change", details.str());
}
