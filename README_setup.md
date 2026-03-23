# PEGASUS HUD Setup Guide

## Overview

This project is a Qt-based C++ heads-up display (HUD) application for PEGASUS. It renders flight-style HUD data, ingests sensor data over UART using CBOR, and logs sessions, events, and flight samples to SQLite.

The intended deployment target is a Raspberry Pi 4 running Raspberry Pi OS.

## System Requirements

- Raspberry Pi 4 or similar ARM Linux system
- Raspberry Pi OS (Debian-based)
- Qt 5 development libraries
- C++ compiler (`g++`)
- CMake
- SQLite

## Required Dependencies

Update the system first:

```bash
sudo apt update
sudo apt full-upgrade
```

Install core build tools:

```bash
sudo apt install build-essential cmake git
```

Install Qt dependencies:

```bash
sudo apt install qtbase5-dev libqt5serialport5-dev
```

Install SQLite dependencies:

```bash
sudo apt install sqlite3 libsqlite3-dev libqt5sql5-sqlite
```

Notes:

- SQLite is embedded and serverless, so no separate database server setup is required.
- The Qt SQLite driver package `libqt5sql5-sqlite` is required for `QSqlDatabase` to open SQLite databases.

## Build Instructions

Clone the repository:

```bash
git clone <repo-url>
cd <repo-name>
```

Go to the Qt HUD project directory:

```bash
cd GUI/hud
```

Create a build directory:

```bash
mkdir build
cd build
```

Configure the project:

```bash
cmake ..
```

Build the HUD application:

```bash
make -j4
```

## Running the Application

Run the compiled HUD binary:

```bash
./hud
```

Optional useful modes:

```bash
./hud --dummy
./hud --dev
./hud --port /dev/ttyUSB0 --baud 115200
```

Notes:

- The SQLite database file is created automatically on startup.
- On Raspberry Pi OS, the database is typically stored under Qt's application data directory, usually something under `~/.local/share/`, with the file name `hud_logs.sqlite`.

## Logging / Database Notes

The application uses SQLite for persistent logging.

- SQLite stores everything in a single database file.
- No database server setup is required.
- The database is created automatically when the application starts.

The main tables are:

- `sessions`
- `event_logs`
- `flight_samples`

Logging behavior:

- `flight_samples` are logged at 1 Hz
- warnings and errors are recorded as structured events
- startup and shutdown activity is also logged

## Raspberry Pi Considerations

Raspberry Pi systems usually run from SD card storage, which is slower than desktop SSDs and can become a bottleneck if writes are too frequent.

This project reduces that risk by:

- keeping a single persistent SQLite connection open
- reusing prepared SQL statements
- batching writes into transactions
- enabling WAL mode for better write performance
- keeping flight sample logging low-frequency

This helps ensure that logging does not noticeably block the HUD rendering loop and minimizes unnecessary database writes.

## Troubleshooting

### QSQLITE driver not loaded

Install the Qt SQLite driver:

```bash
sudo apt install libqt5sql5-sqlite
```

### Database not created

Check:

- the application has permission to write to its data directory
- the target filesystem is writable
- the process is not running from a restricted environment

### App stuttering

Possible causes:

- SD card write latency
- other heavy system load
- excessive debug output

Checks:

- confirm batching/transaction-based logging is enabled in the current build
- use a good-quality SD card
- reduce other background tasks while testing

## Verifying Setup

After running the app once, locate the generated database file and open it with `sqlite3`.

Example:

```bash
sqlite3 ~/.local/share/*/hud_logs.sqlite
```

Inside the SQLite shell, run:

```sql
.tables
```

Expected tables:

- `sessions`
- `event_logs`
- `flight_samples`

## Quick Log Inspection

To view recent event logs quickly:

```bash
sqlite3 ~/.local/share/*/hud_logs.sqlite
```

Then run:

```sql
SELECT * FROM event_logs ORDER BY timestamp DESC LIMIT 10;
```
