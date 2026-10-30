# Ground Seismic and Acoustic Perimeter Mesh

Private research repo. A solar-powered 915 MHz LoRa mesh of ESP32 sensor nodes with ADXL355 accelerometers and SM-24 geophones, classifying vehicles, footsteps, and wind at the edge.

Full write-up: [`docs/project-page.md`](docs/project-page.md)

## What's here

| Path | Language | Purpose |
|---|---|---|
| `firmware/lib/seismic/fft.h` | C++17 | Radix-2 FFT (1024 points) |
| `firmware/lib/seismic/features.h` | C++17 | 1 kHz × 1024 window → RMS, 3 band fractions (2–20, 20–60, 60–200 Hz), kurtosis, **footstep cadence** (envelope autocorrelation over about 4 s, 1.4–2.6 steps/s) |
| `firmware/lib/seismic/classifier.h` | C++17 | Explainable rules: every decision carries the rule that fired. Vehicles need sustained low-band energy across windows |
| `firmware/lib/seismic/sha256.h` | C++17 | SHA-256 and HMAC-SHA256 (RFC 4231 verified) |
| `firmware/lib/seismic/mesh.h` | C++17 | 29-byte packet, truncated HMAC tag (relays can decrement hops without breaking it), flooding with (src, seq) dedup, store-and-forward ring for gateway outages (T6) |
| `firmware/src/main.cpp` | Arduino | ADXL355 SPI at 1 kHz on a hardware timer, SX1262 radio (RadioLib) |
| `gateway/decode.py` | Python | Decodes and verifies packets, dedupes floods |
| `gateway/tune.py` | Python | Grid-searches thresholds on labelled feature logs for macro-F1, ready to paste into firmware |

## Quickstart

```bash
make test     # C++ host tests (synthetic vehicle, footsteps, wind, quiet) + Python, including a C++→Python signed-packet cross-check
cp firmware/include_node_secrets.example.h firmware/src/node_secrets.h   # set NODE_ID + key
make flash
```

## Next steps
- [ ] Measure the quiet-night RMS per node and set `noise_gate_rms` to about 3× that
- [ ] Log features (not decisions) for labelled walks and drive-bys, then run `gateway/tune.py`
- [ ] Geophone channel: SM-24 → ADS1115/ADS1256 differential ADC, fused with the MEMS channel for long-range vehicles
- [ ] Second-pass model: a tiny MLP on the same feature vector (TFLite Micro), keeping the rules as a fallback
- [ ] Multi-node localisation: arrival-time differences between neighbouring nodes
- [ ] Deep sleep between windows when RMS stays below the gate for N minutes (solar budget)

LoRa at 915 MHz must stay within ISED RSS-247 limits.
