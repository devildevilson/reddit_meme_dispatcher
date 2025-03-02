#include "steal_method.h"
#include "utils.h"
#include "reddit_post.h"

#include <drogon/drogon.h>
#include <glaze/glaze.hpp>
#include "spdlog/spdlog.h"

#include <string_view>
#include <optional>
#include <format>
#include <iostream>

template <typename F>
static void http_get(const std::string_view &url, const std::initializer_list<std::string_view> &header_list, F&& f) {
  size_t path_start = url.find('/');
  const size_t protocol_index = url.find("://");
  if (protocol_index != std::string_view::npos) {
    path_start = url.find('/', protocol_index+3);
  }
  const auto full_host = url.substr(0, path_start);
  const auto path_part = url.substr(path_start);
  auto client = HttpClient::newHttpClient(std::string(full_host));
  client->setUserAgent("redditHomeProject v1 by Manatrimyss");
  auto req = HttpRequest::newHttpRequest();
  req->setMethod(drogon::Get);
  req->setPath(std::string(path_part));
  for (const auto h : header_list) {
    const size_t colon_index = h.find(':');
    const auto name = h.substr(0, colon_index);
    const auto value = h.substr(colon_index+1);
    req->addHeader(std::string(name), std::string(value));
  }

  client->sendRequest(req, std::move(f));

  //spdlog::info("Request: {}", path_part);
}

void steal::get(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, std::string &&raw_url) {
  const auto port = utility::global::settings().port;
  const auto url = std::format("http://localhost:{}/parse/?url={}", port, raw_url);
  http_get(url, {}, [callback = std::move(callback)] (drogon::ReqResult result, const drogon::HttpResponsePtr &response) {
    utility::time_log log("steal");

    if (result != ReqResult::Ok) {
      const auto err = std::format("error while sending request to server! result: {}", to_string_view(result));
      spdlog::warn(err);
      auto resp = HttpResponse::newHttpResponse(k500InternalServerError, CT_APPLICATION_JSON);
      resp->setBody(std::format("{{\"error\":\"{}\"}}", err));
      callback(resp);
      return;
    }

    // делаем примерно тоже самое что и в скрапере, как этот функционал совместить?

    utility::reddit_list list;
    auto ec = glz::read_json(list, response->getBody());
    if (ec) {
      const auto descriptive_error = glz::format_error(ec, response->getBody());
      throw std::runtime_error(descriptive_error);
    }

    for (const auto &post : list.posts) {
      if (post.type == "image") {
        utility::global::steal_post(post);
        continue;
      }

      if (post.type == "video") {
        utility::global::steal_post(post);
        continue;
      }

      spdlog::warn("Could not steal '{}', url: {}. Skip", post.name, post.url);
    }

    auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
    resp->setBody(std::string(response->getBody()));
    callback(resp);
  });
}