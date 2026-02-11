#include <crow.h>
#include <crow/app.h>
#include <crow/http_request.h>
#include <crow/websocket.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/codec_id.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}

// --- структура для передачи callback ---
struct WSContext {
    std::function<void(const uint8_t*, size_t)> send_func;
};

// --- функция для WebSocket-пакета ---
int write_packet_cb(void* opaque, const uint8_t* buf, int buf_size) {
    WSContext* ctx = (WSContext*)opaque;
    if (ctx && ctx->send_func) {
        std::vector<uint8_t> msg(buf_size);
        memcpy(msg.data(), buf, buf_size);
        ctx->send_func(msg.data(), msg.size());
    }
    return buf_size;    // FFmpeg считает, что записали всё
}

// Генератор "картинок" для примера
void generate_dummy_frames(std::vector<std::vector<uint8_t>>& frames, int width, int height) {
    const int steps = 100;
    const double step = 256.0 / steps;
    for (int i = 0; i < steps; ++i) {
        frames.emplace_back(width * height * 4, 0);
        auto& frame = frames.back();
        // делаем разный цвет
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                frame[4 * (y * width + x) + 0] = 255 - ((double)i * step);    // R
                frame[4 * (y * width + x) + 1] = (double)i * step;            // G
                frame[4 * (y * width + x) + 2] = 0;                           // B
                frame[4 * (y * width + x) + 3] = 255;                         // A
            }
        }
    }
}

int main() {
    const int width = 320;
    const int height = 240;
    const int fps = 30;
    const int frame_duration_ms = 1000 / fps;

    // --- инициализация ffmpeg ---
    avformat_network_init();

    AVFormatContext* fmt_ctx = nullptr;
    avformat_alloc_output_context2(&fmt_ctx, nullptr, "webm", nullptr);

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_VP8);
    AVStream* stream = avformat_new_stream(fmt_ctx, codec);
    stream->time_base = AVRational { 1, 1000 };    // миллисекунды

    AVCodecContext* c = avcodec_alloc_context3(codec);
    c->width = width;
    c->height = height;
    c->pix_fmt = AV_PIX_FMT_YUV420P;
    c->time_base = stream->time_base;
    c->gop_size = 25;
    c->max_b_frames = 0;

    if (avcodec_open2(c, codec, nullptr) < 0) {
        std::cerr << "Could not open codec\n";
        return -1;
    }
    avcodec_parameters_from_context(stream->codecpar, c);

    // --- кастомный AVIOContext ---
    uint8_t* buffer = (uint8_t*)av_malloc(32768);
    WSContext ws_ctx;
    crow::websocket::connection* connection = nullptr;
    ws_ctx.send_func = [&](const uint8_t* data, size_t size) {
        // здесь шлём данные по WebSocket
        // псевдокод:
        std::cout << "WS send chunk size: " << size << "\n";
        connection->send_binary(std::string { (const char*)data, size });
    };

    // --- SwsContext для RGBA -> YUV420 ---
    SwsContext* sws_ctx = sws_getContext(width,
                                         height,
                                         AV_PIX_FMT_RGBA,
                                         width,
                                         height,
                                         AV_PIX_FMT_YUV420P,
                                         SWS_BILINEAR,
                                         nullptr,
                                         nullptr,
                                         nullptr);

    AVFrame* frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = width;
    frame->height = height;
    av_frame_get_buffer(frame, 32);

    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = nullptr;
    pkt.size = 0;

    // --- генерируем тестовые кадры ---
    std::vector<std::vector<uint8_t>> frames;
    generate_dummy_frames(frames, width, height);

    int64_t pts = 0;
    size_t index = 0;

    const auto processFrames = [&]() {
        AVIOContext* avio_ctx =
                avio_alloc_context(buffer, 32768, 1, &ws_ctx, nullptr, write_packet_cb, nullptr);
        fmt_ctx->pb = avio_ctx;
        fmt_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;

        // --- пишем заголовок WebM (init segment) ---
        if (avformat_write_header(fmt_ctx, nullptr) < 0) {
            std::cerr << "Error writing header\n";
            return;
        }

        // --- главный loop, 10 секунд ---
        while (pts < 10000) {
            auto& rgba = frames[index];

            uint8_t* in_data[1] = { rgba.data() };
            int in_linesize[1] = { 4 * width };
            sws_scale(sws_ctx, in_data, in_linesize, 0, height, frame->data, frame->linesize);

            frame->pts = pts;

            avcodec_send_frame(c, frame);
            while (avcodec_receive_packet(c, &pkt) == 0) {
                av_interleaved_write_frame(fmt_ctx,
                                           &pkt);    // пойдёт в write_packet_cb
                av_packet_unref(&pkt);
            }

            pts += frame_duration_ms;
            index = (index + 1) % frames.size();

            // имитация нерегулярной генерации
            std::this_thread::sleep_for(std::chrono::milliseconds(10 + (rand() % 20)));
        }
    };

    crow::SimpleApp app;

    CROW_WEBSOCKET_ROUTE(app, "/stream")
            .onaccept([&](const crow::request&, void**) { return true; })
            .onopen([&](crow::websocket::connection& conn) {
                connection = &conn;
                std::thread thread { processFrames };
                thread.detach();
            })
            .onclose([&](crow::websocket::connection&, const std::string&, uint16_t) {
                // flush кодек
                avcodec_send_frame(c, nullptr);
                while (avcodec_receive_packet(c, &pkt) == 0) {
                    av_interleaved_write_frame(fmt_ctx, &pkt);
                    av_packet_unref(&pkt);
                }

                // финализируем WebM
                av_write_trailer(fmt_ctx);

                sws_freeContext(sws_ctx);
                av_frame_free(&frame);
                // --- кастомный AVIOContext ---
                if (fmt_ctx->pb) {
                    av_free(fmt_ctx->pb->buffer);    // буфер, который мы выделили
                    av_free(fmt_ctx->pb);            // сам AVIOContext
                    fmt_ctx->pb = nullptr;
                }
                avformat_free_context(fmt_ctx);

                std::cout << "Streaming done!\n";

                app.stop();
            });

    app.port(8080).multithreaded().run();
}
