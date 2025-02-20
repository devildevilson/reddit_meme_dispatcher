FROM alpine:3.21 AS build
RUN apk update && apk add --no-cache cmake make jsoncpp-dev openssl-dev c-ares-dev zlib-dev brotli-dev gstreamer-dev git g++
WORKDIR /reddit_meme_dispatcher
COPY src/ ./src/
COPY CMakeLists.txt .
WORKDIR /reddit_meme_dispatcher/build
RUN cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SQLITE=OFF -DUSE_SPDLOG=ON ..
RUN cmake --build . --parallel 8

FROM alpine:3.21 AS release
RUN apk update && apk add --no-cache libstdc++ openssl gstreamer c-ares jsoncpp brotli libuuid gst-plugins-good gst-plugins-bad gst-plugins-ugly
EXPOSE 8080
RUN addgroup -S appgroup && adduser -S appuser -G appgroup
USER appuser
WORKDIR /app
COPY --chown=appgroup:appuser --from=build /reddit_meme_dispatcher/build/reddit_meme_dispatcher ./
ENTRYPOINT [ "/app/reddit_meme_dispatcher" ]