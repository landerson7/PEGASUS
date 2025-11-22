#include "ssd1306.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#include <algorithm>
#include <cstring>
#include <iostream>

SSD1306::SSD1306(const std::string &i2cDev, uint8_t addr)
    : addr_(addr)
{
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
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

bool SSD1306::writeCommand(uint8_t cmd) {
    if (fd_ < 0) return false;
    uint8_t buf[2] = { 0x00, cmd };   // 0x00 = control byte for command
    return ::write(fd_, buf, 2) == 2;
}

bool SSD1306::writeData(const uint8_t *data, size_t len) {
    if (fd_ < 0) return false;

    size_t offset = 0;
    while (offset < len) {
        size_t chunk = std::min(len - offset, static_cast<size_t>(16));
        uint8_t buf[1 + 16];
        buf[0] = 0x40; // 0x40 = control byte for data
        std::memcpy(buf + 1, data + offset, chunk);
        if (::write(fd_, buf, chunk + 1) != static_cast<ssize_t>(chunk + 1)) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

bool SSD1306::init() {
    if (fd_ < 0) return false;

    // Init sequence based on Adafruit SSD1306 for 128x64 / 128x32
    if (!writeCommand(0xAE)) return false;          // display off

    if (!writeCommand(0xD5)) return false;          // display clock divide
    if (!writeCommand(0x80)) return false;          // suggested ratio

    if (!writeCommand(0xA8)) return false;          // multiplex
    if (!writeCommand(Height - 1)) return false;    // 0x3F for 64, 0x1F for 32

    if (!writeCommand(0xD3)) return false;          // display offset
    if (!writeCommand(0x00)) return false;          // no offset

    if (!writeCommand(0x40 | 0x00)) return false;   // start line = 0

    if (!writeCommand(0x8D)) return false;          // charge pump
    if (!writeCommand(0x14)) return false;          // enable

    if (!writeCommand(0x20)) return false;          // memory mode
    if (!writeCommand(0x00)) return false;          // horizontal addressing mode (unused here, we use page mode manually)

    if (!writeCommand(0xA1)) return false;          // segment remap
    if (!writeCommand(0xC8)) return false;          // COM scan dec

    if (!writeCommand(0xDA)) return false;          // COM pins config
    if (!writeCommand(Height == 64 ? 0x12 : 0x02)) return false; // 0x12 for 128x64, 0x02 for 128x32

    if (!writeCommand(0x81)) return false;          // contrast
    if (!writeCommand(0xCF)) return false;

    if (!writeCommand(0xD9)) return false;          // pre-charge
    if (!writeCommand(0xF1)) return false;

    if (!writeCommand(0xDB)) return false;          // VCOM detect
    if (!writeCommand(0x40)) return false;

    if (!writeCommand(0xA4)) return false;          // display resume
    if (!writeCommand(0xA6)) return false;          // normal (non-inverted)

    if (!writeCommand(0xAF)) return false;          // display on

    clear();
    return true;
}

void SSD1306::clear() {
    std::vector<uint8_t> buf(BufferSize, 0x00);
    update(buf);
}

void SSD1306::update(const std::vector<uint8_t> &buffer) {
    if (fd_ < 0) return;
    if (buffer.size() != BufferSize) {
        std::cerr << "SSD1306::update: wrong buffer size: "
                  << buffer.size() << " expected " << BufferSize << "\n";
        return;
    }

    for (int page = 0; page < Pages; ++page) {
        // Set current page address (0xB0..0xB7)
        writeCommand(0xB0 + page);
        // Set column start to 0
        writeCommand(0x00);        // low column
        writeCommand(0x10);        // high column

        const uint8_t* ptr = &buffer[page * Width];
        writeData(ptr, Width);
    }
}
