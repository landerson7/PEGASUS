#pragma once
#include <cstdint>
#include <string>
#include <vector>

class SSD1306 {
public:
    static constexpr int Width  = 128;
    static constexpr int Height = 64;        // change to 32 if your panel is 128x32
    static constexpr int Pages  = Height / 8;
    static constexpr int BufferSize = Width * Pages;

    SSD1306(const std::string &i2cDev = "/dev/i2c-1", uint8_t addr = 0x3C);
    ~SSD1306();

    bool isOpen() const { return fd_ >= 0; }
    bool init();
    void clear();
    void update(const std::vector<uint8_t> &buffer);

private:
    int fd_ = -1;
    uint8_t addr_;

    bool writeCommand(uint8_t cmd);
    bool writeData(const uint8_t *data, size_t len);
};
