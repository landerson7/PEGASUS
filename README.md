# PEGASUS HUD

PEGASUS HUD is a C++ / Qt-based heads-up display for Raspberry Pi 4. It receives IMU and altimeter telemetry from an ESP32 over UART, renders flight-style attitude and altitude data in real time, and logs sessions plus flight data to SQLite for later analysis.

The active HUD application lives in `PEGASUS/GUI/hud/`. After a successful build, the executable is:

```text
/home/pi/PEGASUS/GUI/hud/build/hud
```

## Project Overview

The HUD is designed to run on a Raspberry Pi 4 with a connected display and an ESP32 telemetry source. The ESP32 sends CBOR packets over UART containing barometric altitude, vertical speed, and IMU data. The Raspberry Pi decodes that stream, updates the HUD in real time, and records application sessions, events, and periodic flight samples to a local SQLite database.

Typical repository paths:

```text
PEGASUS/
|- GUI/hud/              # Qt HUD application
|- GUI/hud/build/        # Build output
|- GUI/hud/start.sh      # Clean build + launch script
|- README_setup.md       # Additional setup notes
|- README_logging.md     # Logging/database notes
`- README_codebase.md    # Codebase overview
```

## Hardware Requirements

- Raspberry Pi 4
- microSD card with Raspberry Pi OS installed
- HDMI or DSI display
- ESP32 connected over UART
- Stable 5V / 3A power supply for the Raspberry Pi 4
- Clean power budget for the display and ESP32 if they are powered from the same system

## Software Requirements

- Raspberry Pi OS, 64-bit preferred
- Qt6 preferred
- Qt5 supported as a fallback by the current HUD CMake configuration
- CMake
- Ninja, optional but preferred
- GCC and `build-essential`
- SQLite and the Qt SQLite driver
- Git

## Initial Raspberry Pi Setup

Update the Pi first:

```bash
sudo apt update
sudo apt full-upgrade -y
```

Install the preferred dependency set for Qt6:

```bash
sudo apt install -y \
  git \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  qt6-base-dev \
  qt6-base-dev-tools \
  qt6-serialport-dev \
  libqt6sql6-sqlite \
  sqlite3 \
  libsqlite3-dev
```

If your Raspberry Pi OS image does not provide the Qt6 packages above, install the Qt5 fallback set instead:

```bash
sudo apt install -y \
  qtbase5-dev \
  qtbase5-dev-tools \
  libqt5serialport5-dev \
  libqt5sql5-sqlite
```

Enable UART on the Raspberry Pi:

1. Run `sudo raspi-config`
2. Open `Interface Options`
3. Open `Serial Port`
4. Select `No` for `Would you like a login shell to be accessible over serial?`
5. Select `Yes` for `Would you like the serial port hardware to be enabled?`
6. Exit and reboot

Optional but recommended: add the runtime user to the serial device group and reboot once more.

```bash
sudo usermod -a -G dialout pi
sudo reboot
```

## Clone the Repository

Clone the project into `/home/pi`:

```bash
cd /home/pi
git clone https://github.com/landerson7/PEGASUS.git
cd PEGASUS
```

## Build Instructions

Build the HUD from the `GUI/hud` directory:

```bash
cd /home/pi/PEGASUS/GUI/hud
rm -rf build
cmake -S . -B build -G Ninja
cmake --build build
cd build
./hud
```

If `ninja-build` is not installed, use the default generator instead:

```bash
cd /home/pi/PEGASUS/GUI/hud
rm -rf build
cmake -S . -B build
cmake --build build
cd build
./hud
```

The included `start.sh` script performs a clean rebuild and then launches the app:

```bash
cd /home/pi/PEGASUS/GUI/hud
chmod +x start.sh
./start.sh
```

## Running the HUD

Manual run:

```bash
/home/pi/PEGASUS/GUI/hud/build/hud
```

Useful runtime options:

```bash
/home/pi/PEGASUS/GUI/hud/build/hud --dummy
/home/pi/PEGASUS/GUI/hud/build/hud --dev
/home/pi/PEGASUS/GUI/hud/build/hud --small-display
/home/pi/PEGASUS/GUI/hud/build/hud --port /dev/serial0 --baud 115200
```

Expected behavior:

- The HUD opens full-screen on the external display when one is available.
- `--dev` forces the HUD onto the primary display for desktop development.
- `--small-display` adjusts text sizing for the 640x480 test display.
- The app shows startup test values, then begins updating from ESP32 UART telemetry.
- If UART cannot be opened, the app continues in dummy mode so the display path can still be verified.
- SQLite logging starts automatically when the app launches.

## Auto-Start on Boot (systemd)

Build the project once before enabling the service so that `/home/pi/PEGASUS/GUI/hud/build/hud` exists.

Create the service file:

```bash
sudo nano /etc/systemd/system/pegasus.service
```

Example service file:

```ini
[Unit]
Description=PEGASUS HUD
After=graphical.target
Wants=graphical.target

[Service]
Type=simple
User=pi
Group=pi
WorkingDirectory=/home/pi/PEGASUS/GUI/hud
Environment=DISPLAY=:0
Environment=XDG_RUNTIME_DIR=/run/user/1000
ExecStart=/home/pi/PEGASUS/GUI/hud/build/hud --small-display --port /dev/serial0 --baud 115200
Restart=on-failure
RestartSec=2

[Install]
WantedBy=graphical.target
```

If you are using a larger display, remove `--small-display` from `ExecStart`.

If you run the service as a user other than `pi`, update `User=`, `Group=`, and `XDG_RUNTIME_DIR=/run/user/1000` to match that account.

Enable and manage the service:

```bash
sudo systemctl daemon-reload
sudo systemctl enable pegasus.service
sudo systemctl start pegasus.service
sudo systemctl status pegasus.service
```

Useful service diagnostics:

```bash
sudo journalctl -u pegasus.service -n 100 --no-pager
sudo journalctl -u pegasus.service -f
```

## Logging (SQLite)

The HUD stores logs in a SQLite database created under Qt's `QStandardPaths::AppDataLocation`. When you run the app as user `pi`, the database is typically created somewhere under:

```text
/home/pi/.local/share/
```

Locate the database:

```bash
find ~/.local/share -type f -name hud_logs.sqlite
```

Open the first match with `sqlite3`:

```bash
DB_PATH="$(find ~/.local/share -type f -name hud_logs.sqlite | head -n 1)"
sqlite3 "$DB_PATH"
```

Useful SQLite checks:

```sql
.tables
SELECT id, start_time, end_time, exit_status FROM sessions ORDER BY id DESC LIMIT 5;
SELECT timestamp, altitudeFt, vspeedFpm, headingDeg FROM flight_samples ORDER BY id DESC LIMIT 10;
SELECT timestamp, level, component, event_type, message FROM event_logs ORDER BY id DESC LIMIT 10;
```

## Troubleshooting

### Qt plugin errors

Symptoms:

- `Could not load the Qt platform plugin`
- `This application failed to start because no Qt platform plugin could be initialized`

Fixes:

- Reinstall the Qt development and runtime packages listed above.
- Make sure the Qt major version installed on the Pi matches what CMake found during configuration.
- Delete the build directory and configure again after changing Qt packages.
- If the issue is specifically the XCB backend, install the missing support package:

```bash
sudo apt install -y libxcb-cursor0
```

### Display issues

Symptoms:

- The app launches on the wrong screen
- The HUD is too large or too small
- The systemd service starts but nothing appears on screen

Fixes:

- Use `--dev` to force rendering on the primary display while testing.
- Use `--small-display` for the 640x480 HUD test screen.
- Make sure the display is connected before boot if the Pi should treat it as the primary output.
- If the service has GUI access issues, confirm `DISPLAY=:0` and `XDG_RUNTIME_DIR=/run/user/1000` are present in the service file.
- On Raspberry Pi OS Bookworm, if desktop-session launch still fails under systemd, test manual launch from the logged-in desktop session first and consider switching the desktop session from Wayland to X11.

### UART not receiving data

Symptoms:

- No live telemetry appears
- The app falls back to dummy mode
- `/dev/serial0` opens but no valid samples are decoded

Fixes:

- Confirm the ESP32 TX line is connected to the Raspberry Pi RX line and that ground is shared.
- Verify the baud rate matches the ESP32 sender. The HUD default is `115200`.
- Confirm the port path. The default is `/dev/serial0`.
- Make sure the serial login console is disabled in `raspi-config`.
- Inspect raw serial output directly:

```bash
stty -F /dev/serial0 115200
cat /dev/serial0
```

- Stop the `cat` command with `Ctrl+C` after confirming data is present.

### Permissions

Symptoms:

- `Permission denied` opening `/dev/serial0`
- The HUD works manually but not as a service

Fixes:

- Add user `pi` to the `dialout` group:

```bash
sudo usermod -a -G dialout pi
sudo reboot
```

- Make sure the service runs as `User=pi` rather than `root`.
- Ensure `start.sh` is executable if you intend to use it directly:

```bash
chmod +x /home/pi/PEGASUS/GUI/hud/start.sh
```

### Build failures

Symptoms:

- CMake cannot find Qt
- `ninja` is missing
- Linker errors after package changes

Fixes:

- Re-run the dependency install commands and confirm the correct Qt packages are installed.
- If Qt6 packages are unavailable on your image, install the Qt5 fallback packages.
- Remove the old build directory and configure from scratch:

```bash
cd /home/pi/PEGASUS/GUI/hud
rm -rf build
cmake -S . -B build -G Ninja
cmake --build build
```

- If `ninja-build` is not installed, omit `-G Ninja`.

## Development Notes

Fast rebuild after source changes:

```bash
cd /home/pi/PEGASUS/GUI/hud
cmake --build build -j"$(nproc)"
```

Useful source locations:

- `GUI/hud/main.cpp` is the main application entry point.
- `GUI/hud/HudWidget.cpp` and `GUI/hud/HudWidget.h` contain the widget layout and paint logic.
- `GUI/hud/UartCborSource.cpp` handles UART ingest and CBOR decoding.
- `GUI/hud/HudLogger.cpp` handles SQLite logging.

UI scaling:

- The quickest runtime adjustment is `--small-display`.
- Code-level scaling is currently set in `GUI/hud/main.cpp` with `setTextScale()` and `setNumericScale()`.
- If you need deeper layout changes, update the geometry and font sizing in `GUI/hud/HudWidget.cpp`.

The included `start.sh` script always deletes `build/`, performs a clean configure/build, and then launches `./hud`. That is useful for deployment smoke tests, but it is slower than `cmake --build build` during normal iteration.

## Optional: Display Configuration

For Raspberry Pi OS Bookworm, HDMI timing is usually configured in:

```text
/boot/firmware/config.txt
```

On older Raspberry Pi OS images, the file may instead be:

```text
/boot/config.txt
```

Example for forcing 640x480 HDMI output for HUD testing:

```bash
sudo nano /boot/firmware/config.txt
```

Add or update:

```ini
hdmi_force_hotplug=1
hdmi_group=2
hdmi_mode=4
```

Then reboot:

```bash
sudo reboot
```

Notes:

- `hdmi_group=2` and `hdmi_mode=4` correspond to 640x480 at 60 Hz.
- For small HDMI panels, combine the display timing above with the HUD's `--small-display` option.
- For DSI panels, prefer the panel's native resolution and use HUD scaling options before forcing non-native timings.
