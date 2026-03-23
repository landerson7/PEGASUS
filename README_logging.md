# HUD SQLite Logging

## Summary

The HUD includes a centralized SQLite logger implemented in `GUI/hud/HudLogger.*`.
It records:

- one `sessions` row per application run
- structured `event_logs` rows for startup, shutdown, warnings, and errors
- one `flight_samples` row per second with the latest decoded HUD data

The logger uses the decoded `HudSample` field names as the source of truth for database columns. This keeps the schema aligned with the existing UART/CBOR ingest path in `UartCborSource::decodeCborToSample()`.

## Database Location

The database path is created from Qt's application data directory:

- `QStandardPaths::AppDataLocation/hud_logs.sqlite`

## Schema

### `sessions`

One row per application run.

Columns:

- `id`
- `start_time`
- `end_time`
- `build_version`
- `git_branch`
- `git_commit`
- `exit_status`
- `notes`

### `event_logs`

Structured event records for application, UART, sensor, display, and database events.

Columns:

- `id`
- `session_id`
- `timestamp`
- `level`
- `component`
- `event_type`
- `message`
- `details`
- `error_code`

### `flight_samples`

One row per second containing the latest decoded sample plus timing/spec-test fields.

Decoded sample columns:

- `tsMs`
- `altitudeFt`
- `vspeedFpm`
- `pressureHpa`
- `tempC`
- `ax`
- `ay`
- `az`
- `gx`
- `gy`
- `gz`
- `mx`
- `my`
- `mz`
- `rollDeg`
- `pitchDeg`
- `headingDeg`

Timing and spec-test columns:

- `sensor_read_ms`
- `decode_ms`
- `display_ms`
- `end_to_end_ms`
- `sensor_rate_hz`
- `display_rate_hz`
- `invalid_packets`
- `dropped_packets`
- `stale_data_flag`
- `data_valid`
- `data_fresh`

Per-field nullable measurement timing columns:

- `altitudeFt_time_meas`
- `vspeedFpm_time_meas`
- `pressureHpa_time_meas`
- `tempC_time_meas`
- `ax_time_meas`
- `ay_time_meas`
- `az_time_meas`
- `gx_time_meas`
- `gy_time_meas`
- `gz_time_meas`
- `mx_time_meas`
- `my_time_meas`
- `mz_time_meas`
- `rollDeg_time_meas`
- `pitchDeg_time_meas`
- `headingDeg_time_meas`

Rolling timing summary columns:

- `sensor_read_ms_min`
- `sensor_read_ms_max`
- `sensor_read_ms_avg`
- `decode_ms_min`
- `decode_ms_max`
- `decode_ms_avg`
- `display_ms_min`
- `display_ms_max`
- `display_ms_avg`
- `end_to_end_ms_min`
- `end_to_end_ms_max`
- `end_to_end_ms_avg`

## Raspberry Pi 4 Performance Notes

The logging path is tuned for Pi 4 and SD-card style storage:

- a single persistent SQLite connection is opened once and reused
- prepared statements are created once and reused for inserts/updates
- `PRAGMA journal_mode = WAL` is enabled
- `PRAGMA synchronous = NORMAL` is used to reduce write overhead while keeping a safe embedded profile
- writes are grouped into short transactions
- event and sample inserts are buffered in memory before flush
- `flight_samples` stays low-rate at 1 Hz
- warnings/errors trigger an earlier flush request so important events are not left buffered for long

This design intentionally favors:

- low HUD-loop overhead
- lower flash wear than per-call commits
- preserving reliable event logging for important faults

It also accepts that the last few buffered non-critical records may be lost on a sudden crash.

## Flush Strategy

Two timers are used:

- a 1 second capture timer snapshots the latest `HudSample` into the pending sample queue
- a short flush timer writes pending events and samples in a single transaction

This separates sample capture frequency from SQLite write frequency.

## Timing Metrics

Timing is measured in the existing runtime path:

- `sensor_read_ms`: UART/frame handling time estimate
- `decode_ms`: CBOR decode timing
- `display_ms`: widget paint/update timing
- `end_to_end_ms`: combined ingest + decode + display estimate
- `sensor_rate_hz`: effective UART/sample rate
- `display_rate_hz`: effective paint rate

If a metric is not available, the logger writes `NULL` rather than forcing a fake zero.

## Example Queries

Latest session summary:

```sql
SELECT id, start_time, end_time, git_branch, git_commit, exit_status
FROM sessions
ORDER BY id DESC
LIMIT 5;
```

Recent errors:

```sql
SELECT timestamp, component, event_type, message, details
FROM event_logs
WHERE level = 'ERROR'
ORDER BY id DESC
LIMIT 20;
```

Last 60 seconds of flight data:

```sql
SELECT timestamp, altitudeFt, vspeedFpm, headingDeg, rollDeg, pitchDeg
FROM flight_samples
WHERE session_id = ?
ORDER BY id DESC
LIMIT 60;
```

Display timing threshold checks:

```sql
SELECT timestamp, display_ms, display_rate_hz, end_to_end_ms
FROM flight_samples
WHERE display_ms IS NOT NULL
ORDER BY id DESC
LIMIT 100;
```

Rows with stale data:

```sql
SELECT timestamp, stale_data_flag, data_valid, data_fresh
FROM flight_samples
WHERE stale_data_flag = 1
ORDER BY id DESC;
```
