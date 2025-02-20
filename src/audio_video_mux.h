#pragma once

#include <string>
#include <cstdint>

namespace utility {
int32_t audio_video_mux(const std::string &audio_file, const std::string &video_file, const std::string &outfile);
}