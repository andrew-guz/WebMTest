
#include <AVContext.hpp>
#include <CustomContext.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/codec_id.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}

AVContext::AVContext(const std::size_t width,
                     const std::size_t height,
                     CustomContext* customContext)
        : width(width), height(height) {
    av_log_set_level(AV_LOG_ERROR);

    this->formatContext = nullptr;
    avformat_alloc_output_context2(&this->formatContext, nullptr, "webm", nullptr);

    const AVCodec* codec = AVContext::getCodec();
    this->stream = avformat_new_stream(this->formatContext, codec);
    this->stream->time_base = AVRational { 1, 1000 };    // миллисекунды

    this->codecContext = avcodec_alloc_context3(codec);
    this->codecContext->width = this->width;
    this->codecContext->height = this->height;
    this->codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    this->codecContext->time_base = this->stream->time_base;
    this->codecContext->gop_size = 25;
    this->codecContext->max_b_frames = 0;

    avcodec_open2(this->codecContext, codec, nullptr);

    avcodec_parameters_from_context(this->stream->codecpar, this->codecContext);

    this->swsContext = sws_getContext(this->width,
                                      this->height,
                                      AV_PIX_FMT_RGBA,
                                      this->width,
                                      this->height,
                                      AV_PIX_FMT_YUV420P,
                                      SWS_BILINEAR,
                                      nullptr,
                                      nullptr,
                                      nullptr);

    this->frame = av_frame_alloc();
    this->frame->format = AV_PIX_FMT_YUV420P;
    this->frame->width = this->width;
    this->frame->height = this->height;
    av_frame_get_buffer(this->frame, 32);

    av_init_packet(&this->pkt);
    this->pkt.data = nullptr;
    this->pkt.size = 0;

    // инициализируем контекс с кастомным обработчиком полученных данных
    const std::size_t buffer_size = 32768;
    AVIOContext* avio_ctx =
            avio_alloc_context((std::uint8_t*)av_malloc(buffer_size),
                               buffer_size,
                               1,
                               customContext,
                               nullptr,
                               [](void* opaque, const uint8_t* buf, int buf_size) {
                                   CustomContext* ctx = (CustomContext*)opaque;
                                   if (ctx && ctx->writeFunction) {
                                       ctx->writeFunction(buf, buf_size);
                                   }
                                   return buf_size;    // FFmpeg считает, что записали всё
                               },
                               nullptr);
    this->formatContext->pb = avio_ctx;
    this->formatContext->flags |= AVFMT_FLAG_CUSTOM_IO;
}

AVContext::~AVContext() {
    // flush кодек
    avcodec_send_frame(this->codecContext, nullptr);
    while (avcodec_receive_packet(this->codecContext, &this->pkt) == 0) {
        av_interleaved_write_frame(this->formatContext, &this->pkt);
        av_packet_unref(&this->pkt);
    }

    // финализируем WebM
    av_write_trailer(this->formatContext);

    sws_freeContext(this->swsContext);
    av_frame_free(&this->frame);
    // --- кастомный AVIOContext ---
    if (this->formatContext->pb) {
        av_free(this->formatContext->pb->buffer);    // буфер, который мы выделили
        av_free(this->formatContext->pb);            // сам AVIOContext
        this->formatContext->pb = nullptr;
    }
    avformat_free_context(this->formatContext);
}

void AVContext::processImage(const std::vector<std::uint8_t>& frame,
                             const std::int64_t presentationTimestamp) {
    if (!initMessageSent) {
        // пишем заголовок WebM (init segment)
        static_cast<void>(avformat_write_header(this->formatContext, nullptr));
        initMessageSent = true;
    }

    const std::uint8_t* data = frame.data();
    int linesize = 4 * this->width;
    sws_scale(this->swsContext,
              &data,
              &linesize,
              0,
              this->height,
              this->frame->data,
              this->frame->linesize);

    this->frame->pts = presentationTimestamp;

    avcodec_send_frame(this->codecContext, this->frame);
    while (avcodec_receive_packet(this->codecContext, &this->pkt) == 0) {
        av_interleaved_write_frame(this->formatContext, &this->pkt);
        av_packet_unref(&this->pkt);
    }
}

const AVCodec* AVContext::getCodec() {
    return avcodec_find_encoder(AV_CODEC_ID_VP8);
}
