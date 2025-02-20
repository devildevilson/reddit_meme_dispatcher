#include "random.h"

#include <string>
#include <string_view>
#include <iostream>
#include <filesystem>
#include <ranges>
#include <fstream>

#include <drogon/drogon.h>
#include "utils.h"

#include "spdlog/spdlog.h"

//using namespace srt;
namespace fs = std::filesystem;
namespace rv = std::ranges::views;

// вообще я так понимаю у меня пока что наклевывается 4 метода
// метод random - возвращает рандомный мем (наверное нужна поддержка конкретного сабреддита)
// метод find - пытается найти мем среди текущих мемов по имени
// метод scrape - скачивает мем в локальную папку (возвращает только имя мема и может какие нибудь доп данные)
// метод recheck - перепроверяет папки
// + возможно parse - забирает ссылку на мем и пытается отыскать метаинформацию о нем

meme::meme() {
  s = xoshiro256starstar::init(123);
}

void meme::get(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
  utility::time_log l("meme");

  const auto memepath = utility::global::unique_files()->call([this] (const std::vector<std::string> &paths) {
    if (paths.size() == 0) return std::string();
    const size_t num = this->interval(paths.size());
    return paths[num];
  });

  // лучше возвращать 404
  if (memepath.empty()) {
    auto resp = HttpResponse::newNotFoundResponse(req);
    resp->setContentTypeString("text/plain");
    resp->setBody("Could not find any meme");
    callback(resp);
    return;
  }

  auto file_data = utility::read_file_bin(memepath);
  if (file_data.empty()) { // попробовать еще раз?
    spdlog::warn("Could not read file '{}'", memepath);
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode((HttpStatusCode)500);
    resp->setContentTypeString("text/plain");
    resp->setBody("Internal server error");
    callback(resp);
    return;
  }

  const auto ext = memepath.substr(memepath.rfind('.')+1);
  auto mime_type = ext == "mp4" ? "video/mp4" : "image/" + ext;
  
  spdlog::info("Trying to send '{}'", memepath);
  auto resp = HttpResponse::newHttpResponse();
  resp->setContentTypeString(std::move(mime_type));
  resp->setBody(std::move(file_data));
  callback(resp);
}

double prng_normalize(const uint64_t value) {
  union { uint64_t i; double d; } u;
  u.i = (UINT64_C(0x3FF) << 52) | (value >> 12);
  return u.d - 1.0;
}

static inline uint64_t rotl(const uint64_t x, int k) {
  return (x << k) | (x >> (64 - k));
}

const size_t xoshiro256starstar::state_size;
xoshiro256starstar::state xoshiro256starstar::init(const uint64_t seed) {
  xoshiro256starstar::state new_state;
  for (size_t i = 0; i < state_size; ++i) new_state.s[i] = seed+i;
  return new_state;
}
  
xoshiro256starstar::state xoshiro256starstar::next(state s) {
  const uint64_t t = s.s[1] << 17;
  s.s[2] ^= s.s[0];
  s.s[3] ^= s.s[1];
  s.s[1] ^= s.s[2];
  s.s[0] ^= s.s[3];
  s.s[2] ^= t;
  s.s[3] = rotl(s.s[3], 45);
  return s;
}
  
uint64_t xoshiro256starstar::value(const state &s) {
  return rotl(s.s[1] * 5, 7) * 9;
}

uint64_t meme::gen_value() {
  std::unique_lock<std::mutex> lock(mutex);
  s = xoshiro256starstar::next(s);
  return xoshiro256starstar::value(s);
}

size_t meme::interval(const size_t max) {
  size_t num = 0;
  do {
    const uint64_t value = gen_value();
    const double num_float = prng_normalize(value);
    num = num_float * max;
  } while (num >= max);
  return num;
}