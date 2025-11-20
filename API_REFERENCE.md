# Remote Temperature & Humidity Monitor - API Reference

## Overview
This document provides a complete reference for all cloud functions, cloud variables, events, and configuration parameters available in the Remote Temperature & Humidity Monitor firmware.

## Table of Contents
- [Cloud Functions](#cloud-functions)
- [Cloud Variables](#cloud-variables)
- [Cloud Events](#cloud-events)
- [Configuration Parameters](#configuration-parameters)
- [EEPROM Memory Layout](#eeprom-memory-layout)
- [DOE (Design of Experiments)](#doe-design-of-experiments)

---

## Cloud Functions

Cloud functions can be called remotely via the Particle Cloud API or Console to configure and control the device.

### 1. `setInterval`
**Purpose**: Configure the data publish interval

**Parameter**: `intervalSeconds` (integer, 30-3600)
- Sets how often the device publishes sensor data to the cloud
- Minimum: 30 seconds
- Maximum: 3600 seconds (1 hour)
- Default: 300 seconds (5 minutes)

**Return Value**:
- Success: Returns the new interval in seconds
- Failure: Returns -1 if value is out of range

**Side Effects**:
- Updates moving average buffer size automatically
- Saves new interval to EEPROM for persistence
- Publishes confirmation event to `config/interval`

**Example**:
```
particle call <device-name> setInterval 600
# Sets publish interval to 10 minutes
```

---

### 2. `forceReading`
**Purpose**: Force an immediate sensor reading and evaluation

**Parameter**: Any string (ignored)

**Return Value**: Always returns 1

**Side Effects**:
- Triggers immediate DHT22 sensor reading
- Updates moving average buffer
- May publish data if conditions are met (temperature change or interval elapsed)
- Does not reset publish interval timer

**Example**:
```
particle call <device-name> forceReading ""
```

---

### 3. `enableShort`
**Purpose**: Enable or disable short-format message publishing

**Parameter**:
- `"1"`, `"true"`, or empty: Enable short messages
- `"0"` or `"false"`: Disable short messages

**Return Value**:
- 1 if enabled
- 0 if disabled

**Behavior**:
- When enabled, publishes both JSON (to `sensor/reading`) and short format (to `sensor/short`)
- Short format: `"23.5C 45.6%"` (human-readable, < 20 chars)
- Auto-disables after 1 hour if enabled
- Resets 1-hour timer when re-enabled

**Side Effects**:
- Publishes confirmation event to `config/shortmsg`

**Example**:
```
particle call <device-name> enableShort 1
# Enables short messages for 1 hour
```

---

### 4. `startDOE`
**Purpose**: Start Design of Experiments timing optimization

**Parameter**: Any string (ignored)

**Return Value**:
- Success: Returns 1
- Failure: Returns -1 if DOE already running

**Behavior**:
- Runs a comprehensive 4-phase experiment to find optimal DHT22 timing parameters
- Tests each parameter independently (one-factor-at-a-time design)
- Performs 30 reads per configuration for statistical confidence
- Publishes progress and results via `doe/*` events
- Automatically applies and saves optimal parameters to EEPROM when complete

**Duration**:
- Phase 1 (Start Signal): ~13 tests × 60 seconds = ~13 minutes
- Phase 2 (Response Timeout): ~16 tests × 60 seconds = ~16 minutes
- Phase 3 (Bit Timeout): ~15 tests × 60 seconds = ~15 minutes
- Phase 4 (Bit Threshold): ~11 tests × 60 seconds = ~11 minutes
- **Total: ~55 minutes**

**Warning**: Normal sensor readings are suspended during DOE execution

**Example**:
```
particle call <device-name> startDOE ""
```

---

### 5. `stopDOE`
**Purpose**: Stop a running DOE experiment

**Parameter**: Any string (ignored)

**Return Value**:
- Success: Returns 1
- Failure: Returns -1 if DOE not running

**Side Effects**:
- Restores default timing parameters
- Sets DOE status to "stopped"
- Publishes stop event to `doe/status`

**Example**:
```
particle call <device-name> stopDOE ""
```

---

### 6. `setStartSig`
**Purpose**: Set DHT22 start signal timing parameter

**Parameter**: `microseconds` (integer, 800-2000)

**Return Value**:
- Success: Returns the new value in microseconds
- Failure: Returns -1 if value is out of range

**Valid Range**: 800-2000 μs
**Default**: 1600 μs (optimized via DOE)

**Side Effects**:
- Applies immediately to DHT22 communication
- Saves to EEPROM for persistence
- Publishes confirmation event to `config/timing`

**Example**:
```
particle call <device-name> setStartSig 1600
```

---

### 7. `setRespTO`
**Purpose**: Set DHT22 response timeout parameter

**Parameter**: `microseconds` (integer, 150-300)

**Return Value**:
- Success: Returns the new value in microseconds
- Failure: Returns -1 if value is out of range

**Valid Range**: 150-300 μs
**Default**: 240 μs (optimized via DOE)

**Side Effects**:
- Applies immediately to DHT22 communication
- Saves to EEPROM for persistence
- Publishes confirmation event to `config/timing`

**Example**:
```
particle call <device-name> setRespTO 240
```

---

### 8. `setBitTO`
**Purpose**: Set DHT22 bit timeout parameter

**Parameter**: `microseconds` (integer, 80-150)

**Return Value**:
- Success: Returns the new value in microseconds
- Failure: Returns -1 if value is out of range

**Valid Range**: 80-150 μs
**Default**: 115 μs (optimized via DOE)

**Side Effects**:
- Applies immediately to DHT22 communication
- Saves to EEPROM for persistence
- Publishes confirmation event to `config/timing`

**Example**:
```
particle call <device-name> setBitTO 115
```

---

### 9. `setBitThr`
**Purpose**: Set DHT22 bit decision threshold parameter

**Parameter**: `microseconds` (integer, 40-60)

**Return Value**:
- Success: Returns the new value in microseconds
- Failure: Returns -1 if value is out of range

**Valid Range**: 40-60 μs
**Default**: 46 μs (optimized via DOE)

**Technical Details**:
- DHT22 sends bit 0 as ~26-28μs high pulse
- DHT22 sends bit 1 as ~70μs high pulse
- Threshold determines decision boundary

**Side Effects**:
- Applies immediately to DHT22 communication
- Saves to EEPROM for persistence
- Publishes confirmation event to `config/timing`

**Example**:
```
particle call <device-name> setBitThr 46
```

---

### 10. `uptime`
**Purpose**: Publish device uptime and system information

**Parameter**: Any string (ignored)

**Return Value**: Always returns 1

**Output Format** (published to `system/uptime` event):
```
"10:15:23 up 5 days, 3:42, cloud: connected, free mem: 45632 bytes"
```

**Information Provided**:
- Current time (HH:MM:SS)
- Uptime in days, hours, minutes
- Cloud connection status
- Free memory in bytes

**Example**:
```
particle call <device-name> uptime ""
```

---

## Cloud Variables

Cloud variables can be read remotely via the Particle Cloud API or Console. All variables are read-only.

### 1. `lastReading`
**Type**: String (JSON)

**Format**:
```json
{
  "measurement": "environment",
  "tags": {
    "location": "default",
    "device": "<device-id>"
  },
  "fields": {
    "temperature": 23.5,
    "humidity": 45.2
  },
  "timestamp": 1234567890
}
```

**Description**: Last published sensor reading in InfluxDB-compatible JSON format

**Update Frequency**: Updates only when data is published (not every measurement)

---

### 2. `publishSec`
**Type**: Integer

**Description**: Current publish interval in seconds

**Range**: 30-3600 seconds

**Default**: 300 seconds (5 minutes)

---

### 3. `shortMsg`
**Type**: Boolean

**Description**: Short message publishing status

**Values**:
- `true`: Short messages are enabled
- `false`: Short messages are disabled

**Note**: Auto-disables after 1 hour when enabled

---

### 4. `temperature`
**Type**: Double

**Description**: Current temperature moving average in Celsius

**Update Frequency**: Updates every 10 seconds (measurement interval)

**Precision**: 2 decimal places

---

### 5. `humidity`
**Type**: Double

**Description**: Current humidity moving average as percentage

**Range**: 0-100%

**Update Frequency**: Updates every 10 seconds (measurement interval)

**Precision**: 2 decimal places

---

### 6. `readingAge`
**Type**: Integer

**Description**: Time elapsed since last publish in seconds

**Use Case**: Monitor how long since data was last sent to cloud

---

### 7. `bufferFill`
**Type**: Integer

**Description**: Moving average buffer fill percentage

**Range**: 0-100%

**Calculation**: `(current readings / buffer size) × 100`

**Use Case**: Monitor if buffer has enough data for stable average

---

### 8. `resetReason`
**Type**: String

**Description**: Reason for the last device reset

**Possible Values**:
- `"none"` - No reset has occurred
- `"unknown"` - Reset reason unknown
- `"pin_reset"` - Reset via reset pin
- `"power_management"` - Power management reset
- `"power_down"` - Power down reset
- `"brownout"` - Brownout detected
- `"watchdog"` - Watchdog timer reset
- `"update"` - Firmware update
- `"update_error"` - Firmware update error
- `"update_timeout"` - Firmware update timeout
- `"factory_reset"` - Factory reset
- `"safe_mode"` - Safe mode entry
- `"dfu_mode"` - DFU mode entry
- `"panic"` - System panic/crash
- `"user"` - User-initiated reset
- `"unknown_code_<N>"` - Unknown code N

**Use Case**: Diagnose unexpected resets or crashes

---

### 9. `doeStatus`
**Type**: String

**Description**: Current DOE experiment status

**Possible Values**:
- `"idle"` - No experiment running
- `"starting"` - Experiment initializing
- `"running"` - Experiment in progress
- `"testing_start_signal"` - Phase 1 running
- `"testing_response_timeout"` - Phase 2 running
- `"testing_bit_timeout"` - Phase 3 running
- `"testing_bit_threshold"` - Phase 4 running
- `"complete"` - Experiment finished
- `"stopped"` - Experiment stopped by user

---

### 10. `doeProgress`
**Type**: Integer

**Description**: DOE experiment progress percentage

**Range**: 0-100%

**Update Frequency**: Updates after each parameter configuration test

---

### 11. `doePhase1`
**Type**: String (JSON)

**Description**: Start signal parameter optimization phase summary

**Format**:
```json
{
  "param": "start_signal",
  "count": 13,
  "avg_fail": 5.23,
  "best_fail": 0.00,
  "worst_fail": 15.67,
  "std_dev": 4.12,
  "cv": 78.77,
  "z_score": 3.45,
  "p_value": 0.0003,
  "best_value": 1600
}
```

**Field Descriptions**:
- `param`: Parameter name
- `count`: Number of configurations tested
- `avg_fail`: Average failure percentage
- `best_fail`: Best (minimum) failure percentage
- `worst_fail`: Worst (maximum) failure percentage
- `std_dev`: Standard deviation of failure rates
- `cv`: Coefficient of variation (relative variability %)
- `z_score`: Statistical significance score
- `p_value`: Probability value (< 0.05 = statistically significant)
- `best_value`: Optimal parameter value in microseconds

---

### 12. `doePhase2`
**Type**: String (JSON)

**Description**: Response timeout parameter optimization phase summary

**Format**: Same as `doePhase1` with `param: "response_timeout"`

---

### 13. `doePhase3`
**Type**: String (JSON)

**Description**: Bit timeout parameter optimization phase summary

**Format**: Same as `doePhase1` with `param: "bit_timeout"`

---

### 14. `doePhase4`
**Type**: String (JSON)

**Description**: Bit threshold parameter optimization phase summary

**Format**: Same as `doePhase1` with `param: "bit_threshold"`

---

## Cloud Events

Events are published by the device to report status, data, and experimental results.

### Sensor Data Events

#### `sensor/reading`
**Frequency**: Variable (based on publish interval and temperature change)

**Format**: JSON (InfluxDB-compatible)
```json
{
  "measurement": "environment",
  "tags": {
    "location": "default",
    "device": "<device-id>"
  },
  "fields": {
    "temperature": 23.5,
    "humidity": 45.2
  },
  "timestamp": 1234567890
}
```

**Publish Conditions**:
- First reading after boot
- Temperature changed by ≥ 0.25°C
- 5× publish interval elapsed (forced publish)

---

#### `sensor/short`
**Frequency**: Same as `sensor/reading` (when short messages enabled)

**Format**: Plain text
```
23.5C 45.6%
```

**Duration**: Only published when short messages are enabled (auto-disables after 1 hour)

---

#### `sensor/error`
**Frequency**: On sensor read failure

**Format**: Plain text
```
DHT22 read failed
```

---

### Configuration Events

#### `config/interval`
**Trigger**: After `setInterval` function call

**Format**: Integer string
```
300
```

---

#### `config/shortmsg`
**Trigger**: After `enableShort` function call

**Format**: Plain text
```
enabled
```
or
```
disabled
```

---

#### `config/timing`
**Trigger**: After any timing parameter function call

**Format**: Plain text
```
start_signal=1600
```

---

### System Events

#### `system/uptime`
**Trigger**: After `uptime` function call

**Format**: Plain text (Linux uptime-style)
```
10:15:23 up 5 days, 3:42, cloud: connected, free mem: 45632 bytes
```

---

#### `sensor/info`
**Trigger**: Various informational events

**Format**: Plain text
```
Short messages disabled
```

---

### DOE Events

#### `doe/status`
**Frequency**: At DOE start, stop, and phase transitions

**Format**: JSON
```json
{
  "status": "Phase 1/4: Testing start signal timing",
  "progress": 25,
  "elapsed": 789
}
```

**Fields**:
- `status`: Human-readable status message
- `progress`: Progress percentage (0-100)
- `elapsed`: Seconds since DOE started

---

#### `doe/result`
**Frequency**: After each configuration test during DOE

**Format**: JSON
```json
{
  "ss": 1600,
  "rt": 240,
  "bt": 115,
  "bth": 46,
  "success": 28,
  "fail": 2,
  "rate": 93.3,
  "best": true
}
```

**Fields**:
- `ss`: Start signal (μs)
- `rt`: Response timeout (μs)
- `bt`: Bit timeout (μs)
- `bth`: Bit threshold (μs)
- `success`: Successful reads
- `fail`: Failed reads
- `rate`: Success rate percentage
- `best`: True if this is the best result so far

---

#### `doe/phase_summary`
**Frequency**: After each DOE phase completes

**Format**: JSON (same as doePhase1-4 cloud variables)

---

#### `doe/phase_data`
**Frequency**: After each DOE phase completes (may be multiple chunks)

**Format**: JSON with CSV data
```json
{
  "param": "start_signal",
  "chunk": 1,
  "total": 1,
  "data": "800,25,5,83.3,16.7\n900,27,3,90.0,10.0\n..."
}
```

**CSV Data Format**: `value,success,fail,success_rate,fail_rate`

**Use Case**: Export to spreadsheet for detailed analysis

---

## Configuration Parameters

### Measurement Timing
- **Measurement Interval**: 10 seconds (fixed)
  - DHT22 is read every 10 seconds
  - Cannot be changed (DHT22 requires 2s minimum between reads)

- **Publish Interval**: 30-3600 seconds (configurable via `setInterval`)
  - Default: 300 seconds (5 minutes)
  - Determines moving average window size

### Moving Average Buffer
- **Buffer Size**: Automatically calculated
  - Formula: `publishInterval / 10`
  - Minimum: 3 readings
  - Maximum: 360 readings (1 hour at 10s intervals)
  - Provides smoothing of noisy sensor data

### Sensor Validation
- **Temperature Jump Threshold**: 1.0°C
  - If temperature changes more than 1°C between readings, triggers retry
  - Helps filter spurious readings

- **Valid Ranges** (DHT22 specifications):
  - Temperature: -40°C to +80°C
  - Humidity: 0% to 100%

### DHT22 Timing Parameters
All timing values in microseconds (μs). Optimized via DOE experiments.

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| Start Signal | 1600 | 800-2000 | Initial low pulse to wake sensor |
| Response Timeout | 240 | 150-300 | Max wait time for sensor response |
| Bit Timeout | 115 | 80-150 | Max wait time for bit signals |
| Bit Threshold | 46 | 40-60 | Decision boundary for 0/1 bits |

---

## EEPROM Memory Layout

The device uses EEPROM to persist configuration across power cycles and resets.

| Address | Size | Parameter | Type |
|---------|------|-----------|------|
| 0 | 4 bytes | Publish Interval | uint32_t |
| 4 | 4 bytes | Magic Number | uint32_t (0xA5B4C3D2) |
| 8 | 2 bytes | Start Signal | uint16_t |
| 10 | 2 bytes | Response Timeout | uint16_t |
| 12 | 2 bytes | Bit Timeout | uint16_t |
| 14 | 2 bytes | Bit Threshold | uint16_t |

**Total EEPROM Usage**: 16 bytes

**Magic Number**: Used to validate EEPROM data integrity
- If magic number matches 0xA5B4C3D2, data is valid
- If magic number doesn't match, defaults are used and EEPROM is initialized

---

## DOE (Design of Experiments)

### Purpose
Find optimal DHT22 communication timing parameters to minimize read failures.

### Methodology
**One-Factor-at-a-Time (OFAT) Design**
- Tests each parameter independently while holding others constant
- More manageable than full factorial design
- Provides clear insights into each parameter's effect

### Phases

#### Phase 1: Start Signal Optimization
- **Range**: 800-2000 μs (step: 100 μs)
- **Tests**: 13 configurations × 30 reads = 390 total reads
- **Duration**: ~13 minutes
- **Baseline**: RT=240, BT=115, BTh=46

#### Phase 2: Response Timeout Optimization
- **Range**: 150-300 μs (step: 10 μs)
- **Tests**: 16 configurations × 30 reads = 480 total reads
- **Duration**: ~16 minutes
- **Baseline**: Best SS from Phase 1, BT=115, BTh=46

#### Phase 3: Bit Timeout Optimization
- **Range**: 80-150 μs (step: 5 μs)
- **Tests**: 15 configurations × 30 reads = 450 total reads
- **Duration**: ~15 minutes
- **Baseline**: Best SS, Best RT, BTh=46

#### Phase 4: Bit Threshold Optimization
- **Range**: 40-60 μs (step: 2 μs)
- **Tests**: 11 configurations × 30 reads = 330 total reads
- **Duration**: ~11 minutes
- **Baseline**: Best SS, Best RT, Best BT

### Statistical Metrics

Each phase calculates comprehensive statistics to determine optimal values:

#### Failure Rate Metrics
- **Average Failure %**: Mean failure rate across all configurations
- **Best Failure %**: Minimum failure rate (optimal value)
- **Worst Failure %**: Maximum failure rate

#### Variability Metrics
- **Standard Deviation**: Absolute variability in failure rates
- **Coefficient of Variation (CV)**: Relative variability (stdDev/mean × 100%)
  - Lower CV indicates more consistent results

#### Statistical Significance
- **Z-Score**: How many standard errors the best result differs from mean
  - Higher Z-score indicates stronger significance
- **P-Value**: Probability that results occurred by chance
  - P < 0.05 indicates statistically significant result
  - P < 0.01 indicates highly significant result

### Results Storage

#### Real-time Publishing
- Progress updates via `doe/status` events
- Individual test results via `doe/result` events
- Phase summaries via `doe/phase_summary` events
- Detailed CSV data via `doe/phase_data` events

#### Cloud Variable Storage
- Complete phase summaries stored in `doePhase1-4` variables
- Accessible anytime without real-time monitoring
- Persists until next DOE run

#### EEPROM Storage
- Optimal parameters automatically saved to EEPROM
- Loaded automatically on next boot
- Can be overridden with `setStartSig`, `setRespTO`, `setBitTO`, `setBitThr`

### Best Practices

1. **When to Run DOE**:
   - After hardware changes (new sensor, different wiring)
   - When experiencing persistent read failures
   - In new environmental conditions (temperature extremes)
   - After Particle firmware updates

2. **During DOE**:
   - Normal sensor readings are suspended
   - Device remains cloud-connected
   - Can be stopped anytime with `stopDOE`
   - Monitor progress via `doeStatus` and `doeProgress` variables

3. **After DOE**:
   - Review phase summaries in `doePhase1-4` variables
   - Check p-values for statistical significance
   - Optimal parameters are automatically applied and saved
   - Normal operation resumes immediately

4. **Interpreting Results**:
   - Look for p-value < 0.05 (statistically significant)
   - Lower CV indicates more reliable parameter
   - Export CSV data for graphical analysis in spreadsheet

---

## Version History

- **v1.3.0**: Added DOE functionality, timing parameter configuration, uptime function, reset reason variable
- **v1.2.0**: Added EEPROM persistence, configurable timing parameters
- **v1.1.0**: Added moving average, short messages, cloud functions
- **v1.0.0**: Initial release with basic DHT22 reading and cloud publishing

---

## Hardware Requirements

- **Particle Boron** (nRF52840)
- **DHT22 Temperature/Humidity Sensor**
- **10kΩ Pull-up Resistor** (DATA line to 3.3V)

### Wiring
```
DHT22 Pin 1 (VCC)  -> Boron 3V3
DHT22 Pin 2 (DATA) -> Boron D3 + 10kΩ resistor to 3V3
DHT22 Pin 4 (GND)  -> Boron GND
```

**Important**: External 10kΩ pull-up resistor is required. Internal pull-up is disabled for precise timing.

---

## Example Usage Scenarios

### Scenario 1: Change Publish Interval
```bash
# Set publish interval to 10 minutes (600 seconds)
particle call mydevice setInterval 600

# Verify new setting
particle get mydevice publishSec
# Returns: 600
```

### Scenario 2: Monitor Current Conditions
```bash
# Get current temperature (moving average)
particle get mydevice temperature
# Returns: 23.45

# Get current humidity (moving average)
particle get mydevice humidity
# Returns: 45.67

# Check how long since last publish
particle get mydevice readingAge
# Returns: 245 (4 minutes ago)
```

### Scenario 3: Run DOE to Optimize Timing
```bash
# Start DOE experiment
particle call mydevice startDOE ""

# Monitor progress (repeat every few minutes)
particle get mydevice doeProgress
# Returns: 35 (35% complete)

particle get mydevice doeStatus
# Returns: testing_bit_timeout

# After completion, review results
particle get mydevice doePhase1
# Returns: {"param":"start_signal","count":13,"avg_fail":5.23,...}

particle get mydevice doePhase2
# Returns: {"param":"response_timeout","count":16,"avg_fail":2.15,...}

# Optimal parameters are automatically applied and saved
```

### Scenario 4: Manually Tune Timing Parameters
```bash
# After reviewing DOE results, manually set parameters
particle call mydevice setStartSig 1600
particle call mydevice setRespTO 240
particle call mydevice setBitTO 115
particle call mydevice setBitThr 46

# Parameters are saved to EEPROM automatically
```

### Scenario 5: Diagnose Reset Issues
```bash
# Check why device last reset
particle get mydevice resetReason
# Returns: "watchdog" (indicates firmware hang/crash)

# or "power_down" (indicates power loss)
# or "update" (indicates firmware update)
# or "user" (indicates manual reset)
```

### Scenario 6: Get System Information
```bash
# Publish uptime and system info
particle call mydevice uptime ""

# Subscribe to uptime event to see output
particle subscribe system/uptime
# Event: "14:23:56 up 12 days, 5:42, cloud: connected, free mem: 48392 bytes"
```

---

## API Rate Limits

Particle Cloud has rate limits for API calls:
- **Function calls**: 1 per second per function
- **Variable reads**: Unlimited
- **Event publishing**: 1 per second per event name (burst of 4 allowed)

The firmware respects these limits and includes delays in DOE experiments.

---

## Troubleshooting

### High Failure Rates
1. Check wiring: Ensure 10kΩ pull-up resistor is properly connected
2. Run DOE experiment to find optimal timing parameters
3. Check power supply: Ensure stable 3.3V supply
4. Verify sensor quality: Cheap DHT22 clones may have timing variations

### No Data Publishing
1. Check `readingAge` variable - should increment
2. Force reading: `particle call mydevice forceReading ""`
3. Subscribe to events: `particle subscribe sensor/`
4. Check cloud connection: Look for "cloud: connected" in uptime

### Unexpected Resets
1. Check `resetReason` variable
2. "watchdog" = firmware hang (may need debug)
3. "panic" = crash (check logs)
4. "brownout" = power supply issue

### DOE Not Completing
1. Check `doeProgress` and `doeStatus` variables
2. DOE takes ~55 minutes total
3. Can stop manually: `particle call mydevice stopDOE ""`
4. Results saved after each phase completes

---

## Support & Feedback

For issues, questions, or feature requests, please refer to the project repository or contact the maintainer.
