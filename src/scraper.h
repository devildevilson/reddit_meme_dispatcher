#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
#include <memory>

#include "pool.h"
#include <drogon/drogon.h>

#include "reddit_post.h"

namespace drogon {
  class HttpClient;
}

class scraper {
public:
  scraper(const size_t threads_count, std::string path); // std::string tmp_path

  void check_folders(const size_t no_older_than, const size_t maximum_size) const;
  void scrape(const std::string_view &current_subreddit, const std::string_view &duration);
  // надо добавить функцию которая выгрузит отдельный мем
  // причем эта функция наверное примет json с мемом

  void steal_post(const utility::reddit_post &post);
private:
  thread::pool pool;
  std::string path;
  std::string tmp_path;
  std::shared_ptr<drogon::HttpClient> main_client;
  std::shared_ptr<drogon::HttpClient> i_client;
  std::shared_ptr<drogon::HttpClient> v_client;

  void get_json_data_from_reddit(std::string subreddit, std::string meme_path, drogon::ReqResult result, const drogon::HttpResponsePtr &response);
  void download_image(std::string name, std::string meme_path, drogon::ReqResult result, const drogon::HttpResponsePtr &response);
  void download_dash_playlist(std::string url, std::string name, std::string meme_path, drogon::ReqResult result, const drogon::HttpResponsePtr &response);
  void download_video(std::string name, std::string meme_path, std::string audio_url, drogon::ReqResult result, const drogon::HttpResponsePtr &response);
  void download_audio(std::string name, std::string meme_path, drogon::ReqResult result, const drogon::HttpResponsePtr &response);
  void mux_video_audio(std::string video_name, std::string audio_name, std::string outname);
};