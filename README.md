# Sub0h264

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
