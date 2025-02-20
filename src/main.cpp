#include <cstdint>
#include <cstddef>
#include <iostream>
#include <chrono>
#include <stop_token>
#include <thread>
#include <condition_variable>

#include <drogon/drogon.h>
#include "scraper.h"
#include "utils.h"

#include "spdlog/spdlog.h"
#include <gst/gst.h>

using namespace drogon;

const std::string_view settings_path = "./scraper.json";

int main(int argc, char *argv[]) {
  gst_init (&argc, &argv);

  auto sets = utility::scraper_settings_construct();
  try {
    sets = utility::parse_json(std::string(settings_path));
  } catch(const std::exception &e) {
    spdlog::warn("Error '{}'. Skip. Using default one", e.what());
    // сохранить json на диск
    auto buffer = create_json(sets);
    utility::write_file(settings_path, buffer);
  }

  const uint16_t port = sets.port;
  const uint16_t log_level = sets.log_level;

  {
    utility::global g;
    g.init_scraper(4, sets.folder);
    g.init_settings(std::move(sets));
  }

  std::stop_source stop_source;
  std::jthread scraper_thread([] (std::stop_token stoken) {
    utility::scraper_run(stoken);
  }, stop_source.get_token());

  app().setLogPath("./")
       .setLogLevel(static_cast<trantor::Logger::LogLevel>(log_level)) //  trantor::Logger::kWarn
       .addListener("0.0.0.0", port)
       .setThreadNum(8)
       //.enableRunAsDaemon()
       .run();

  stop_source.request_stop();

  return 0;
}