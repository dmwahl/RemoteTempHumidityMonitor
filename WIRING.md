# Wiring Diagram: Particle Boron + DHT22

## Components Required

- Particle Boron
- DHT22 Temperature/Humidity Sensor (AM2302)
- Jumper wires (3 wires)
- **10kΩ resistor (REQUIRED)** - External pull-up resistor

## DHT22 Pinout

```
DHT22 Sensor (looking at front with vents):
  ┌─────────────┐
  │  ┌───────┐  │
  │  │ VENTS │  │
  │  └───────┘  │
  └─────────────┘
   │  │  │  │
   1  2  3  4

Pin 1: VCC (Power)
Pin 2: DATA (Signal)
Pin 3: NC (Not Connected)
Pin 4: GND (Ground)
```

## Connections

| DHT22 Pin | Function | Particle Boron Pin | Notes |
|-----------|----------|-------------------|-------|
| Pin 1     | VCC      | 3V3               | Power supply (3.3V) |
| Pin 2     | DATA     | D2                | Data signal (internal pull-up enabled in code) |
| Pin 3     | NC       | -                 | Not connected |
| Pin 4     | GND      | GND               | Ground |

## Wiring Diagram

```
DHT22                          Particle Boron
┌─────┐                        ┌──────────┐
│     │                        │          │
│  1  ├────────────────────────┤ 3V3      │
│     │      (Red Wire)        │          │
│  2  ├────────────────────────┤ D2       │
│     │      (Yellow Wire)     │          │
│  3  │ (Not Connected)        │          │
│     │                        │          │
│  4  ├────────────────────────┤ GND      │
│     │      (Black Wire)      │          │
└─────┘                        └──────────┘
```

## Important Notes

1. **External Pull-Up Resistor REQUIRED**: A 10kΩ resistor between DATA (D3) and 3V3 is **required** for reliable operation. The internal pull-up resistor is disabled in firmware for better signal quality.

2. **Power**: The DHT22 operates on 3.3V-5.5V. Using the Boron's 3V3 pin is recommended for compatibility.

3. **Pin Selection**: D3 is used in the code. You can change this to any digital GPIO pin (D2, D4, D5, etc.) by modifying the `DHTPIN` definition in the code.

4. **Wire Length**: Keep wires as short as practical (<1 meter preferred). For longer runs, consider:
   - Adding a 100nF capacitor between VCC and GND near the sensor
   - Using shielded cable for the DATA line

5. **Sensor Orientation**: The DHT22 sensor should be mounted with the vented side facing the environment being measured.

## Verification

After wiring:
1. Connect Boron to USB
2. Upload the firmware
3. Open Serial Monitor at 9600 baud
4. You should see measurement readings every 5 minutes
5. Check Particle Console for published events under "sensor/reading"
