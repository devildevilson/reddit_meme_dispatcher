#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

class find : public drogon::HttpController<find> {
public:
  METHOD_LIST_BEGIN
  METHOD_ADD(find::get,"/{}",Get,Options);
  METHOD_LIST_END

  void get(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, std::string &&name);

  // static method? хотя поди можно сделать асинхронный вызов на самого себя, норм?
};