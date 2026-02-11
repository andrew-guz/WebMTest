#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

struct CustomContext {
    std::function<void(const uint8_t*, size_t)> writeFunction;
};
