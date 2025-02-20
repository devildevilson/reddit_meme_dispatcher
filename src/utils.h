#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include <memory>
#include <shared_mutex>
#include "scraper.h"

namespace utility {
struct time_log {
  std::string_view msg;
  std::chrono::steady_clock::time_point tp;

  time_log(const std::string_view &msg, std::chrono::steady_clock::time_point tp_ = std::chrono::steady_clock::now()) noexcept;
  ~time_log() noexcept;
};

template <typename T>
class double_buffer_data {
public:
  double_buffer_data() {}

  size_t size() const {
    std::shared_lock<std::shared_mutex> lock(mutex);
    return current.size();
  }

  T get(const size_t index) const {
    std::shared_lock<std::shared_mutex> lock(mutex);
    return current[index];
  }

  T get_norm(const double k) const {
    std::shared_lock<std::shared_mutex> lock(mutex);
    const size_t index = std::min(size_t(double(current.size()) * std::abs(k)), current.size()-1);
    return current[index];
  }

  size_t find(const T &obj) const {
    std::shared_lock<std::shared_mutex> lock(mutex);
    for (size_t i = 0; i < current.size(); ++i) {
      if (current[i] == obj) return i;
    }

    return SIZE_MAX;
  }

  std::vector<T> get_buffer() const {
    std::shared_lock<std::shared_mutex> lock(mutex);
    return current;
  }

  template <typename F>
  auto call(F&& f) const -> typename std::invoke_result_t<F, std::vector<T>> {
    std::shared_lock<std::shared_mutex> lock(mutex);
    return f(current);
  }

  void set(const size_t index, T obj) {
    std::shared_lock<std::shared_mutex> lock(mutex);
    staging[index] = std::move(obj);
  }

  void setup_staging(std::vector<T> data) {
    std::shared_lock<std::shared_mutex> lock(mutex);
    staging = std::move(data);
  }

  void swap_buffers() {
    std::lock_guard<std::shared_mutex> lock(mutex);
    std::swap(current, staging);
  }
private:
  mutable std::shared_mutex mutex;
  std::vector<T> current;
  std::vector<T> staging;
};

using unique_files_t = double_buffer_data<std::string>;

template <typename F>
class staging_func {
public:
  staging_func(F&& _func) noexcept : func(std::move(_func)) {}
  ~staging_func() noexcept { func(); }
private:
  F func;
};

struct scraper_settings {
  std::vector<std::string> subreddits;
  std::string reddit_time_length;
  std::string folder;
  size_t every_N_days;
  size_t max_size;
  uint16_t port;
  uint16_t log_level;
};

struct global {
public:
  static unique_files_t* unique_files();
  static const scraper_settings& settings();
  static void check_folders(const size_t no_older_than, const size_t maximum_size);
  static void scrape(const std::string_view &current_subreddit, const std::string_view &duration);
  void init_scraper(const size_t threads_count, std::string path);
  void init_settings(scraper_settings sets);
private:
  static unique_files_t uf;
  static scraper_settings sets;
  static std::unique_ptr<scraper> s;
  static std::mutex mutex;
};

scraper_settings scraper_settings_construct();
scraper_settings parse_json(const std::string &path);
std::string create_json(const scraper_settings &s);
void spin_until(std::chrono::steady_clock::time_point tp, std::stop_token stoken);
void scraper_run(std::stop_token stoken);

enum class io_state {
  ok,
  open_error,
  io_error,
  bad_value
};

std::string read_file(const std::string &path);
std::string read_file_bin(const std::string &path);
io_state write_file_bin(const std::string_view &name, const std::string_view &content);
io_state write_file(const std::string_view &name, const std::string_view &content);
}
