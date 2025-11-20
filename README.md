# Remote Temperature & Humidity Monitor

A cloud-connected temperature and humidity monitoring system for Particle Boron using a DHT22 sensor. Features hardware-timer precision, automatic retry logic, and InfluxDB-compatible JSON output for Grafana visualization.

[![Particle](https://img.shields.io/badge/Particle-Boron-blue)](https://www.particle.io/)
[![Device OS](https://img.shields.io/badge/Device%20OS-6.x-green)](https://docs.particle.io/reference/device-os/firmware/)
[![License](https://img.shields.io/badge/License-MIT-yellow)](LICENSE)

## Features

- ✅ **Precise Hardware Timing** - nRF52840 hardware timer (1µs resolution) for reliable DHT22 communication
- ✅ **Moving Average Filtering** - Measurements every 10 seconds with configurable publish interval (default 300s)
- ✅ **Smart Publishing** - Publishes when temperature changes ≥0.25°C OR 5× interval elapsed
- ✅ **Temperature Validation** - Automatic retry on >1°C jumps to filter sensor glitches
- ✅ **Automatic Retry Logic** - Automatically retries failed reads once before reporting error
- ✅ **DOE Timing Optimization** - Design of Experiments framework to find optimal 1-wire timing parameters
- ✅ **Cloud Connected** - Real-time data access via Particle Cloud
- ✅ **InfluxDB Compatible** - JSON output format ready for InfluxDB/Grafana
- ✅ **Remote Control** - Cloud functions for interval adjustment and forced readings
- ✅ **Low Overhead** - Efficient custom DHT22 library optimized for Gen3 devices

## Hardware Requirements

### Components
- **Particle Boron** (or other Gen3 device: Argon, Xenon)
- **DHT22** Temperature/Humidity Sensor (AM2302)
- **10kΩ Resistor** (pull-up resistor)
- **3 Jumper Wires**
- **USB Cable** for power/programming

### Wiring

**Important: External 10kΩ pull-up resistor REQUIRED**

```
DHT22          Resistor              Particle Boron
Pin 1 (VCC) ─────────────────────────── 3V3
Pin 2 (DATA) ──┬──────────────────────── D3
               │
               └─── 10kΩ resistor ───── 3V3
Pin 4 (GND) ─────────────────────────── GND
```

See [WIRING.md](WIRING.md) for detailed connection diagrams and troubleshooting.

## Installation

### 1. Hardware Setup
1. Connect DHT22 sensor following the wiring diagram above
2. Ensure the 10kΩ pull-up resistor is connected between DATA and 3V3

### 2. Flash Firmware

#### Option A: Particle Cloud Flash
```bash
# Install Particle CLI
npm install -g particle-cli

# Login to Particle
particle login

# Flash to device
particle flash <device-name> src/
```

#### Option B: Particle Workbench (VS Code)
1. Open this project folder in VS Code with Particle Workbench
2. Select your device: `Ctrl+Shift+P` → "Particle: Configure Project for Device"
3. Flash: `Ctrl+Shift+P` → "Particle: Cloud Flash"

#### Option C: Particle Web IDE
1. Create a new project in [Particle Web IDE](https://build.particle.io)
2. Copy files from `src/` directory
3. Flash to your device

### 3. Verify Operation
1. Open Serial Monitor at 9600 baud
2. Watch for initialization message and readings
3. Check [Particle Console](https://console.particle.io) for published events

## Usage

### Cloud Functions

The device exposes three cloud functions:

#### `setInterval` - Change Publish Interval

Change how often data is published to the cloud (30-3600 seconds). Note: Measurements are always taken every 10 seconds, but publishing is controlled by this interval along with the 0.25°C change threshold.

```bash
# Via Particle CLI
particle call <device-name> setInterval 300

# Via API
curl https://api.particle.io/v1/devices/<device-id>/setInterval \
  -d access_token=<token> \
  -d arg=300
```

**Parameters:**
- Range: 30 - 3600 seconds
- Default: 300 seconds (5 minutes)
- Returns: New interval value or -1 on error
- Note: Buffer size automatically adjusts to match new interval

#### `forceReading` - Trigger Immediate Reading

Force a measurement immediately without waiting for the interval.

```bash
# Via Particle CLI
particle call <device-name> forceReading ""

# Via API
curl https://api.particle.io/v1/devices/<device-id>/forceReading \
  -d access_token=<token>
```

#### `startDOE` - Start Design of Experiments

Start a comprehensive Design of Experiments (DOE) to find optimal 1-wire timing parameters. This systematically tests different timing combinations to maximize read success rate and minimize communication failures.

```bash
# Via Particle CLI
particle call <device-name> startDOE ""

# Via API
curl https://api.particle.io/v1/devices/<device-id>/startDOE \
  -d access_token=<token>
```

**What it does:**
- Tests 4 timing parameters independently (one-factor-at-a-time design):
  - **Start Signal**: 800-2000 µs (step 100) - 13 configurations
  - **Response Timeout**: 150-300 µs (step 10) - 16 configurations
  - **Bit Timeout**: 80-150 µs (step 5) - 15 configurations
  - **Bit Threshold**: 40-60 µs (step 2) - 11 configurations
- Performs 30 reads per configuration for statistical confidence
- Total: ~55 configurations × 30 reads × 2s = **~55 minutes**
- Publishes progress via `doe/status` and `doe/result` events
- Automatically applies optimal parameters when complete

**Monitoring progress:**
```bash
# Watch status updates
particle subscribe doe/status

# Watch individual test results
particle subscribe doe/result

# Check progress variables
particle get <device-name> doeStatus
particle get <device-name> doeProgress
```

**Event formats:**

*doe/status event:*
```json
{
  "status": "Phase 1/4: Testing start signal timing",
  "progress": 25,
  "elapsed": 420
}
```

*doe/result event:*
```json
{
  "ss": 1100,
  "rt": 200,
  "bt": 100,
  "bth": 50,
  "success": 28,
  "fail": 2,
  "rate": 93.3,
  "best": true
}
```

**Returns:**
- `1` - DOE started successfully
- `-1` - DOE already running

**Note:** Normal measurements are paused during DOE. The experiment can be stopped at any time using `stopDOE`.

#### `stopDOE` - Stop Design of Experiments

Stop a running DOE experiment and restore default timing parameters.

```bash
# Via Particle CLI
particle call <device-name> stopDOE ""

# Via API
curl https://api.particle.io/v1/devices/<device-id>/stopDOE \
  -d access_token=<token>
```

**Returns:**
- `1` - DOE stopped successfully
- `-1` - No DOE running

### Cloud Variables

Seven read-only variables for monitoring:

#### `temperature` - Latest Temperature Reading

```bash
particle get <device-name> temperature
```

Returns temperature in Celsius (e.g., `22.5`)

#### `humidity` - Latest Humidity Reading

```bash
particle get <device-name> humidity
```

Returns humidity percentage (e.g., `39.1`)

#### `readingAge` - Time Since Last Successful Reading

```bash
particle get <device-name> readingAge
```

Returns seconds since last successful reading (e.g., `45`)

#### `lastReading` - Most Recent Sensor Data (JSON)

```bash
particle get <device-name> lastReading
```

Returns JSON:
```json
{
  "measurement": "environment",
  "tags": {
    "location": "default",
    "device": "e00fce68xxxxxxxxxx"
  },
  "fields": {
    "temperature": 23.45,
    "humidity": 45.67
  },
  "timestamp": 1234567890
}
```

#### `intervalSec` - Current Measurement Interval

```bash
particle get <device-name> intervalSec
```

Returns interval in seconds (e.g., `10`)

**Note:** The measurement interval is persisted to EEPROM and survives device reboots.

#### `doeStatus` - DOE Experiment Status

```bash
particle get <device-name> doeStatus
```

Returns current DOE status:
- `"idle"` - No experiment running
- `"starting"` - Experiment initializing
- `"running"` - Experiment in progress
- `"testing_start_signal"` - Testing start signal parameter (Phase 1/4)
- `"testing_response_timeout"` - Testing response timeout (Phase 2/4)
- `"testing_bit_timeout"` - Testing bit timeout (Phase 3/4)
- `"testing_bit_threshold"` - Testing bit threshold (Phase 4/4)
- `"complete"` - Experiment completed successfully
- `"stopped"` - Experiment stopped by user

#### `doeProgress` - DOE Progress Percentage

```bash
particle get <device-name> doeProgress
```

Returns progress as integer 0-100 (e.g., `45`)

### Published Events

#### `sensor/reading` - Sensor Data

Published every measurement interval with JSON payload (see format above).

**Event Name:** `sensor/reading`
**Visibility:** Private
**Rate:** Every `intervalSec` seconds

#### `sensor/error` - Error Notifications

Published when sensor reading fails after retry.

**Event Name:** `sensor/error`
**Visibility:** Private
**Payloads:**
- `"DHT22 read failed"` - Communication timeout or protocol error
- `"Values out of range"` - Sensor returned invalid values

#### `doe/status` - DOE Experiment Status Updates

Published periodically during DOE experiments to report progress.

**Event Name:** `doe/status`
**Visibility:** Private
**Rate:** At start of each phase and completion

**Payload format:**
```json
{
  "status": "Phase 2/4: Testing response timeout",
  "progress": 45,
  "elapsed": 1350
}
```

**Fields:**
- `status` - Human-readable status message
- `progress` - Progress percentage (0-100)
- `elapsed` - Seconds since experiment started

#### `doe/result` - DOE Test Results

Published after each parameter configuration is tested.

**Event Name:** `doe/result`
**Visibility:** Private
**Rate:** After each configuration test (~every 60 seconds during DOE)

**Payload format:**
```json
{
  "ss": 1100,
  "rt": 200,
  "bt": 100,
  "bth": 50,
  "success": 28,
  "fail": 2,
  "rate": 93.3,
  "best": true
}
```

**Fields:**
- `ss` - Start signal timing (microseconds)
- `rt` - Response timeout (microseconds)
- `bt` - Bit timeout (microseconds)
- `bth` - Bit threshold (microseconds)
- `success` - Number of successful reads
- `fail` - Number of failed reads
- `rate` - Success rate percentage
- `best` - `true` if this is the best configuration so far

## Integration with InfluxDB & Grafana

### Network Architecture Overview

Choose the integration method based on your network setup:

| Method | Inbound Access Needed | Outbound Access Needed | Best For |
|--------|----------------------|------------------------|----------|
| Method 1: Webhooks | ✅ InfluxDB must be publicly accessible | ❌ Not required | Cloud-hosted InfluxDB |
| Method 2: Bridge Service | ❌ No inbound required | ✅ Bridge needs outbound to Particle & InfluxDB | **Firewalled networks (most home/office setups)** |
| Method 3: Telegraf + Webhook | ✅ Telegraf must accept external connections | ❌ Not required | Networks with port forwarding enabled |
| Method 4: API Polling | ❌ No inbound required | ✅ Needs outbound to Particle API | Simple setups, manual polling |

**⚠️ Important:** If your network blocks all incoming connections from the internet (most residential/corporate networks), use **Method 2 (Bridge Service)**.

### Method 1: Particle Webhooks (External InfluxDB)

**Best for:** InfluxDB servers with public internet access

**Network Requirements:** InfluxDB must be accessible from the internet (Particle Cloud → InfluxDB)

1. **Create Webhook** in [Particle Console](https://console.particle.io) → Integrations
2. **Configure:**
   - Event Name: `sensor/reading`
   - URL: `https://your-influxdb-server:8086/api/v2/write?org=your-org&bucket=your-bucket`
   - Request Type: `POST`
   - Headers:
     ```
     Authorization: Token YOUR_INFLUXDB_TOKEN
     Content-Type: application/json
     ```
   - Body:
     ```
     {{{PARTICLE_EVENT_VALUE}}}
     ```

### Method 2: Custom Bridge (Firewalled InfluxDB) ⭐ RECOMMENDED

**Best for:** InfluxDB servers behind firewall with no external/inbound access allowed

**Network Requirements:**
- ❌ No inbound connections required
- ✅ Bridge makes outbound connections to Particle Cloud (subscribes to events)
- ✅ Bridge writes to InfluxDB on local network

**Perfect for:**
- Home networks without port forwarding
- Corporate networks with strict firewall policies
- Synology NAS with no external access
- Dynamic IP addresses
- Networks where exposing services is not allowed

Since the Particle device publishes events to Particle Cloud, but your InfluxDB is not accessible from the internet, you need a bridge service running on your local network that:
1. Makes **outbound** connection to Particle Cloud (subscribes to event stream via Server-Sent Events)
2. Writes to local InfluxDB (internal connection)

**✅ This project includes a ready-to-use Python bridge service in the `bridge/` directory with Docker deployment support.**

See the complete setup guide in [bridge/README.md](bridge/README.md) for step-by-step Synology deployment instructions.

#### Quick Start with Included Bridge Service

The `bridge/` directory contains:
- `particle-bridge.py` - Python service that connects Particle Cloud to InfluxDB
- `Dockerfile` - Container definition
- `docker-compose.yml.example` - Template configuration
- `README.md` - Complete deployment guide

**Basic setup:**
1. Copy `docker-compose.yml.example` to `docker-compose.yml`
2. Edit environment variables with your credentials
3. Deploy to Synology Container Manager or run with `docker-compose up -d`

**Key features:**
- Automatic reconnection on connection loss
- Manual SSE parsing (no external dependencies beyond requests and influxdb-client)
- Correct timestamp conversion (seconds → nanoseconds for InfluxDB)
- Event filtering to process only sensor readings
- Detailed logging for troubleshooting

#### Option A: Node.js Bridge Service (Alternative)

Create a bridge service that runs on your local network:

```javascript
const Particle = require('particle-api-js');
const { InfluxDB, Point } = require('@influxdata/influxdb-client');

// Particle configuration
const particle = new Particle();
const PARTICLE_TOKEN = 'your-particle-token';
const DEVICE_ID = 'your-device-id';

// InfluxDB configuration (local network)
const influxDB = new InfluxDB({
  url: 'http://localhost:8086',  // or your local InfluxDB IP
  token: 'your-local-influxdb-token'
});
const writeApi = influxDB.getWriteApi('your-org', 'your-bucket');

// Subscribe to Particle events
particle.getEventStream({
  deviceId: DEVICE_ID,
  name: 'sensor/reading',
  auth: PARTICLE_TOKEN
}).then(stream => {
  console.log('Connected to Particle event stream');

  stream.on('event', data => {
    try {
      const payload = JSON.parse(data.data);

      // Write to InfluxDB
      const point = new Point(payload.measurement)
        .tag('location', payload.tags.location)
        .tag('device', payload.tags.device)
        .floatField('temperature', payload.fields.temperature)
        .floatField('humidity', payload.fields.humidity)
        .timestamp(new Date(payload.timestamp * 1000));

      writeApi.writePoint(point);
      writeApi.flush();

      console.log(`Written: ${payload.fields.temperature}°C, ${payload.fields.humidity}%`);
    } catch (error) {
      console.error('Error processing event:', error);
    }
  });

  stream.on('error', error => {
    console.error('Stream error:', error);
  });
});

// Graceful shutdown
process.on('SIGTERM', () => {
  writeApi.close().then(() => process.exit(0));
});
```

**Installation:**
```bash
npm install particle-api-js @influxdata/influxdb-client
node bridge.js
```


### Method 3: Telegraf with HTTP Listener + Particle Webhook

**Best for:** Networks with port forwarding enabled and existing Telegraf deployments

**Network Requirements:**
- ✅ Telegraf must accept **inbound** connections from Particle Cloud
- ✅ Port forwarding required (Router → Telegraf)
- ✅ Static IP or Dynamic DNS recommended
- ❌ InfluxDB only needs **internal** network access

**⚠️ WARNING:** This method requires exposing Telegraf to the internet. If your network:
- Blocks all incoming connections
- Has dynamic IP without DDNS
- Is behind strict corporate firewall
- Cannot configure port forwarding

**→ Use Method 2 (Bridge Service) instead**

This method uses Particle Webhooks to push data to Telegraf's HTTP listener.

#### Step 1: Configure Telegraf HTTP Listener

Add to your Telegraf configuration file (usually `/etc/telegraf/telegraf.conf` or on Synology: Portainer container config):

```toml
# Receive Particle webhook data
[[inputs.http_listener_v2]]
  ## Address and port to listen on
  ## On Synology: ensure this port is not blocked by firewall
  service_address = ":8086"

  ## Path to listen on
  path = "/particle"

  ## Data format
  data_format = "json_v2"

  [[inputs.http_listener_v2.json_v2]]
    ## Measurement name (from JSON field)
    measurement_name_path = "measurement"

    ## Timestamp field (Unix timestamp in seconds)
    timestamp_path = "timestamp"
    timestamp_format = "unix"

    ## Tags (device identifiers)
    [[inputs.http_listener_v2.json_v2.tag]]
      path = "tags.location"
      rename = "location"

    [[inputs.http_listener_v2.json_v2.tag]]
      path = "tags.device"
      rename = "device"

    ## Fields (sensor readings)
    [[inputs.http_listener_v2.json_v2.field]]
      path = "fields.temperature"
      rename = "temperature"
      type = "float"

    [[inputs.http_listener_v2.json_v2.field]]
      path = "fields.humidity"
      rename = "humidity"
      type = "float"

# Output to InfluxDB
[[outputs.influxdb_v2]]
  ## InfluxDB URL (adjust for your setup)
  ## On Synology: use container network address or localhost
  urls = ["http://localhost:8086"]

  ## Authentication
  token = "your-influxdb-token"
  organization = "your-org"
  bucket = "your-bucket"

  ## Optional: Add timeout for writes
  timeout = "5s"
```

**Synology-specific notes:**
- If running Telegraf in Docker/Portainer, use bridge networking or host mode
- Map port 8086 (or your chosen port) in container settings
- Ensure Synology firewall allows incoming connections on this port

**Port Forwarding Requirements:**
- Configure your router to forward external port (e.g., 8086) to Synology IP:8086
- In Particle webhook, use your **public IP or domain**: `http://YOUR_PUBLIC_IP:8086/particle`
- Security: Consider using non-standard port (e.g., 18086) and implement rate limiting
- Alternative: Use dynamic DNS service if your ISP changes your IP frequently

#### Step 2: Restart Telegraf

**Linux/Standard:**
```bash
sudo systemctl restart telegraf
sudo systemctl status telegraf
```

**Synology/Docker:**
- Restart the Telegraf container in Portainer or Docker UI
- Check logs to verify HTTP listener started

#### Step 3: Configure Particle Webhook

1. Go to [Particle Console](https://console.particle.io) → Integrations → New Integration → Webhook

2. **Configure the webhook:**
   - **Event Name:** `sensor/reading`
   - **URL:** `http://YOUR_PUBLIC_IP:8086/particle`
     - Use your **public IP address** or domain name (NOT local IP)
     - If using port forwarding, use the external port number
     - Example: `http://203.0.113.45:8086/particle` or `http://mynas.ddns.net:8086/particle`
   - **Request Type:** `POST`
   - **Request Format:** `JSON`
   - **Device:** Select your Particle device (or leave as "Any")
   - **JSON Data:** Leave default - will send `{{PARTICLE_EVENT_VALUE}}`

3. **Advanced Settings:**
   - **Enforce SSL:** No (HTTP only, unless you setup HTTPS with reverse proxy)
   - **HTTP Basic Auth:** Not needed (optional for added security)

4. **Save** the webhook

#### Step 4: Test the Integration

**Trigger a test reading:**
```bash
particle call <device-name> forceReading
```

**Check Telegraf is receiving data:**

Linux:
```bash
sudo journalctl -u telegraf -f
```

Synology/Docker:
```bash
docker logs -f telegraf-container-name
```

You should see logs like:
```
2025-01-18T10:30:00Z I! [inputs.http_listener_v2] Received POST request on /particle
```

**Verify data in InfluxDB:**
```bash
influx query 'from(bucket:"your-bucket")
  |> range(start:-1h)
  |> filter(fn:(r) => r._measurement == "environment")'
```

#### Step 5: Monitor Webhook Status

In Particle Console → Integrations → Your Webhook:
- View **Logs** tab to see webhook execution history
- Check for HTTP response codes (200 = success)
- Look for any error messages

#### Troubleshooting

**Webhook fails (no response):**
- Verify Telegraf container is running
- Check Synology firewall allows port 8086
- **Verify port forwarding** is configured on your router (external → Synology)
- Ensure webhook URL uses **public IP**, not local/private IP (192.168.x.x won't work)
- Test externally: Use online webhook tester or mobile network to POST to your public URL
- Test internally: `curl -X POST http://localhost:8086/particle -d '{"measurement":"test"}'`

**Data not appearing in InfluxDB:**
- Check Telegraf logs for parsing errors
- Verify InfluxDB token has write permissions
- Confirm bucket name is correct
- Test InfluxDB connection: `influx ping`

**Port conflicts:**
- If port 8086 is used by InfluxDB, choose different port for Telegraf (e.g., 8087)
- Update both Telegraf config and Particle webhook URL

### Method 4: Direct Particle API Polling (No Bridge)

Poll the Particle Cloud API periodically from your local network:

```bash
# Cron job to fetch data every minute
* * * * * curl -s "https://api.particle.io/v1/devices/DEVICE_ID/lastReading?access_token=TOKEN" | \
  jq -r '.result' | \
  influx write -b your-bucket -o your-org -
```

### Grafana Dashboard Setup

After data is flowing into InfluxDB, set up Grafana visualization:

#### Step 1: Add InfluxDB Data Source

1. Open Grafana (usually `http://your-server:3000`)
2. Navigate to **Configuration** → **Data Sources**
3. Click **Add data source** → Select **InfluxDB**
4. Configure:
   - **Query Language**: `Flux` (important!)
   - **URL**: `http://your-influxdb-server:8086`
   - **Access**: `Server (default)`
   - **Organization**: Your InfluxDB org name
   - **Token**: Your InfluxDB API token
   - **Default Bucket**: Your bucket name
5. Click **Save & Test** (should show green checkmark)

#### Step 2: Create Dashboard

1. Click **+** → **Dashboard** → **Add visualization**
2. Select your InfluxDB data source
3. Switch to **Code** mode in query editor

**Temperature Panel Query:**
```flux
from(bucket: "your-bucket")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r._measurement == "environment")
  |> filter(fn: (r) => r._field == "temperature")
```

**Panel Settings:**
- Title: `Temperature`
- Unit: `Temperature` → `Celsius (°C)`
- Min/Max: Auto or set custom range

**Humidity Panel Query:**
```flux
from(bucket: "your-bucket")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r._measurement == "environment")
  |> filter(fn: (r) => r._field == "humidity")
```

**Panel Settings:**
- Title: `Humidity`
- Unit: `Percent (0-100)`
- Min: `0`, Max: `100`

4. Arrange panels and save dashboard

#### Step 3: Verify Data Flow

After deployment, verify the complete pipeline:

1. **Trigger test reading:**
   ```bash
   particle call <device-name> forceReading
   ```

2. **Check bridge logs** (should show):
   ```
   [timestamp] Valid sensor reading found!
   [timestamp] ✓ Written: 22.5°C, 39.1%
   ```

3. **Query InfluxDB** to confirm data:
   ```flux
   from(bucket: "your-bucket")
     |> range(start: -1h)
     |> filter(fn: (r) => r._measurement == "environment")
   ```

4. **View in Grafana** - data should appear in your dashboard

#### Common Grafana Issues

**"No data" in dashboard:**
- Verify data exists in InfluxDB first (use InfluxDB UI Data Explorer)
- Check time range selector (top right) - ensure it covers your data timeframe
- Verify query syntax in panel (click query inspector for errors)

**Wrong timestamp range:**
- The bridge converts Unix timestamps (seconds) to nanoseconds for InfluxDB
- If using old data before timestamp fix, it may be timestamped in 2025
- Force new readings to populate correct current data

## Technical Details

### Custom DHT22 Library

This project includes a custom-built DHT22 library (`SimpleDHT22`) optimized for Particle Gen3 devices:

**Features:**
- ✅ Hardware timer (nRF52840 TIMER1) for microsecond-precision timing
- ✅ Automatic retry on communication failure
- ✅ Checksum verification
- ✅ Range validation
- ✅ 2-second minimum interval enforcement (DHT22 requirement)

**Performance:**
- Timing precision: ±1µs (vs ±5µs with OS timing)
- Read success rate: >99% (with retry logic)
- Memory: Flash 15.7KB, RAM 434 bytes

### DHT22 Timing Implementation

The library uses direct nRF52840 hardware timer access for precise timing:

```cpp
// Hardware timer configured at 1MHz (1 tick = 1 microsecond)
NRF_TIMER1->PRESCALER = 4;  // 16MHz / 2^4 = 1MHz

// Precise pulse duration measurement
uint32_t start = getHardwareMicros();
// ... wait for pin state change ...
uint32_t duration = getHardwareMicros() - start;
```

## Customization

### Change Default Interval

Edit [src/RemoteTempHumidityMonitor.ino:25](src/RemoteTempHumidityMonitor.ino#L25):
```cpp
unsigned long measurementInterval = 10000; // milliseconds
int currentInterval = 10; // seconds (for cloud variable)
```

### Change Data Pin

Edit [src/RemoteTempHumidityMonitor.ino:15](src/RemoteTempHumidityMonitor.ino#L15):
```cpp
#define DHTPIN D3  // Change to D2, D4, etc.
```

### Change Location Tag

Edit [src/RemoteTempHumidityMonitor.ino:line in createJsonPayload()](src/RemoteTempHumidityMonitor.ino):
```cpp
writer.name("location").value("your-location-name");
```

### Add Fahrenheit Support

The library reads Celsius. To add Fahrenheit:
```cpp
float tempF = temperature * 9.0 / 5.0 + 32.0;
```

## Troubleshooting

### No Readings / NaN Values

**Symptoms:** Serial shows "DHT22 read failed" or NaN values

**Solutions:**
1. ✅ Check wiring - verify all connections
2. ✅ **Verify 10kΩ pull-up resistor** is installed (REQUIRED)
3. ✅ Ensure DHT22 has stable 3.3V power
4. ✅ Try a different GPIO pin (D2, D4, D5)
5. ✅ Verify genuine DHT22 sensor (not DHT11)
6. ✅ Check for loose connections in breadboard

### Intermittent Failures

**Symptoms:** Readings work sometimes, fail other times

**Solutions:**
1. ✅ Add 100nF capacitor between VCC and GND at sensor
2. ✅ Shorten wire length (<1 meter preferred)
3. ✅ Check for EMI sources nearby
4. ✅ Verify stable power supply
5. ✅ Update to latest firmware (includes retry logic)
6. ✅ **Run DOE experiment** to find optimal timing parameters for your specific hardware setup:
   ```bash
   particle call <device-name> startDOE
   particle subscribe doe/status  # Monitor progress
   ```
   The DOE will systematically test timing parameters and automatically apply the best settings.

### Device Not Connecting to Cloud

**Symptoms:** Functions/variables not appearing in Console

**Solutions:**
1. ✅ Check cellular signal (LED should breathe cyan)
2. ✅ Verify SIM card is activated
3. ✅ Check Device OS version compatibility (6.x)
4. ✅ Reset device and reflash firmware

### Checksum Errors

**Symptoms:** Serial shows "DHT22 checksum failed"

**Solutions:**
1. ✅ Install 10kΩ pull-up resistor if not present
2. ✅ Replace DHT22 sensor (may be faulty)
3. ✅ Check for electrical noise on data line
4. ✅ Reduce wire length between sensor and Boron

## Specifications

### DHT22 Sensor

- **Temperature Range:** -40°C to +80°C (±0.5°C accuracy)
- **Humidity Range:** 0-100% RH (±2% accuracy)
- **Response Time:** 2 seconds (enforced by library)
- **Operating Voltage:** 3.3V - 5.5V
- **Protocol:** Single-wire digital (custom implementation)

### Power Consumption

| Mode | Current | Notes |
|------|---------|-------|
| Deep Sleep | ~5-10mA | Not implemented |
| Normal Operation | ~50-200mA | During cellular transmission |
| Sensor Reading | ~15-20mA | Brief spike |

**Battery Operation Tips:**
- Increase measurement interval (e.g., 300 seconds)
- Implement sleep mode between measurements
- Monitor battery level regularly

## Project Structure

```
RemoteTempHumidityMonitor/
├── src/
│   ├── RemoteTempHumidityMonitor.ino  # Main application
│   ├── SimpleDHT22.h                   # Custom DHT22 library header
│   └── SimpleDHT22.cpp                 # Custom DHT22 library implementation
├── bridge/
│   ├── particle-bridge.py             # Python bridge service
│   ├── Dockerfile                     # Docker container definition
│   ├── docker-compose.yml.example     # Docker Compose template
│   └── README.md                      # Bridge deployment guide
├── project.properties                  # Particle project configuration
├── WIRING.md                          # Detailed wiring diagrams
├── README.md                          # This file
├── .gitignore                         # Git ignore rules
└── LICENSE                            # MIT License
```

## JSON Data Format

```json
{
  "measurement": "environment",
  "tags": {
    "location": "default",
    "device": "unique-device-id"
  },
  "fields": {
    "temperature": 23.45,
    "humidity": 45.67
  },
  "timestamp": 1234567890
}
```

**Fields:**
- `measurement`: Metric category (always "environment")
- `tags.location`: Deployment location (customizable)
- `tags.device`: Unique Particle device ID
- `fields.temperature`: Temperature in Celsius (2 decimal places)
- `fields.humidity`: Relative humidity percentage (2 decimal places)
- `timestamp`: Unix timestamp (seconds since epoch)

## Known Limitations

1. **DHT22 Minimum Interval:** Sensor requires 2 seconds between reads (enforced by library)
2. **Measurement Accuracy:** ±0.5°C temperature, ±2% humidity (sensor limitation)
3. **Temperature Units:** Celsius only (add conversion for Fahrenheit if needed)
4. **Single Sensor:** One DHT22 per device (can be extended for multiple sensors)
5. **Network Dependency:** Requires cellular connection for cloud features

## Future Enhancements

- [ ] Power optimization with sleep modes
- [ ] Battery level monitoring
- [ ] Support for multiple DHT22 sensors
- [ ] Local data logging to SD card
- [ ] OLED display support
- [ ] Alert thresholds with notifications
- [ ] Fahrenheit temperature support
- [ ] Heat index and dew point calculations

## Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Test thoroughly on hardware
4. Submit a pull request

## License

This project is licensed under the MIT License - see [LICENSE](LICENSE) file for details.

## Support

- **Issues:** [GitHub Issues](https://github.com/yourusername/RemoteTempHumidityMonitor/issues)
- **Particle Community:** https://community.particle.io
- **DHT22 Datasheet:** [Available online](https://www.sparkfun.com/datasheets/Sensors/Temperature/DHT22.pdf)
- **Particle Documentation:** https://docs.particle.io

## Acknowledgments

- Custom DHT22 library optimized for nRF52840 hardware timing
- Inspired by various DHT22 implementations for Arduino and Particle
- Built with [Particle Device OS](https://docs.particle.io/reference/device-os/firmware/)

## Author

David Wahl

---

**Firmware Version:** 1.3.0
**Bridge Version:** 1.1.0
**Last Updated:** 2025-01-29
**Compatible Device OS:** 6.x

## Recent Updates

### v1.3.0 - DOE Timing Optimization
- **Design of Experiments (DOE):** Systematic testing framework to find optimal 1-wire timing parameters
- **Configurable Timing:** All DHT22 timing parameters now runtime-adjustable
- **Cloud Functions:** Added `startDOE()` and `stopDOE()` for remote experiment control
- **Cloud Variables:** Added `doeStatus` and `doeProgress` for monitoring
- **Event Publishing:** Real-time `doe/status` and `doe/result` events during experiments
- **Automatic Optimization:** Best parameters automatically applied upon completion
- **Statistical Confidence:** 30 reads per configuration for reliable results

### v1.2.0 - Moving Average & Smart Publishing
- **Moving Average System:** Measurements taken every 10 seconds with configurable publish interval (default 300s)
- **Dynamic Buffer:** Buffer size automatically calculated based on publish interval
- **Smart Publishing:** Publishes when temperature changes ≥0.25°C OR 5× publish interval elapsed
- **Temperature Validation:** >1°C changes trigger automatic retry
- **Sensor Reliability:** DHT22 read failures trigger automatic retry before error reporting

### v1.1.0 - Reliability Enhancements
- Temperature jump validation (>1°C changes trigger retry)
- DHT22 read failure retry
- Bridge connection health monitoring with 630-second timeout
- Automatic reconnection on network loss
