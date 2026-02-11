#pragma once

#include <CustomContext.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

class AVContext final {
public:
    AVContext(std::size_t width, std::size_t height, CustomContext* customContext);

    ~AVContext();

    void processImage(const std::vector<std::uint8_t>& frame, std::int64_t presentationTimestamp);

private:
    static const AVCodec* getCodec();

    std::size_t width;
    std::size_t height;
    bool initMessageSent = false;
    AVFormatContext* formatContext = nullptr;
    AVStream* stream = nullptr;
    AVCodecContext* codecContext = nullptr;
    SwsContext* swsContext = nullptr;
    AVFrame* frame = nullptr;
    AVPacket pkt;
};
