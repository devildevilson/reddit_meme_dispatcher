#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <optional>
#include <vector>

namespace utility {
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

struct reddit_list {
  std::vector<reddit_post> posts;
};
}