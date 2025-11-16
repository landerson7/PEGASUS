#include "ssd1306.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <cstring>
#include <iostream>

SSD1306::SSD1306(const std::string &i2cDev, uint8_t addr)
    : addr_(addr) {
    fd_ = ::open(i2cDev.c_str(), O_RDWR);
    if (fd_ < 0) {
        std::perror("open i2c");
        return;
    }
    if (ioctl(fd_, I2C_SLAVE, addr_) < 0) {
        std::perror("ioctl I2C_SLAVE");
        ::close(fd_);
        fd_ = -1;
    }
}

SSD1306::~SSD1306() {
    if (fd_ >= 0) ::close(fd_);
}

bool SSD1306::writeCommand(uint8_t cmd) {
    if (fd_ < 0) return false;
    uint8_t buf[2] = {0x00, cmd}; // 0x00 = command
    return ::write(fd_, buf, 2) == 2;
}

bool SSD1306::writeData(const uint8_t *data, size_t len) {
    if (fd_ < 0) return false;
    // We need a control byte 0x40 followed by data; send in chunks
    size_t offset = 0;
    while (offset < len) {
        size_t chunk = std::min(len - offset, (size_t)16);
        uint8_t buf[17];
        buf[0] = 0x40; // data
        std::memcpy(buf + 1, data + offset, chunk);
        if (::write(fd_, buf, chunk + 1) != (ssize_t)(chunk + 1))
            return false;
        offset += chunk;
    }
    return true;
}

bool SSD1306::init() {
    if (fd_ < 0) return false;

    // Basic init sequence for 128x64 SSD1306
    if (!writeCommand(0xAE)) return false; // display off
    if (!writeCommand(0x20)) return false; // memory addressing mode
    if (!writeCommand(0x00)) return false; // horizontal
    if (!writeCommand(0x40)) return false; // start line = 0
    if (!writeCommand(0xA1)) return false; // segment remap
    if (!writeCommand(0xC8)) return false; // COM scan direction
    if (!writeCommand(0x81)) return false; // contrast
    if (!writeCommand(0x7F)) return false;
    if (!writeCommand(0xA4)) return false; // display follows RAM
    if (!writeCommand(0xA6)) return false; // normal display
    if (!writeCommand(0xD5)) return false; // clock divide
    if (!writeCommand(0x80)) return false;
    if (!writeCommand(0xD9)) return false; // pre-charge
    if (!writeCommand(0xF1)) return false;
    if (!writeCommand(0xDA)) return false; // COM pins
    if (!writeCommand(0x02)) return false;
    if (!writeCommand(0xDB)) return false; // VCOM detect
    if (!writeCommand(0x40)) return false;
    if (!writeCommand(0x8D)) return false; // charge pump
    if (!writeCommand(0x14)) return false;
    if (!writeCommand(0xAF)) return false; // display on
    return true;
}

void SSD1306::clear() {
    std::vector<uint8_t> buf(BufferSize, 0x00);
    update(buf);
}

void SSD1306::update(const std::vector<uint8_t> &buffer) {
    if (buffer.size() != BufferSize) return;

    // Set column & page addresses
    writeCommand(0x21); // column addr
    writeCommand(0x00);
    writeCommand(Width - 1);
    writeCommand(0x22); // page addr
    writeCommand(0x00);
    writeCommand((Height / 8) - 1);

    writeData(buffer.data(), buffer.size());
}
