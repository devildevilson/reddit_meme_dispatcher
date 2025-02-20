#include "audio_video_mux.h"

#include <format>
#include <gst/gst.h>
#include "spdlog/spdlog.h"

namespace utility {

int32_t audio_video_mux(const std::string &audio_file, const std::string &video_file, const std::string &outfile) {
  // инициализируем гстреамер в другом месте
  GError *err = nullptr;
  int32_t ret = 0;

  const auto pipeline_str = std::format(
    "filesrc location=\"{}\" ! qtdemux ! h264parse " // avdec_h264
    "! queue ! h264parse ! qtmux name=mux ! filesink location=\"{}\"  " // x264enc
    "filesrc location=\"{}\" ! qtdemux ! decodebin ! audioconvert ! audioresample ! opusenc ! queue ! mux.",
    video_file, outfile, audio_file
  );

  //spdlog::info("Using gstreamer pipeline '{}'", pipeline_str);

  // судя по всему пайплайн будет довольно простым
  auto pipeline = gst_parse_launch(pipeline_str.c_str(), &err);
  if (err != nullptr) {
    g_print(err->message);
    ret = -1;
  } else {
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    auto bus = gst_element_get_bus(pipeline);
    auto msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
    if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
      g_printerr("An error occurred! Re-run with the GST_DEBUG=*:WARN environment variable set for more details.\n");
      ret = -1;
    }

    gst_message_unref(msg);
    gst_object_unref(bus);
  }

  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
  return ret;
}

}