# HUD Codebase Overview

## High-Level Architecture

This repository contains a Qt-based HUD application and related display code.

The active HUD application lives under:

- `GUI/hud/`

The current HUD path is intentionally simple:

1. `main.cpp` creates the Qt application and HUD widget
2. a data source provides `HudSample` values
3. `HudWidget` renders the current sample state
4. `HudLogger` records sessions, events, and periodic flight samples

## Core HUD Components

### `HudSample`

`GUI/hud/HudSample.h`

This is the decoded sample model shared across the application. It is the source of truth for field naming used by the logger and should be extended carefully.

Primary fields include:

- `tsMs`
- `altitudeFt`
- `vspeedFpm`
- `pressureHpa`
- `tempC`
- `ax`, `ay`, `az`
- `gx`, `gy`, `gz`
- `mx`, `my`, `mz`
- `rollDeg`, `pitchDeg`, `headingDeg`

It also carries timing/spec-test fields and nullable per-field `*_time_meas` values.

### `UartCborSource`

`GUI/hud/UartCborSource.h`
`GUI/hud/UartCborSource.cpp`

This is the live data ingest path. It supports:

- binary framed CBOR packets
- developer text lines

The CBOR decode path in `decodeCborToSample()` maps wire-format keys into the `HudSample` fields used throughout the app.

Important mappings:

- `ts_us -> tsMs`
- `alt -> altitudeFt`
- `vs -> vspeedFpm`
- `baro.p -> pressureHpa`
- `baro.T -> tempC`
- `imu.ax/ay/az -> ax/ay/az`
- `imu.gx/gy/gz -> gx/gy/gz`
- `mag.mx/my/mz -> mx/my/mz`
- `euler -> rollDeg/pitchDeg/headingDeg`

When the embedded source does not provide Euler angles, `computeAttitudeFallback()` derives attitude from accelerometer and magnetometer data.

### `HudWidget`

`GUI/hud/HudWidget.h`
`GUI/hud/HudWidget.cpp`

This is the Qt widget that renders the HUD. It owns only presentation state:

- heading
- roll
- pitch
- altitude
- vertical speed

The rendering path should stay UI-focused. Timing instrumentation is added around `paintEvent()` without changing the actual drawing design.

### `HudLogger`

`GUI/hud/HudLogger.h`
`GUI/hud/HudLogger.cpp`

This is the centralized SQLite logging layer.

Responsibilities:

- open a persistent SQLite connection
- create/update schema
- begin and end sessions
- queue event logs and periodic flight samples
- flush writes in short transactions
- keep database field names aligned with `HudSample`

## Data Flow

### Live UART mode

1. `main.cpp` starts `UartCborSource`
2. UART bytes arrive in `onReadyRead()`
3. framed payloads are parsed
4. CBOR is decoded into `HudSample`
5. `sampleReady` is emitted
6. `main.cpp` updates `HudWidget`
7. `main.cpp` also passes the latest sample to `HudLogger`
8. `HudLogger` snapshots one sample per second into `flight_samples`

### Dummy mode

1. `DummyDataSource` synthesizes a `HudSample`
2. `main.cpp` updates the widget and logger the same way as live mode

## Where Timing Is Measured

Timing instrumentation is intentionally placed near existing behavior:

- `UartCborSource::onReadyRead()` and frame parsing
- `UartCborSource::decodeCborToSample()`
- `HudWidget::paintEvent()`
- logger-side rolling summaries during sample insertion

Current metrics include:

- `sensor_read_ms`
- `decode_ms`
- `display_ms`
- `end_to_end_ms`
- `sensor_rate_hz`
- `display_rate_hz`

## Logging System Data Flow

The logger uses two internal stages:

1. collect/update in memory
2. flush queued rows to SQLite

This keeps the HUD path lightweight on Raspberry Pi 4 hardware.

Current behavior:

- latest sample is updated whenever the app receives new data
- a 1 Hz timer captures that latest sample
- events and samples are buffered
- a short flush timer writes buffered rows using transactions and prepared statements

## Safe Extension Guidelines

When extending the system:

1. Start from `HudSample` and `decodeCborToSample()` first.
2. Reuse the exact decoded field names in SQLite and documentation.
3. Add new `*_time_meas` columns only when the corresponding sample field exists.
4. Keep GUI changes separate from ingest/logging changes unless the UI truly needs them.
5. Prefer extending `HudLogger` internals over scattering direct SQLite calls elsewhere.
6. Keep sample logging low-rate unless a clearly defined high-rate mode is added.
7. If a new metric is optional, store `NULL` when unavailable instead of inventing a default.
8. For performance-sensitive paths, queue work and batch writes instead of writing directly per callback.

## Minimal-Risk Places To Modify

If you need to add new telemetry or timing:

- add the field to `HudSample`
- decode it in `UartCborSource`
- update `HudLogger` schema and prepared insert bindings
- document it in `README_logging.md`

This keeps the architecture localized and avoids redesigning the application.
