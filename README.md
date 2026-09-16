# AP V4.0 Legacy Version

AP V4.0 firmware migrated to a PlatformIO `src/` and `include/` layout.

This branch keeps the V4.0 wireless packet layout and PLC/W17 behavior while using the RMT-based peer check LED driver from the V4.1 refactor.

## Build

```powershell
& 'C:\Users\d\.platformio\penv\Scripts\pio.exe' run -e ap_v4_0_20260528
```

## Notes

- Active PlatformIO sources are under `src/` and `include/`.
- `legacy_arduino/` keeps the previous Arduino Workshop layout for reference.
- Peer check LED uses the RMT backend, not `Adafruit_NeoPixel`.
- Packet expansion and W17 RSSI/FW-version reporting are intentionally not applied.

## Documentation

- [V3.31 양산 버전 대비 V4.0 P0List 개발 변경 이력](docs/AP_V3.31_to_V4.0_개발_히스토리.md)
