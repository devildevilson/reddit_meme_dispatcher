#pragma once

#include <drogon/HttpController.h>
#include <mutex>

using namespace drogon;

double prng_normalize(const uint64_t value);

struct xoshiro256starstar {
  static const size_t state_size = 4;
  struct state { 
    using outer = xoshiro256starstar;
    uint64_t s[state_size]; 
  };
  static state init(const uint64_t seed);
  static state next(state s);
  static uint64_t value(const state &s);
};

class meme : public drogon::HttpController<meme> {
public:
  METHOD_LIST_BEGIN
  METHOD_ADD(meme::get,"",Get,Options);
  METHOD_LIST_END

  meme();

  void get(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);

  // я же легко могу обойтись без мьютекса
  uint64_t gen_value();
  size_t interval(const size_t max);
private:
  std::mutex mutex;
  xoshiro256starstar::state s;
};
