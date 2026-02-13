#pragma once
#include <optional>
#include "HudSample.h"

class IDataSource {
public:
    virtual ~IDataSource() = default;

    // Non-blocking: returns a sample if a new one is available
    virtual std::optional<HudSample> poll() = 0;
};
