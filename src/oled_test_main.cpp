#include "ssd1306.h"

#include <vector>
#include <chrono>
#include <thread>
#include <iostream>

int main() {
    SSD1306 oled("/dev/i2c-1", 0x3C);

    if (!oled.isOpen()) {
        std::cerr << "Failed to open I2C\n";
        return 1;
    }
    if (!oled.init()) {
        std::cerr << "Failed to init SSD1306\n";
        return 1;
    }

    std::cout << "Init OK, drawing patterns...\n";

    std::vector<uint8_t> buf(SSD1306::BufferSize);

    // 1. Full white
    std::fill(buf.begin(), buf.end(), 0xFF);
    oled.update(buf);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 2. Full black
    std::fill(buf.begin(), buf.end(), 0x00);
    oled.update(buf);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 3. Checkerboard
    std::fill(buf.begin(), buf.end(), 0x00);
    for (int y = 0; y < SSD1306::Height; ++y) {
        for (int x = 0; x < SSD1306::Width; ++x) {
            if (((x / 8) + (y / 8)) % 2 == 0) {
                int page = y / 8;
                int bit  = y % 8;
                buf[page * SSD1306::Width + x] |= (1 << bit);
            }
        }
    }
    oled.update(buf);
    std::this_thread::sleep_for(std::chrono::seconds(4));

    // 4. Vertical bar in the center
    std::fill(buf.begin(), buf.end(), 0x00);
    int midX = SSD1306::Width / 2;
    for (int y = 0; y < SSD1306::Height; ++y) {
        int page = y / 8;
        int bit  = y % 8;
        buf[page * SSD1306::Width + midX] |= (1 << bit);
    }
    oled.update(buf);
    std::this_thread::sleep_for(std::chrono::seconds(4));

    oled.clear();
    return 0;
}
