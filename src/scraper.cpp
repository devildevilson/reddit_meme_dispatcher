#include "scraper.h"

#include <filesystem>
#include <stdexcept>
#include <fstream>
#include <format>

#include "utils.h"
#include "spdlog/spdlog.h"
#include "pugixml.hpp"

#include "audio_video_mux.h"

namespace fs = std::filesystem;

using namespace drogon;

scraper::scraper(const size_t threads_count_, std::string path_) : //std::string tmp_path_
  pool(threads_count_), 
  path(std::move(path_)), 
  //tmp_path(std::move(tmp_path_)),
  main_client(HttpClient::newHttpClient("https://www.reddit.com/")),
  i_client(HttpClient::newHttpClient("https://i.redd.it/")),
  v_client(HttpClient::newHttpClient("https://v.redd.it/"))
{
  if (path.back() != '/') path.push_back('/');

  if (!fs::exists(path)) {
    fs::create_directory(path);
  }

  if (!fs::is_directory(path)) {
    throw std::runtime_error("'"+path+"' is not a directory");
  }

  std::error_code ec;
  const auto tmp_folder = fs::temp_directory_path(ec);
  if (ec != std::error_code{}) {
    throw std::runtime_error(ec.message());
  }

  spdlog::info("Using tmp directory '{}'", tmp_folder.generic_string());

  std::vector<std::string> paths;
  for (const auto &entry : fs::recursive_directory_iterator(path)) {
    if (!entry.is_regular_file()) continue;

    auto path = entry.path().generic_string();
    paths.emplace_back(std::move(path));
  }

  utility::global::unique_files()->setup_staging(std::move(paths));
  utility::global::unique_files()->swap_buffers();

  main_client->setUserAgent("redditHomeProject v1 by Manatrimyss");
  i_client->setUserAgent("redditHomeProject v1 by Manatrimyss");
  v_client->setUserAgent("redditHomeProject v1 by Manatrimyss");
}

void scraper::check_folders(const size_t no_older_than, const size_t maximum_size) const {
  std::vector<std::string> existing;
  std::vector<std::string> to_remove;
  utility::staging_func f([&existing, &to_remove] () {
    utility::global::unique_files()->setup_staging(std::move(existing));
    utility::global::unique_files()->swap_buffers();

    for (const auto &path : to_remove) {
      fs::remove(path);
      spdlog::info("Removed: {}", path);
    }
  });

  std::vector<fs::directory_entry> entries;
  size_t overall_size = 0;
  for (const auto &entry : fs::recursive_directory_iterator(path)) {
    if (!entry.is_regular_file()) continue;

    overall_size += entry.file_size();
    entries.push_back(entry);
  }

  if (entries.empty()) return;

  // сортируем по самым старым
  std::sort(entries.begin(), entries.end(), [] (const auto &a, const auto &b) -> bool {
    std::error_code ec;
    return a.last_write_time(ec) < b.last_write_time(ec);
  });

  const auto tp_now = std::chrono::system_clock::now();
  std::error_code ec;
  const auto ft = entries.back().last_write_time(ec);
  const auto t = std::chrono::file_clock::to_sys(ft);
  size_t seconds = std::chrono::duration_cast<std::chrono::seconds>(tp_now - t).count();
  while ((seconds > no_older_than || overall_size > maximum_size) && !entries.empty()) {
    overall_size -= entries.back().file_size();
    auto path = entries.back().path().generic_string();
    to_remove.emplace_back(std::move(path));
    entries.pop_back();

    if (entries.empty()) break;

    const auto ft = entries.back().last_write_time(ec);
    const auto t = std::chrono::file_clock::to_sys(ft);
    seconds = std::chrono::duration_cast<std::chrono::seconds>(tp_now - t).count();
  }

  for (const auto &entry : entries) {
    existing.push_back(entry.path().generic_string());
  }

  spdlog::info("Size of all files is {} ({} mb)", overall_size, (double(overall_size)/1024.0/1024.0));
}

template <typename F>
static void http_get(HttpClient* client, const std::string_view &url, const std::initializer_list<std::string_view> &header_list, F&& f) {
  size_t path_start = url.find('/');
  const size_t protocol_index = url.find("://");
  if (protocol_index != std::string_view::npos) {
    path_start = url.find('/', protocol_index+3);
  }
  //const auto full_host = url.substr(0, path_start);
  const auto path_part = url.substr(path_start);
  //auto client = HttpClient::newHttpClient(std::string(full_host));
  //client->setUserAgent("client 1224");
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

  spdlog::info("Request: {}", path_part);
}

static std::string_view find_content_type(const HttpResponsePtr &response) {
  for (const auto &[ key, value ] : response->headers()) {
    if (key == "Content-Type" || key == "content-type") {
      //std::cout << "Content-Type: " << value << "\n";
      return value;
    }
  }

  return std::string_view();
}

static void print_headers(const HttpResponsePtr &response) {
  for (const auto &[ key, value ] : response->headers()) {
    std::cout << key << ": " << value << "\n";
  }
}

template <typename T>
static size_t count_mcs(T tp1, T tp2) {
  const auto dur = tp2 - tp1;
  const size_t ret = std::chrono::duration_cast<std::chrono::microseconds>(dur).count();
  return ret;
}

void scraper::get_json_data_from_reddit(std::string subreddit, std::string meme_path, drogon::ReqResult result, const drogon::HttpResponsePtr &response) {
  if (result != ReqResult::Ok) {
    spdlog::warn("error while sending request to server! result: {}", to_string_view(result));
    return;
  }

  if (response->statusCode() != 200) {
    spdlog::warn("request sent, but receive unexpected responce! code: {}", static_cast<int32_t>(response->statusCode()));
    return;
  }

  if (response->contentType() != CT_APPLICATION_JSON) {
    const std::string_view type = find_content_type(response);
    spdlog::warn("request sent, responce ok, but strange content type! type: {} ({})", type, static_cast<int32_t>(response->contentType()));
    return;
  }

  //std::cout << response->getBody() << std::endl;
  //const auto ret = utility::write_file(final_path + ".json", response->getBody());

  JSONCPP_STRING err;
  const auto &json_str = response->getBody();
  Json::Value json;
  Json::CharReaderBuilder builder;
  const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  if (!reader->parse(json_str.data(), json_str.data() + json_str.size(), &json, &err)) {
    spdlog::warn("Could not parse responce body for subreddit 'r/{}' error: {}", subreddit, err);
    return;
  }

  // какой ответ?
  const auto &childs = json["data"]["children"];
  // compiler segfault if 'i' is 'size_t'
  for (int i = 0; i < int(childs.size()); ++i) {
    const auto &meme_data = childs[i]["data"];
    auto url = meme_data["url"].asString();
    auto name = meme_data["name"].asString();
    //const auto ups = meme_data["ups"].asInt();
    const bool is_video = meme_data["is_video"].asBool();

    // просто пост без картинки можно отделить с помощью поля domain
    // оно должно быть либо v.redd.it либо i.redd.it
    // если не нашли что то такое, то пропускаем
    // + нужно сделать фильтр по mime type

    if (is_video) {
      std::string dash_url;
      if (meme_data["media"].isObject() && meme_data["media"]["reddit_video"].isObject()) {
        dash_url = meme_data["media"]["reddit_video"].get("dash_url", "").asString();
        dash_url = dash_url.substr(0, dash_url.find("?"));
      }

      if (dash_url.empty()) {
        spdlog::warn("Could not find dash_url of video '{}'", url);
        continue;
      }

      auto playlist_f = std::bind(&scraper::download_dash_playlist, this, std::move(url), std::move(name), meme_path, std::placeholders::_1, std::placeholders::_2);
      http_get(this->v_client.get(), dash_url, {}, std::move(playlist_f));
    } else {
      auto img_f = std::bind(&scraper::download_image, this, std::move(name), meme_path, std::placeholders::_1, std::placeholders::_2);
      http_get(this->i_client.get(), url, {}, std::move(img_f));
    }
  }
}

void scraper::download_image(std::string name, std::string meme_path, drogon::ReqResult result, const drogon::HttpResponsePtr &response) {
  if (result != ReqResult::Ok) {
    spdlog::warn("error while sending request to server! result: {}", to_string_view(result));
    return;
  }

  auto type = std::string(find_content_type(response));

  if (type != "image/jpeg" && type != "image/png" && type != "image/bmp" && type != "image/gif") {
    spdlog::warn("expected image, got {}", type);
    return;
  }

  // как украсть боди из респонса не копируя? похоже что никак
  auto body = std::string(response->getBody());
  this->pool.submitbase([type = std::move(type), name = std::move(name), final_path = std::move(meme_path), body = std::move(body)] () {
    const auto ext = type.substr(type.rfind('/')+1); // poor man mime type parser
    const auto meme_file_fullname = final_path + "/" + name + "." + std::string(ext); // std::to_string(ups) + "_" + 

    // блокирующая операция, нужно выкинуть ее как раз в другой поток
    const auto ret = utility::write_file_bin(meme_file_fullname, body);
    if (ret != utility::io_state::ok) {
      spdlog::warn("Could not write meme file: {}", meme_file_fullname);
    } else {
      spdlog::info("Steal {}", meme_file_fullname);
    }
  }); // submitbase
}

void scraper::download_dash_playlist(std::string url, std::string name, std::string meme_path, drogon::ReqResult result, const drogon::HttpResponsePtr &response) {
  if (result != ReqResult::Ok) {
    spdlog::warn("error while sending request to server! result: {}", to_string_view(result));
    return;
  }

  //print_headers(response);
  auto type = std::string(find_content_type(response));

  // expect playlist in xml format
  auto body = std::string(response->getBody()); // по идее это XML строка
  pugi::xml_document doc;
  const auto xml_res = doc.load_string(body.c_str());
  if (!xml_res) {
    spdlog::warn("Could not parse xml '{}', reason: {}", url, xml_res.description());
    //std::cout << body << "\n";
    return;
  }

  // expect different quality level video/audio parts
  std::string video_base;
  std::string audio_base;
  for (auto node = doc.child("MPD").child("Period").child("AdaptationSet"); node; node = node.next_sibling("AdaptationSet")) {
    size_t bandwidth = 0;
    std::string set_type = node.attribute("contentType").value();
    for (auto repr = node.child("Representation"); repr; repr = repr.next_sibling("Representation")) {
      size_t bw = repr.attribute("bandwidth").as_int();
      std::string val = repr.child_value("BaseURL");
      if (bw > bandwidth) {
        bandwidth = bw;
        if (set_type == "video") video_base = std::move(val);
        if (set_type == "audio") audio_base = std::move(val);
      }
    }
  }

  if (video_base.empty()) {
    spdlog::warn("Could not get video base url from data");
    //std::cout << body << "\n";
    return;
  }

  url = url.back() == '/' ? url : url + "/";
  // предположим что мы что то даже нашли теперь нужно скачать оба файла
  auto video_url = url + video_base;
  auto audio_url = audio_base.empty() ? audio_base : url + audio_base;

  auto video_f = std::bind(&scraper::download_video, this, std::move(name), std::move(meme_path), std::move(audio_url), std::placeholders::_1, std::placeholders::_2);
  http_get(this->v_client.get(), video_url, {}, std::move(video_f));
}

void scraper::download_video(std::string name, std::string meme_path, std::string audio_url, drogon::ReqResult result, const drogon::HttpResponsePtr &response) {
  if (result != ReqResult::Ok) {
    spdlog::warn("error while sending request to server! result: {}", to_string_view(result));
    return;
  }

  //print_headers(response);
  auto type = std::string(find_content_type(response));

  // ожидаем тут тип видеофайла
  auto body = std::string(response->getBody());
  this->pool.submitbase([type = std::move(type), name, meme_path, body = std::move(body)] () {
    // у нас есть расширение у url, но лучше использовать mime type
    const auto ext = type.substr(type.rfind('/')+1);
    const auto meme_filename = name + "_video." + std::string(ext);
    //const auto tmp_dir = fs::temp_directory_path().generic_string();
    const auto meme_file_fullname = (fs::temp_directory_path() / meme_filename).generic_string();

    // блокирующая операция, нужно выкинуть ее как раз в другой поток
    const auto ret = utility::write_file_bin(meme_file_fullname, body);
    if (ret != utility::io_state::ok) {
      spdlog::warn("Could not write meme file: {}", meme_file_fullname);
    } else {
      spdlog::info("Steal {}", meme_file_fullname);
    }
  }); // submitbase

  if (!audio_url.empty()) {
    auto audio_f = std::bind(&scraper::download_audio, this, std::move(name), std::move(meme_path), std::placeholders::_1, std::placeholders::_2);
    http_get(this->v_client.get(), audio_url, {}, std::move(audio_f));
  }
}

void scraper::download_audio(std::string name, std::string meme_path, drogon::ReqResult result, const drogon::HttpResponsePtr &response) {
  if (result != ReqResult::Ok) {
    spdlog::warn("error while sending request to server! result: {}", to_string_view(result));
    return;
  }

  //print_headers(response);
  auto type = std::string(find_content_type(response));

  // ожидаем тут тип видеофайла
  auto body = std::string(response->getBody());
  this->pool.submitbase([this, type = std::move(type), name = std::move(name), meme_path = std::move(meme_path), body = std::move(body)] () {
    // у нас есть расширение у url, но лучше использовать mime type
    const auto ext = type.substr(type.rfind('/')+1);
    //const auto tmp_dir = fs::temp_directory_path().generic_string();
    auto audio_file_fullname = (fs::temp_directory_path() / (name + "_audio." + std::string(ext))).generic_string();
    auto video_file_fullname = (fs::temp_directory_path() / (name + "_video." + std::string(ext))).generic_string();
    auto  meme_file_fullname = meme_path + "/" + name + "." + std::string(ext);

    // блокирующая операция, нужно выкинуть ее как раз в другой поток
    const auto ret = utility::write_file_bin(audio_file_fullname, body);
    if (ret != utility::io_state::ok) {
      spdlog::warn("Could not write meme file: {}", audio_file_fullname);
      audio_file_fullname.clear();
    } else {
      spdlog::info("Steal {}", audio_file_fullname);
    }

    // теперь их надо замиксить
    // вообще можно прям тут вызвать
    this->pool.submitbase([this, audio_file_fullname = std::move(audio_file_fullname), video_file_fullname = std::move(video_file_fullname), meme_file_fullname = std::move(meme_file_fullname)] () {
      this->mux_video_audio(std::move(video_file_fullname), std::move(audio_file_fullname), std::move(meme_file_fullname));
    });
  }); // submitbase
}

void scraper::mux_video_audio(std::string video_name, std::string audio_name, std::string outname) {
  // проверить все ли на месте? предполагаем что запись на диск быстрее чем получение данных по HTTP?
  if (audio_name.empty()) {
    spdlog::warn("Audio file output error, remove video: {}", video_name);
    fs::remove(video_name);
    return;
  }

  // тут что делаем? подключаем гстреамер и переделываем звуки и видео
  const auto ret = utility::audio_video_mux(audio_name, video_name, outname);
  if (ret != 0) {
    spdlog::warn("Could not mux audio and video from files: {}, {}", audio_name, video_name);
  } else {
    spdlog::info("Successfuly muxed audio/video meme '{}'", outname);
    fs::remove(audio_name);
    fs::remove(video_name);
  }
}

void scraper::scrape(const std::string_view &current_subreddit, const std::string_view &duration) {
  auto final_path = path + std::string(current_subreddit);
  if (!fs::exists(final_path)) {
    fs::create_directory(final_path);
  }

  if (!fs::is_directory(final_path)) {
    throw std::runtime_error("'"+final_path+"' is not a directory");
  }

  auto url = std::format("https://www.reddit.com/r/{}/top.json?sort=top&t={}", current_subreddit, duration);
  auto reddit_f = std::bind(&scraper::get_json_data_from_reddit, this, std::string(current_subreddit), std::move(final_path), std::placeholders::_1, std::placeholders::_2);
  http_get(this->main_client.get(), url, {}, reddit_f);

  const auto tp = std::chrono::steady_clock::now();
  auto end = std::chrono::steady_clock::now();
  while (pool.tasks_count() == 0 && count_mcs(tp, end) < 1000000) { 
    std::this_thread::sleep_for(std::chrono::microseconds(1));
    end = std::chrono::steady_clock::now(); 
  }
  pool.compute();
  pool.wait();

  spdlog::info("Scraping ends");
}

void scraper::steal_post(const utility::reddit_post &post) {
  auto final_path = path + std::string(post.subreddit.empty() ? "_" : post.subreddit);

  if (post.type == "image") {
    auto img_f = std::bind(&scraper::download_image, this, post.name, std::move(final_path), std::placeholders::_1, std::placeholders::_2);
    http_get(this->i_client.get(), url, {}, std::move(img_f));
    return;
  }

  if (post.type == "video") {
    auto video_f = std::bind(&scraper::download_video, this, post.name, std::move(final_path), post.audio_link.value_or(std::string()), std::placeholders::_1, std::placeholders::_2);
    http_get(this->v_client.get(), post.video_link.value(), {}, std::move(video_f));
  }

  spdlog::warn("Skip downloading meme '{}', url: {}", post.name, post.url);
}