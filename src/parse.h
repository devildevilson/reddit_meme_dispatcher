#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

class parse : public drogon::HttpController<parse> {
public:
  METHOD_LIST_BEGIN
  METHOD_ADD(parse::get,"?url={1}",Get,Options);
  METHOD_LIST_END

  void get(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, std::string &&reddit_url);

  // static method? хотя поди можно сделать асинхронный вызов на самого себя, норм?
};