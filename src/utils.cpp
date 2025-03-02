#include "utils.h"

#include <iostream>
#include <fstream>
#include <string>

#include "spdlog/spdlog.h"
#include "pugixml.hpp"

namespace utility {
void unique_names::set(std::vector<std::pair<std::string, std::string>> v) {
  std::unique_lock<std::mutex> lock(mutex);
  container.clear();
  for (const auto &[ key, value ] : v) {
    container[key] = value;
  }
}

bool unique_names::add(std::string name, std::string value) {
  std::unique_lock<std::mutex> lock(mutex);
  auto itr = container.find(name);
  if (itr != container.end()) { 
    itr->second = std::move(value);
    return true;
  }

  container[std::move(name)] = std::move(value);
  return false;
}

bool unique_names::remove(const std::string& name) {
  std::unique_lock<std::mutex> lock(mutex);
  auto itr = container.find(name);
  if (itr == container.end()) return false;

  container.erase(itr);
  return true;
}

std::string unique_names::get(std::string name) const {
  std::unique_lock<std::mutex> lock(mutex);
  const auto itr = container.find(name);
  if (itr == container.end()) return std::string();
  return std::string(itr->second);
}

unique_names* global_names() {
  static unique_names names;
  return &names;
}

unique_files* global_unique_files() {
  static unique_files files;
  return &files;
}

time_log::time_log(const std::string_view &msg, std::chrono::steady_clock::time_point tp_) noexcept : msg(msg), tp(tp_) {}
time_log::~time_log() noexcept {
  const auto dur = std::chrono::steady_clock::now() - tp;
  const size_t mcs = std::chrono::duration_cast<std::chrono::microseconds>(dur).count();
  spdlog::info("'{}' took {} mcs ({} s)", msg, mcs, (double(mcs)/1000000.0));
}

std::tuple<std::string, std::string, std::string> parse_reddit_dash_playlist(const std::string &url, const std::string &xml) {
  pugi::xml_document doc;
  const auto xml_res = doc.load_string(xml.c_str());
  if (!xml_res) {
    auto err = std::format("Could not parse xml '{}', reason: {}", url, xml_res.description());
    return std::make_tuple(std::string(), std::string(), std::move(err));
  }

  // expect different quality level video/audio parts
  std::string video_base;
  std::string audio_base;
  for (auto node = doc.child("MPD").child("Period").child("AdaptationSet"); node; node = node.next_sibling("AdaptationSet")) {
    size_t bandwidth = 0;
    std::string set_type = node.attribute("contentType").value();
    for (auto repr = node.child("Representation"); repr; repr = repr.next_sibling("Representation")) {
      size_t bw = repr.attribute("bandwidth").as_int();
      std::string val = repr.child_value("BaseURL");
      if (bw > bandwidth) {
        bandwidth = bw;
        if (set_type == "video") video_base = std::move(val);
        if (set_type == "audio") audio_base = std::move(val);
      }
    }
  }

  if (video_base.empty()) {
    auto err = std::format("Could not get video base url from data '{}'", url);
    return std::make_tuple(std::string(), std::string(), std::move(err));
  }

  const auto base_url = url.back() == '/' ? url : url + "/";
  // предположим что мы что то даже нашли теперь нужно скачать оба файла
  auto video_url = base_url + video_base;
  auto audio_url = audio_base.empty() ? audio_base : base_url + audio_base;
  return std::make_tuple(std::move(video_url), std::move(audio_url), std::string());
}

std::string read_file(const std::string &path) {
  std::ifstream file(path, std::ios::in);
  if (!file.is_open()) return std::string();
  std::string content((std::istreambuf_iterator<char>(file)), (std::istreambuf_iterator<char>()));
  if (file.bad() || file.fail()) return std::string();
  return content;
}

std::string read_file_bin(const std::string &path) {
  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file.is_open()) return std::string();
  std::string content((std::istreambuf_iterator<char>(file)), (std::istreambuf_iterator<char>()));
  if (file.bad() || file.fail()) return std::string();
  return content;
}

// тут имеет смысл возвращать bool и не выкидывать ошибку
io_state write_file_bin(const std::string_view &name, const std::string_view &content) {
  std::ofstream file(std::string(name), std::ios::out | std::ios::binary);
  if (!file.is_open()) return io_state::open_error;
  file.write(content.data(), content.size());
  if (file.bad()) return io_state::io_error;
  if (file.fail()) return io_state::bad_value;
  return io_state::ok;
}

io_state write_file(const std::string_view &name, const std::string_view &content) {
  std::ofstream file(std::string(name), std::ios::out);
  if (!file.is_open()) return io_state::open_error;
  file.write(content.data(), content.size());
  if (file.bad()) return io_state::io_error;
  if (file.fail()) return io_state::bad_value;
  return io_state::ok;
}
}