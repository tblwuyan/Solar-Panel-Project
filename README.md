# Pneumatic Solar-Tracking Panel

An Arduino Mega 2560 prototype that uses four light sensors to identify uneven illumination and pneumatically adjust a four-sided platform toward the brighter direction. It also measures chamber pressure and panel orientation and provides a serial interface for monitoring, calibration, and manual control.

> **Status:** Experimental hardware prototype. Read the safety notes and known limitations before connecting pumps, valves, or pressurised components.

## System overview

- **Light sensing:** four BH1750 sensors connected through a TCA9548A I²C multiplexer.
- **Pneumatic actuation:** four directions/chambers, each with an inlet valve, outlet valve, and analogue pressure sensor, plus two shared pump outputs.
- **Orientation sensing:** a JY901 IMU connected to the Mega's `Serial1` port for roll, pitch, and yaw feedback.

The controller compares each light reading with the four-sensor mean. A brighter sensor requests negative pressure from its two associated pneumatic drivers, changing the platform's orientation. When the readings become balanced, the corresponding targets are cleared.

This repository demonstrates an adaptive solar-alignment mechanism. It does not yet contain measurements proving improved energy yield over a fixed panel.

## Repository layout

```text
Solar-Panel-Project/
├── firmware/
│   ├── light_control_air_final_v1.2.ino  # Current integrated firmware
│   └── controll_avg.ino                  # Alternative/earlier integrated sketch
├── libraries/
│   ├── JY901SerialMega2560/              # JY901 Arduino library
│   ├── wit_c_sdk/                        # WIT sensor SDK and examples
│   ├── Wire/                             # Bundled legacy Wire copy
│   └── JY901.h
├── testfiles/                             # Component tests and development sketches
├── examples                              # Empty placeholder at present
└── readme.md
```

The two `.ino` files in `firmware/` both define Arduino entry points and must not be compiled as one sketch. Follow the setup procedure below and copy only the current integrated firmware into its own sketch directory.

## Hardware indicated by the firmware

| Component | Quantity | Purpose |
|---|---:|---|
| Arduino Mega 2560 | 1 | Main controller; required by pins 22–31 and `Serial1` |
| BH1750 light sensor | 4 | Illuminance measurement |
| TCA9548A I²C multiplexer | 1 | Connects four BH1750 devices with the same address |
| JY901-compatible IMU | 1 | Roll, pitch, and yaw measurement |
| Analogue pressure sensor | 4 | Chamber pressure feedback |
| Pneumatic chamber/actuator | 4 | Platform movement |
| Inlet and outlet valves | 4 each | Charge and exhaust/vacuum paths |
| Pump or controlled pressure source | 2 outputs | Shared inlet and outlet/vacuum control |
| MOSFET/relay drivers, flyback protection, and external supply | As required | Safe actuator switching and power |

The firmware assumes each pressure sensor maps **0.5–4.5 V to -100–300 kPa**. Confirm this against the exact sensor datasheet before operation.

## Pin assignment

### Pneumatic I/O

| Direction | Code | Pressure sensor | Inlet valve | Outlet valve |
|---|---|---:|---:|---:|
| Front | `F` | A0 | 24 | 28 |
| Right | `R` | A1 | 25 | 29 |
| Back | `B` | A2 | 26 | 30 |
| Left | `L` | A3 | 27 | 31 |

| Shared output | Mega pin |
|---|---:|
| Inlet/charge pump | 22 |
| Outlet/vacuum pump | 23 |

### Light sensors

Connect the TCA9548A upstream bus to Mega SDA pin 20 and SCL pin 21. Connect one BH1750 to each downstream channel from 0 to 3. The code expects TCA address `0x70`, BH1750 address `0x23`, and channels 0–3. Use supply voltage and pull-ups suitable for the exact breakout boards.

### JY901 serial connection

The firmware uses `Serial1` at 115200 baud:

| JY901 signal | Arduino Mega 2560 |
|---|---:|
| Sensor TX | RX1, pin 19 |
| Sensor RX | TX1, pin 18 |
| GND | GND |

Check the voltage level and pin labels for the exact JY901 variant before wiring it.

## Software setup

1. Download or clone the repository:

   ```bash
   git clone https://github.com/tblwuyan/Solar-Panel-Project.git
   ```

2. Create a sketch directory named `light_control_air_final_v1_2`.
3. Copy `firmware/light_control_air_final_v1.2.ino` into it and rename the file `light_control_air_final_v1_2.ino`. This prevents the two firmware files from compiling together and uses an Arduino-friendly sketch name.
4. Copy `libraries/JY901SerialMega2560` into the `libraries` directory in your Arduino sketchbook, then restart Arduino IDE.
5. Use the `Wire` library supplied with the Arduino AVR board core. Do not replace it with the bundled legacy copy unless required for a known compatibility reason.
6. Select **Arduino Mega or Mega 2560** and the correct port, then compile and upload the renamed sketch.
7. For the first validation, leave pumps and valves unpowered and confirm that Serial Monitor displays `System Ready`.
8. Set Serial Monitor to **115200 baud** with the **Newline** line ending.

## Automatic control logic

### Light-to-driver mapping

| Light channel | Driver indices | Directions |
|---:|---|---|
| 0 | 2 and 3 | Back and Left |
| 1 | 1 and 2 | Right and Back |
| 2 | 0 and 1 | Front and Right |
| 3 | 0 and 3 | Front and Left |

Every second, the firmware reads the four BH1750 sensors and calculates their mean:

- If an associated reading is more than **500 lux above the mean**, the driver target becomes **-80 kPa**.
- If both readings associated with a driver are within **±200 lux of the mean**, its target is cleared to `0`.
- Otherwise, the previous target is retained.
- Pressure regulation uses a **±0.5 kPa deadband** around a non-zero target.

Pressure is calculated as:

```text
voltage = ADC × 5.0 / 1023.0
pressure_kPa = (voltage - 0.5) × 100 - 100
```

This conversion is valid only for the assumed sensor range and a 5 V ADC reference.

## Serial command protocol

Send one command per line over USB serial at 115200 baud. Direction letters are `F`, `R`, `B`, and `L`.

| Command | Effect |
|---|---|
| `MAN,F,C` | Manually activate the Front inlet/charge path |
| `MAN,F,I` | Manually activate the Front outlet/exhaust path |
| `DEACT,F` | End manual control for Front |
| `SET,F,-40` | Request a -40 kPa Front target |
| `OFF` or `STOP` | Clear the current outputs and targets |
| `ZERO` | Average 50 IMU samples and use them as angle offsets |
| `VENT` | Open all inlet valves for three seconds with both pumps off |

For `MAN`, `C` or `c` selects the inlet path; any other mode character selects the outlet path.

### Serial output

The integrated sketch emits three lines per nominal control update:

```text
P:F=-12.3,R=-10.8,B=-9.7,L=-11.5
L:0=1240,1=1195,2=1210,3=1275
A:roll=1.2,pitch=-0.6,yaw=14.8
```

- `P:` pressure in kPa for Front, Right, Back, and Left
- `L:` light in lux for channels 0–3
- `A:` offset-adjusted roll, pitch, and yaw in degrees

Status messages include `System Ready`, `ZERO_START`, `ZERO_DONE`, `VENT_START`, and `VENT_DONE`.

## Test sketches

`testfiles/` contains experimental sketches for single and multiple light sensors, pressure acquisition and calibration, pump/valve control, combined light/pneumatic control, and earlier integrated versions. Their pins, baud rates, thresholds, and safety behavior do not all match the current firmware; inspect each sketch before using it.

## Known limitations

- **`firmware/` cannot compile as-is:** both files define `setup()`, `loop()`, and duplicate symbols. Isolate the selected sketch as described above.
- **The nominal 50 Hz loop is interrupted:** four sequential BH1750 reads block execution for about 720 ms once per second.
- **The low-pressure code does not match its comment:** when all chambers fall below -30 kPa, it sets every target to `0`; elsewhere, zero disables automatic actuation instead of commanding inflation. Do not treat this as a tested safety feature.
- **`STOP`/`OFF` is not latched:** it clears outputs immediately, but light-control logic can assign new targets later.
- **A serial `SET` target is not isolated:** automatic light logic may replace or clear it on a later cycle.
- **`VENT` is plumbing-dependent:** the code opens all inlet valves and turns both pump outputs off. Confirm that this safely vents the actual circuit.
- **JY901 setup needs verification:** the sketch sends a custom four-byte rate command. Check it against the manual for the exact IMU model and firmware.
- The repository does not yet include a wiring diagram, pneumatic schematic, bill of materials, hardware photos, or performance results.
- No software licence is declared. Public visibility alone does not grant reuse or redistribution rights.

## Safety

- Never power a pump or solenoid valve directly from an Arduino pin.
- Use correctly rated drivers, flyback diodes, fusing, wiring, and an external actuator supply.
- Verify common-ground and voltage-level requirements before applying power.
- Calibrate each pressure sensor against a known reference.
- Start with pumps disconnected, then test one valve and chamber at a time at the lowest practical pressure.
- Depressurise and disconnect power before changing pneumatic connections.
- Add independent pressure relief and a physical emergency stop before unattended or outdoor operation.
- Ensure the frame cannot pinch, collapse, or overturn through its full travel.

## Suggested next steps

1. Place every Arduino sketch in its own matching directory.
2. Add a labelled electrical wiring diagram and pneumatic schematic.
3. Add a bill of materials with exact part numbers and ratings.
4. Latch the emergency stop state and repair the low-pressure recovery logic.
5. Replace blocking light reads with a non-blocking state machine.
6. Log light, angle, pressure, and panel power to compare tracking with a fixed baseline.
7. Choose an open-source licence if reuse is intended.
