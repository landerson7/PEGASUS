#pragma once
#include <cstdint>
#include <string>
#include <vector>

class SSD1306 {
public:
    SSD1306(const std::string &i2cDev = "/dev/i2c-1", uint8_t addr = 0x3C);
    ~SSD1306();

    bool init();
    void clear();
    void update(const std::vector<uint8_t> &buffer); // 128x64 = 1024 bytes
    static constexpr int Width  = 128;
    static constexpr int Height = 32;
    static constexpr int BufferSize = Width * Height / 8; // 1bpp

private:
    int fd_ = -1;
    uint8_t addr_;
    bool writeCommand(uint8_t cmd);
    bool writeData(const uint8_t *data, size_t len);
};
