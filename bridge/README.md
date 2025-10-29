# Particle to InfluxDB Bridge

This bridge service subscribes to Particle Cloud events and writes sensor data to your local InfluxDB instance.

## Network Requirements

- ✅ **No inbound connections required**
- ✅ Makes outbound connections to Particle Cloud (subscribes to events)
- ✅ Writes to InfluxDB on local network
- ✅ Works behind firewalls, with dynamic IPs, no port forwarding needed

## Files

- `particle-bridge.py` - Python script that bridges Particle Cloud to InfluxDB
- `Dockerfile` - Container definition
- `docker-compose.yml` - Docker Compose configuration (edit this with your credentials)

## Setup Instructions

### Step 1: Get Your Credentials

**Particle Token:**
```bash
particle token list
```

**Device ID:**
```bash
particle list
```

**InfluxDB Info:**
- URL (e.g., `http://192.168.1.100:8086` or `http://influxdb:8086`)
- Token (from InfluxDB UI → Load Data → API Tokens)
- Organization name
- Bucket name

### Step 2: Configure docker-compose.yml

Edit `docker-compose.yml` and replace:
- `your-particle-token-here` → Your Particle access token
- `your-device-id-here` → Your Particle device ID
- `http://influxdb:8086` → Your InfluxDB URL
- `your-influxdb-token-here` → Your InfluxDB token
- `your-org-name` → Your InfluxDB organization
- `your-bucket-name` → Your InfluxDB bucket

### Step 3: Deploy to Synology

**Using Container Manager (GUI):**

1. Open **File Station** on Synology
2. Navigate to or create `/docker/particle-bridge`
3. Upload all files from this folder:
   - `particle-bridge.py`
   - `Dockerfile`
   - `docker-compose.yml` (with your credentials filled in)

4. Open **Container Manager**
5. Go to **Project** tab → **Create**
6. Set project name: `particle-bridge`
7. Set path: `/docker/particle-bridge`
8. Click **Build** (first time will take a few minutes)
9. Click **▶ Start**

**Verify:**
- Container status should be **Running**
- Check **Logs** tab for:
  ```
  ✓ Listening for sensor/reading events...
  ```

### Step 4: Test Data Flow

Trigger a reading:
```bash
particle call <device-name> forceReading
```

Check container logs for:
```
[2025-01-18 10:30:00] Written: 23.5°C, 45.6%
```

Verify in InfluxDB:
- Open InfluxDB UI → Data Explorer
- Query bucket for `environment` measurement
- Should see temperature and humidity data

## Troubleshooting

### Container Issues

**Container won't start:**
- Check all environment variables are set in docker-compose.yml
- Verify credentials are correct (no extra spaces)
- Check container logs for specific error messages

**Need to rebuild after code changes:**
1. Stop the container in Container Manager
2. Delete the container
3. Delete the image (important!)
4. Rebuild and start fresh

### Connection Issues

**"Connection refused" to InfluxDB (Errno 111):**
- ❌ **Don't use** `http://localhost:8086` from inside container
- ✅ **Use Synology IP** instead: `http://192.168.x.x:8086`
- Alternative: Use `http://host.docker.internal:8086` (may not work on all Docker versions)
- If InfluxDB is in another container on same network: `http://influxdb:8086`

**"404 Not Found" from Particle API:**
- Fixed in latest version - ensure you have the updated `particle-bridge.py`
- URL should be: `https://api.particle.io/v1/devices/{id}/events` (no event name in path)

**"Authentication failed" to Particle:**
- Verify Particle token: `particle token list`
- Token may have expired - create new one: `particle token create`

### Data Flow Issues

**Events connecting but no data appears:**
- Check if events are being filtered out (wrong event name)
- Look for "Event filtered out" messages in logs
- Verify event name is `sensor/reading` in both device and docker-compose.yml

**Data in InfluxDB but wrong timestamps:**
- Old issue (before fix): timestamps were in seconds, InfluxDB needs nanoseconds
- Updated bridge multiplies timestamp by 1,000,000,000
- Old data may appear in year 2025 - trigger new readings for correct timestamps

**"No data" in Grafana but data exists in InfluxDB:**
- Check Grafana time range selector (top right)
- Verify Flux query syntax
- Test query directly in InfluxDB Data Explorer first
- Ensure data source is configured for **Flux** query language (not InfluxQL)

### Debug Mode

For detailed troubleshooting, the bridge includes debug logging showing:
- Raw SSE data received from Particle Cloud
- Event filtering decisions
- InfluxDB line protocol format
- Full error stack traces

Check container logs in Container Manager → Container → Details → Logs

## Architecture

```
Particle Device → Particle Cloud → Bridge Container (outbound) → InfluxDB
```

The bridge:
- Makes only outbound connections (no firewall rules needed)
- Auto-restarts if it crashes
- Auto-starts on Synology boot
- Runs 24/7 processing events in real-time
