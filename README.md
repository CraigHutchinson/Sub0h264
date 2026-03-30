# Sub0h264

> **Patent Notice:** H.264/AVC may be covered by patents in some jurisdictions.
> The patent portfolio is administered by [Via Licensing Alliance](https://via-la.com/licensing-programs/avc-h-264/)
> (formerly MPEG LA). Most patents have expired; the last known US patents expire
> ~2027-2028. This project provides software under the MIT license which grants
> **copyright only, not patent rights**. Users are responsible for determining
> whether a patent license is required for their use case. See
> [Via LA](https://via-la.com/licensing-programs/avc-h-264/) or the
> [Wikimedia patent tracker](https://meta.wikimedia.org/wiki/Have_the_patents_for_H.264_MPEG-4_AVC_expired_yet%3F).

H.264 Baseline + High profile decoder optimised for ESP32-P4 RISC-V (PIE SIMD) and x86 desktop.

**Max resolution:** 640x480

## Build (Desktop)

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

## Build (ESP32-P4)

```bash
cd test_apps/hello_h264
cmake --preset esp32p4
cmake --build --preset esp32p4
```

## Project Structure

| Directory | Purpose |
|-----------|---------|
| `components/sub0h264/` | Decoder library (builds as ESP-IDF component or standalone static lib) |
| `test_apps/hello_h264/` | ESP-IDF hello-world test app |
| `tests/` | Desktop unit tests (doctest) |

## License

MIT &mdash; see [LICENSE.md](LICENSE.md)
