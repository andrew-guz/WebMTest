#include <crow.h>
#include <crow/app.h>
#include <crow/http_request.h>
#include <crow/websocket.h>

#include <AVContext.hpp>
#include <CustomContext.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
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
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
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

    const auto processFrames = [width, height](crow::websocket::connection* connection) {
        // --- генерируем тестовые кадры ---
        std::vector<std::vector<uint8_t>> frames;
        generate_dummy_frames(frames, width, height);

        // --- кастомный AVIOContext ---
        CustomContext customContext;
        customContext.writeFunction = [connection](const uint8_t* data, size_t size) {
            // здесь шлём данные по WebSocket
            // псевдокод:
            std::cout << "WS send chunk size: " << size << "\n";
            connection->send_binary(std::string { (const char*)data, size });
        };

        int64_t pts = 0;
        size_t index = 0;

        AVContext context { width, height, &customContext };

        // --- главный loop, 10 секунд ---
        while (pts < 10000) {
            context.processImage(frames[index], pts);

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
                std::thread thread { std::bind(processFrames, &conn) };
                thread.detach();
            })
            .onclose([&](crow::websocket::connection&, const std::string&, uint16_t) {});

    app.port(8080).multithreaded().run();
}
