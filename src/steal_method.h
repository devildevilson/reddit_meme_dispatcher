#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

class steal : public drogon::HttpController<steal> {
public:
  METHOD_LIST_BEGIN
  METHOD_ADD(steal::get,"?url={1}",Get,Options);
  METHOD_LIST_END

  void get(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, std::string &&reddit_url);
};