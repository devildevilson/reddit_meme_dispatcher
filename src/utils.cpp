#include "utils.h"

#include <iostream>
#include <fstream>
#include <string>
#include <glaze/glaze.hpp>

#include "spdlog/spdlog.h"
#include "pugixml.hpp"

namespace utility {
time_log::time_log(const std::string_view &msg, std::chrono::steady_clock::time_point tp_) noexcept : msg(msg), tp(tp_) {}
time_log::~time_log() noexcept {
  const auto dur = std::chrono::steady_clock::now() - tp;
  const size_t mcs = std::chrono::duration_cast<std::chrono::microseconds>(dur).count();
  spdlog::info("'{}' took {} mcs ({} s)", msg, mcs, (double(mcs)/1000000.0));
}

unique_files_t* global::unique_files() {
  return &uf;
}

const scraper_settings& global::settings() {
  return sets;
}

void global::check_folders(const size_t no_older_than, const size_t maximum_size) {
  std::unique_lock<std::mutex> lock(mutex);
  s->check_folders(no_older_than, maximum_size);
}

void global::scrape(const std::string_view &current_subreddit, const std::string_view &duration) {
  std::unique_lock<std::mutex> lock(mutex);
  s->scrape(current_subreddit, duration);
}

void global::steal_post(const utility::reddit_post &post) {
  std::unique_lock<std::mutex> lock(mutex);
  s->steal_post(post);
}

void global::init_scraper(const size_t threads_count, std::string path) {
  std::unique_lock<std::mutex> lock(mutex);
  s = std::make_unique<scraper>(threads_count, path);
}

void global::init_settings(scraper_settings sets_) {
  sets = std::move(sets_);
}

unique_files_t global::uf;
scraper_settings global::sets;
std::unique_ptr<scraper> global::s;
std::mutex global::mutex;

const std::array<std::string_view, 6> reddit_time_lengths = { "hour", "day", "week", "month", "year", "all" };

scraper_settings scraper_settings_construct() {
  return scraper_settings{
    { "funny", "memes", "dankmemes", "funnymemes" }, 
    "week",
    "./meme_folder",
    7,
    50 * 1024 * 1024,
    8080,
    uint16_t(trantor::Logger::kWarn)
  };
}

scraper_settings parse_json(const std::string &path) {
  const auto buffer = utility::read_file(path);
  scraper_settings s{};
  auto ec = glz::read_json(s, buffer); // populates s from buffer
  if (ec) { throw std::runtime_error("Could not parse json file " + path); }
  return s;
}

std::string create_json(const scraper_settings &s) {
  std::string buffer;
  auto ec = glz::write_json(s, buffer);
  if (ec) { throw std::runtime_error("Could not create json file from struct"); }
  return buffer;
}

// наверное нужно сделать спинлок?
void spin_until(std::chrono::steady_clock::time_point tp, std::stop_token stoken) {
  auto cur = std::chrono::steady_clock::now();
  while (cur < tp && !stoken.stop_requested()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

void scraper_run(std::stop_token stoken) {
  auto tp = std::chrono::steady_clock::now();
  auto next_tp = tp;
  size_t index = 0;
  const size_t days_seconds = global::settings().every_N_days * 24 * 60 * 60;
  const size_t seconds_until_next = days_seconds / global::settings().subreddits.size();

  while (!stoken.stop_requested()) {
    global::check_folders(days_seconds, global::settings().max_size);

    const std::string_view &current_subreddit = global::settings().subreddits[index];
    global::scrape(current_subreddit, global::settings().reddit_time_length);

    next_tp = next_tp + std::chrono::seconds(seconds_until_next);
    index += 1;
    if (index >= global::settings().subreddits.size()) {
      index = 0;
      next_tp = tp + std::chrono::seconds(days_seconds);
      tp = std::chrono::steady_clock::now();
    }

    spin_until(next_tp, stoken);
  }
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
