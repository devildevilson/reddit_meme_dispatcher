#include "parse.h"
#include "utils.h"

#include <drogon/drogon.h>
#include <glaze/glaze.hpp>
#include "spdlog/spdlog.h"

#include <string_view>
#include <optional>
#include <format>
#include <iostream>

struct reddit_post {
  std::string type;
  std::string name;
  std::string url;
  std::string title;
  std::string author;
  std::string subreddit;
  int64_t score;

  std::optional<std::string> content;
  std::optional<std::string> video_link;
  std::optional<std::string> audio_link;
  // галерея?
};

const std::string_view default_reddit_post_json_str = "{{posts:[{{\"type\":\"image\",\"url\":\"{}\",\"name\":\"\",\"title\":\"\",\"author\":\"\",\"subreddit\":\"\",\"score\":0}}]}}";

struct reddit_list {
  std::vector<reddit_post> posts;
};

constexpr bool is_whitespace(char c) {
  // Include your whitespaces here. The example contains the characters
  // documented by https://en.cppreference.com/w/cpp/string/wide/iswspace
  constexpr char matches[] = { ' ', '\n', '\r', '\f', '\v', '\t' };
  return std::any_of(std::begin(matches), std::end(matches), [c](char c0) { return c == c0; });
}

constexpr std::string_view trim(const std::string_view &input) {
  size_t right = 0; 
  size_t left = input.size() - 1;

  bool right_isspace = is_whitespace(input[right]);
  bool left_isspace = is_whitespace(input[left]);

  while (right <= left && (right_isspace || left_isspace)) {
    right += size_t(right_isspace);
    left -= size_t(left_isspace);
    right_isspace = is_whitespace(input[right]);
    left_isspace = is_whitespace(input[left]);
  }

  if (right >= left) return std::string_view();
  // это индексы поэтому +1
  return input.substr(right, left - right + 1);
}

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

static std::pair<ReqResult, HttpResponsePtr> http_get_sync(const std::string_view &url, const std::initializer_list<std::string_view> &header_list) {
  size_t path_start = url.find('/');
  const size_t protocol_index = url.find("://");
  if (protocol_index != std::string_view::npos) {
    path_start = url.find('/', protocol_index+3);
  }
  const auto full_host = url.substr(0, path_start);
  const auto path_part = url.substr(path_start);

  const auto next_loop_index = (app().getCurrentThreadIndex() + 1) % app().getThreadNum();
  auto loop = app().getIOLoop(next_loop_index);

  auto client = HttpClient::newHttpClient(std::string(full_host), loop);
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

  return client->sendRequest(req);
}

static std::string_view find_content_type(const HttpResponsePtr &response) {
  for (const auto &[ key, value ] : response->headers()) {
    if (key == "Content-Type" || key == "content-type") {
      return value;
    }
  }

  return std::string_view();
}

void parse::get(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, std::string &&raw_url) {
  // как принимать сюда ссылку? ссылку в каком виде? у реддита как будто несколько способов наименования мемов
  // парс должен вернуть что? json с нужными мне данными чтобы однозначно скачать мем
  // если мем скачать не получится, то можно все равно вернуть json с чем нибудь полезным
  // например тут можно соскрапить текстовый пост какой нибудь

  //utility::time_log log("parse");
  const auto tp = std::chrono::steady_clock::now();
  auto reddit_url = utils::urlDecode(raw_url);
  reddit_url = std::string(trim(reddit_url));

  spdlog::info("Parser: got url: {}", reddit_url);

  const bool plain_reddit_url = reddit_url.find("reddit.com") != std::string::npos;
  const bool image_reddit_url = reddit_url.find("i.redd.it") != std::string::npos;
  const bool video_reddit_url = reddit_url.find("v.redd.it") != std::string::npos;

  size_t path_start = reddit_url.find('/');
  const size_t protocol_index = reddit_url.find("://");
  auto host = std::string_view(reddit_url).substr(0, path_start);
  if (protocol_index != std::string_view::npos) {
    path_start = reddit_url.find('/', protocol_index+3);
    host = std::string_view(reddit_url).substr(protocol_index+3, path_start);
  }

  const auto path_part = path_start == std::string_view::npos ? std::string_view() : std::string_view(reddit_url).substr(path_start);

  const bool reddit_main_page = (host == "reddit.com" || host == "www.reddit.com") && (path_part.empty() || path_part == "/");

  // тут нужно вызвать как минимум http get по юрлке
  // как понять что url норм? она должна содержать как минимум reddit.com
  if (!plain_reddit_url && !image_reddit_url && !video_reddit_url) {
    auto resp = HttpResponse::newNotFoundResponse(req);
    resp->setContentTypeString("application/json");
    resp->setBody("{{\"error\":\"Could not parse url. Is it reddit url?\"}}");
    callback(resp);
    return;
  }

  // может быть ссылка типа reddit.com/blahblah....

  // в зависимости от типа ссылки мы пытаемся сделать несколько вещей:
  // обычная ссылка - получаем json со стороны реддита и смотрим что к нам пришло
  // ссылка на картинку - было бы неплохо подтвердить что это валидная ссылка, мы наверное можем по самой ссылке попробовать это сделать
  // возвращаем json: тип картинка, ссылка такая то
  // ссылка на видос - пытаемся загрузить плеер видоса и распарсить с него аудио и видеоссылки
  // возвращаем json: тип видео, аудио ссылка, видео ссылка

  if (plain_reddit_url) {
    if (reddit_url != "https://www.reddit.com/" && reddit_url != "www.reddit.com/" && reddit_url != "reddit.com/" && reddit_url.back() == '/') reddit_url.pop_back();
    if (!reddit_main_page) reddit_url += ".json";
    else reddit_url = "https://www.reddit.com/.json";

    http_get(reddit_url, {}, [tp, callback = std::move(callback), reddit_url] (drogon::ReqResult result, const drogon::HttpResponsePtr &response) {
      utility::time_log log("parse", tp);

      //std::cout << reddit_url << "\n";

      if (result != ReqResult::Ok) {
        const auto err = std::format("error while sending request to server! result: {}", to_string_view(result));
        spdlog::warn(err);
        auto resp = HttpResponse::newHttpResponse(k500InternalServerError, CT_APPLICATION_JSON);
        resp->setBody(std::format("{{\"error\":\"{}\"}}", err));
        callback(resp);
        return;
      }

      if (response->statusCode() != 200) {
        const auto err = std::format("request sent, but receive unexpected responce! code: {}", static_cast<int32_t>(response->statusCode()));
        spdlog::warn(err);
        auto resp = HttpResponse::newHttpResponse(k500InternalServerError, CT_APPLICATION_JSON);
        resp->setBody(std::format("{{\"error\":\"{}\"}}", err));
        callback(resp);
        return;
      }

      if (response->contentType() != CT_APPLICATION_JSON) {
        const std::string_view type = find_content_type(response);
        const auto err = std::format("request sent, responce ok, but strange content type! type: {} ({})", type, static_cast<int32_t>(response->contentType()));
        spdlog::warn(err);
        auto resp = HttpResponse::newHttpResponse(k500InternalServerError, CT_APPLICATION_JSON);
        resp->setBody(std::format("{{\"error\":\"{}\"}}", err));
        callback(resp);
        return;
      }

      // тут мы составляем json из того что к нам пришло

      JSONCPP_STRING json_err;
      const auto &json_str = response->getBody();
      //std::cout << json_str << "\n";
      Json::Value json;
      Json::CharReaderBuilder builder;
      const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
      if (!reader->parse(json_str.data(), json_str.data() + json_str.size(), &json, &json_err)) {
        const auto err = std::format("Could not parse responce body for url '{}' error: {}", reddit_url, json_err);
        spdlog::warn(err);
        auto resp = HttpResponse::newHttpResponse(k500InternalServerError, CT_APPLICATION_JSON);
        resp->setBody(std::format("{{\"error\":\"{}\"}}", err));
        callback(resp);
        return;
      }

      // какой ответ? по идее может быть 2 варианта: либо json[0]["data"]["children"] либо json["data"]["children"]
      Json::Value childs;
      if (json.isObject()) {
        childs = json["data"]["children"];
      } else {
        childs = json[0]["data"]["children"];
      }

      reddit_list list;
      for (int i = 0; i < int(childs.size()); ++i) {
        const auto &meme_data = childs[i]["data"];
        const auto domain = meme_data["domain"].asString();

        reddit_post post;
        post.url = meme_data["url"].asString();
        post.name = meme_data["name"].asString();
        post.title = meme_data["title"].asString();
        post.author = meme_data["author"].asString();
        post.subreddit = meme_data["subreddit"].asString();
        post.score = meme_data["score"].asInt64();
        const auto text = meme_data["selftext"].asString();
        post.content = text.empty() ? std::nullopt : std::make_optional(text);
        const bool is_video = meme_data["is_video"].asBool();

        if (is_video) post.type = "video";
        else if (domain == "i.redd.it") post.type = "image";
        else post.type = "other";

        if (is_video) {
          // нужно найти и распарсить ссылки на аудио и видеоканалы, как делаем?
          // скачиваем дашплейлист и парсим это дело
          auto dash_url = post.url;
          if (dash_url.back() != '/') dash_url.push_back('/');
          dash_url.append("DASHPlaylist.mpd");

          const auto [ res, response ] = http_get_sync(dash_url, {});
          if (res != ReqResult::Ok) {
            spdlog::warn("error while sending request to server! result: {}", to_string_view(res));
            continue;
          }

          const auto body = std::string(response->getBody());
          const auto [ video, audio, err ] = utility::parse_reddit_dash_playlist(post.url, body);
          if (!err.empty()) {
            spdlog::warn(err);
            continue;
          }

          post.video_link = video;
          post.audio_link = audio;
        }

        list.posts.emplace_back(std::move(post));
      }

      std::string buffer;
      auto ec = glz::write_json(list, buffer);
      if (ec) {
        const auto descriptive_error = glz::format_error(ec, buffer);
        throw std::runtime_error(descriptive_error);
      }
      
      auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
      resp->setBody(std::move(buffer));
      callback(resp);
    });
    return;
  } else if (image_reddit_url) {
    utility::time_log log("parse", tp);
    auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
    const auto err = std::format(default_reddit_post_json_str, reddit_url);
    resp->setBody(err);
    callback(resp);
    return;
  } 

  // video_reddit_url
  utility::time_log log("parse", tp);

  auto dash_url = reddit_url;
  if (dash_url.back() != '/') dash_url.push_back('/');
  dash_url.append("DASHPlaylist.mpd");
  const auto [ res, r ] = http_get_sync(dash_url, {});
  if (res != ReqResult::Ok) {
    const auto err = std::format("error while sending request to server! result: {}", to_string_view(res));
    spdlog::warn(err);
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError, CT_APPLICATION_JSON);
    resp->setBody(std::format("{{\"error\":\"{}\"}}", err));
    callback(resp);
    return;
  }

  const auto body = std::string(r->getBody());
  const auto [ video, audio, err ] = utility::parse_reddit_dash_playlist(reddit_url, body);
  if (!err.empty()) {
    spdlog::warn(err);
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError, CT_APPLICATION_JSON);
    resp->setBody(std::format("{{\"error\":\"{}\"}}", err));
    callback(resp);
    return;
  }

  auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
  resp->setBody(std::format("{{posts:[{{\"type\":\"video\",\"url\":\"{}\",\"name\":\"\",\"title\":\"\",\"author\":\"\",\"subreddit\":\"\",\"score\":0,\"video_link\":\"{}\",\"audio_link\":\"{}\"}}]}}", reddit_url, video, audio));
  callback(resp);
}

