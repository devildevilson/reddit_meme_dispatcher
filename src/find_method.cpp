#include "find_method.h"
#include "utils.h"

#include <drogon/drogon.h>
#include <glaze/glaze.hpp>
#include "spdlog/spdlog.h"

#include <string_view>
#include <optional>
#include <format>
#include <iostream>

void find::get(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, std::string &&name) {
  utility::time_log log("find");

  const auto memepath = utility::global::unique_files()->call([this, &name] (const std::vector<std::string> &paths) {
    for (const auto &path : paths) {
      if (path.find(name) != std::string::npos) return path;
    }
    return std::string();
  });

  if (memepath.empty()) {
    auto resp = HttpResponse::newNotFoundResponse(req);
    resp->setContentTypeString("application/json");
    resp->setBody(std::format("{{\"error\":\"Could not find meme with name '{}'\"}}", name));
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