#pragma once

#include <cstdint>
#include <cstddef>

#include <drogon/HttpController.h>

using namespace drogon;

class recheck : public drogon::HttpController<recheck> {
public:
  METHOD_LIST_BEGIN
  METHOD_ADD(recheck::get,"",Get,Options);
  METHOD_LIST_END

  void get(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);
};