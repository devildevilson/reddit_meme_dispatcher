#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include <shared_mutex>

namespace utility {
class unique_names {
public:
  void set(std::vector<std::pair<std::string, std::string>> v);
  bool add(std::string name, std::string value);
  bool remove(const std::string& name);
  std::string get(std::string name) const;
private:
  mutable std::mutex mutex;
  std::unordered_map<std::string, std::string> container;
};

unique_names* global_names();

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

using unique_files = double_buffer_data<std::string>;
unique_files* global_unique_files();

template <typename F>
class staging_func {
public:
  staging_func(F&& _func) noexcept : func(std::move(_func)) {}
  ~staging_func() {
    func();
  }
private:
  F func;
};

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