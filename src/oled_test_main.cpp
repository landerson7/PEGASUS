// src/oled_test_main.cpp
#include "ssd1306.h"
#include <vector>
#include <chrono>
#include <thread>

int main() {
    SSD1306 oled("/dev/i2c-1", 0x3C);
    if (!oled.init()) {
        return 1;
    }

    std::vector<uint8_t> buf(SSD1306::BufferSize, 0x00);

    // simple pattern: every other pixel on
    for (int y = 0; y < SSD1306::Height; ++y) {
        for (int x = 0; x < SSD1306::Width; ++x) {
            if ((x + y) % 2 == 0) {
                int byteIndex = x + (y / 8) * SSD1306::Width;
                int bit = y % 8;
                buf[byteIndex] |= (1 << bit);
            }
        }
    }

    oled.update(buf);
    std::this_thread::sleep_for(std::chrono::seconds(10));
    oled.clear();
    return 0;
}
