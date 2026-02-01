# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Setup development environment (first time only)
python scripts/setup_dev.py

# Source environment variables (required before every build session)
source scripts/get_dev.sh          # Linux
ADF\export.bat                     # Windows

# Build
idf.py build

# Flash to device
idf.py flash -p /dev/ttyUSB0       # Linux (adjust port as needed)
idf.py flash -p COM3               # Windows

# Build and flash together
idf.py build flash -p /dev/ttyUSB0

# Monitor serial output
idf.py monitor -p /dev/ttyUSB0

# Clean build
idf.py fullclean
```

## Architecture Overview

ESP32-S3 audio recording/playback device using ESP-IDF 5.3.0 + ESP-ADF framework.

### Core Components (main/)

| Component | Purpose |
|-----------|---------|
| `firmware.c` | Entry point (`app_main`), initialization sequence, main loop |
| `Audio/` | AAC recording/playback pipelines using ESP-ADF |
| `Storage/` | SPI NAND Flash via espressif/spi_nand_flash, mounted at `/Storage` |
| `Power/` | Battery voltage ADC, charge detection, power sensing |
| `Buttons/` | GPIO interrupt handlers with double-press/long-press detection |
| `Indicator/` | LED PWM control via LEDC |

### Custom Board Definition (components/my_board/)

Board-specific hardware abstraction for ESP-ADF audio board interface.

### Initialization Sequence (app_main)

1. NVS Flash → 2. Storage mount → 3. Power ADC → 4. Button task → 5. LED → 6. Button callbacks → 7. Audio init → 8. Main loop

### Concurrency Model

- **Button Scanner Task**: Priority 20, Core 1 - processes button gestures
- **Audio Recording/Playback Tasks**: Priority 15, created on-demand
- **Semaphores**: Protect ADC access and audio pipeline (prevent concurrent record/play)

### Audio Pipeline Architecture

Recording: `I2S PDM mic → AAC Encoder (64kbps, 44.1kHz) → FATFS Writer`
Playback: `FATFS Reader → AAC Decoder → I2S Speaker`

### Key GPIO Assignments

- Buttons: GPIO_18 (play/pause), GPIO_10 (record)
- Power sensing: GPIO_11 (power), GPIO_12 (charge), GPIO_8 (battery ADC)
- Audio: GPIO_34 (speaker enable)
- LED: GPIO_21
- SPI NAND: GPIO 37-42 (CS, MISO, MOSI, CLK, WP, HD)

## Dependencies

Managed via `main/idf_component.yml` and `dependencies.lock`. Key external component:
- `espressif/spi_nand_flash` ^0.11.0

## Framework Versions

Specific commits pinned in `scripts/setup_dev.py`:
- ESP-IDF: `6568f8c553f89c01c101da4d6c735379b8221858`
- ESP-ADF: `8a3b56a9b65af796164ebffc4e4bc45f144760b3`
