#include "recheck.h"

#include <string>
#include "utils.h"

using namespace drogon;

void recheck::get(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
  utility::time_log l("recheck");

  const size_t days_seconds = utility::global::settings().every_N_days * 24 * 60 * 60;
  utility::global::check_folders(days_seconds, utility::global::settings().max_size);

  auto resp = HttpResponse::newHttpResponse();
  resp->setContentTypeString("application/json");
  resp->setBody("{ \"message\": \"Folders recheck successful\" }");
  callback(resp);
}