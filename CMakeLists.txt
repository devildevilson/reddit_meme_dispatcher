cmake_minimum_required(VERSION 3.30)

project(reddit_meme_dispatcher) 

include(FetchContent)

enable_language(CXX)
set(CMAKE_CXX_STANDARD 23)

if(MSVC)
  add_compile_options("/W4" "$<$<CONFIG:RELEASE>:/O2>")
else()
  add_compile_options("-Wall" "-Wextra" "$<$<CONFIG:RELEASE>:-O3>" "$<$<CONFIG:Debug>:-g>") #"-fno-rtti"
  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    add_compile_options("-stdlib=libc++")
  else()
    # nothing special for gcc at the moment
  endif()
endif()  

if (NOT CMAKE_BUILD_TYPE)
  message("Build Type not set, defaulting to Debug...")
  set(CMAKE_BUILD_TYPE Debug)
  add_compile_definitions("DEBUG=$<CONFIG:Debug>")
endif()

if (CMAKE_BUILD_TYPE STREQUAL Release)
  message("CMAKE_CXX_FLAGS_RELEASE is ${CMAKE_CXX_FLAGS_RELEASE}")
endif()

FetchContent_Declare(
  drogon
  GIT_REPOSITORY https://github.com/drogonframework/drogon.git
  GIT_TAG        38dd5fea31a7a2727c0a6f6b6b04252374796cab
)

FetchContent_Declare(
  pugixml
  GIT_REPOSITORY https://github.com/zeux/pugixml.git
  GIT_TAG        ee86beb30e4973f5feffe3ce63bfa4fbadf72f38
)

FetchContent_Declare(
  glaze
  GIT_REPOSITORY https://github.com/stephenberry/glaze.git
  GIT_TAG        2f9db34c818a7a33c57b51762b153080c574ed26
)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        f355b3d58f7067eee1706ff3c801c2361011f3d5
)

FetchContent_MakeAvailable(drogon pugixml glaze spdlog)

find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(c-ares REQUIRED)
find_package(ZLIB REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET gstreamer-1.0 glib-2.0 libbrotlicommon libbrotlidec libbrotlienc jsoncpp)

include_directories(
  ${jsoncpp_INCLUDE_DIRS} 
  ${OpenSSL_INCLUDE_DIRS} 
  ${c-ares_INCLUDE_DIRS} 
  ${ZLIB_INCLUDE_DIRS}
)

link_libraries(
  ${jsoncpp_LIBRARIES}
  ${OpenSSL_LIBRARIES}
  ${c-ares_LIBRARIES}
  ${ZLIB_LIBRARIES}
)

set(SRC_LIST
  src/main.cpp
  src/pool.cpp
  src/random.cpp
  src/scraper.cpp
  src/parse_method.cpp
  src/utils.cpp
  src/audio_video_mux.cpp
  src/steal_method.cpp
)

add_executable(${PROJECT_NAME} ${SRC_LIST})
target_link_libraries(${PROJECT_NAME} ZLIB::ZLIB PkgConfig::deps drogon trantor pugixml::pugixml glaze::glaze spdlog::spdlog OpenSSL::SSL OpenSSL::Crypto c-ares::cares ZLIB::ZLIB)

install(TARGETS ${PROJECT_NAME} DESTINATION /usr/local/bin/)