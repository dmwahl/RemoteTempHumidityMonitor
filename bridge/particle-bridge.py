from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS
import json
import os
import time
import requests

# Get configuration from environment variables
PARTICLE_TOKEN = os.environ.get('PARTICLE_TOKEN')
DEVICE_ID = os.environ.get('DEVICE_ID')
EVENT_NAME = os.environ.get('EVENT_NAME', 'sensor/reading')
INFLUX_URL = os.environ.get('INFLUX_URL', 'http://localhost:8086')
INFLUX_TOKEN = os.environ.get('INFLUX_TOKEN')
INFLUX_ORG = os.environ.get('INFLUX_ORG')
INFLUX_BUCKET = os.environ.get('INFLUX_BUCKET')

print(f"Starting Particle to InfluxDB Bridge")
print(f"Device ID: {DEVICE_ID}")
print(f"Event Name: {EVENT_NAME}")
print(f"InfluxDB URL: {INFLUX_URL}")
print(f"InfluxDB Org: {INFLUX_ORG}")
print(f"InfluxDB Bucket: {INFLUX_BUCKET}")

# Connect to InfluxDB
influx_client = InfluxDBClient(
    url=INFLUX_URL,
    token=INFLUX_TOKEN,
    org=INFLUX_ORG
)
write_api = influx_client.write_api(write_options=SYNCHRONOUS)

# Particle Server-Sent Events (SSE) endpoint
# Note: Particle API filters events by prefix, so "sensor" will match "sensor/reading"
if DEVICE_ID:
    # Subscribe to specific device events
    url = f'https://api.particle.io/v1/devices/{DEVICE_ID}/events?access_token={PARTICLE_TOKEN}'
else:
    # Subscribe to all devices
    url = f'https://api.particle.io/v1/events?access_token={PARTICLE_TOKEN}'

def process_event(event_data):
    """Process a single event and write to InfluxDB"""
    try:
        data = json.loads(event_data)

        timestamp = time.strftime('%Y-%m-%d %H:%M:%S')
        print(f"[{timestamp}] Processing event:")
        print(f"  Measurement: {data['measurement']}")
        print(f"  Tags: {data['tags']}")
        print(f"  Fields: {data['fields']}")
        print(f"  Timestamp: {data['timestamp']}")

        # Create InfluxDB point
        # Note: Particle device sends timestamp in seconds, InfluxDB expects nanoseconds
        timestamp_ns = int(data['timestamp']) * 1_000_000_000

        point = Point(data['measurement']) \
            .tag('location', data['tags']['location']) \
            .tag('device', data['tags']['device']) \
            .field('temperature', float(data['fields']['temperature'])) \
            .field('humidity', float(data['fields']['humidity'])) \
            .time(timestamp_ns)

        print(f"  Writing to bucket: {INFLUX_BUCKET}")
        print(f"  Point: {point.to_line_protocol()}")

        # Write to InfluxDB
        write_api.write(bucket=INFLUX_BUCKET, record=point)

        print(f"[{timestamp}] ✓ Written: {data['fields']['temperature']}°C, {data['fields']['humidity']}%")

    except json.JSONDecodeError as e:
        timestamp = time.strftime('%Y-%m-%d %H:%M:%S')
        print(f"[{timestamp}] JSON decode error: {e}")
    except Exception as e:
        timestamp = time.strftime('%Y-%m-%d %H:%M:%S')
        print(f"[{timestamp}] Error processing event: {e}")
        import traceback
        traceback.print_exc()

# Main loop with reconnection logic
def main():
    retry_delay = 5
    max_retry_delay = 60

    while True:
        try:
            print(f"Connecting to Particle Cloud event stream...")
            print(f"URL: {url.replace(PARTICLE_TOKEN, 'REDACTED')}")

            # Subscribe to Particle event stream via SSE using requests
            response = requests.get(url, stream=True, headers={'Accept': 'text/event-stream'})
            response.raise_for_status()

            print('✓ Connected! Listening for events...')
            retry_delay = 5  # Reset retry delay on successful connection

            # Process SSE stream manually
            buffer = ''
            for chunk in response.iter_content(chunk_size=1, decode_unicode=True):
                if chunk:
                    buffer += chunk
                    if '\n\n' in buffer:
                        # SSE messages are separated by double newlines
                        messages = buffer.split('\n\n')
                        buffer = messages[-1]  # Keep incomplete message in buffer

                        for message in messages[:-1]:
                            if message.strip():
                                # Parse SSE message - Particle only sends data: lines, no event: lines
                                lines = message.split('\n')
                                event_data = None

                                for line in lines:
                                    if line.startswith('data: '):
                                        event_data = line[6:]
                                        break

                                if event_data:
                                    timestamp = time.strftime('%Y-%m-%d %H:%M:%S')
                                    try:
                                        # Parse the Particle event wrapper JSON
                                        wrapper = json.loads(event_data)

                                        # The wrapper doesn't have a 'name' field, but we can filter by checking
                                        # if 'data' field contains our expected JSON structure
                                        if 'data' in wrapper:
                                            print(f"[{timestamp}] Event received, data: {wrapper['data'][:100]}...")

                                            # Try to parse the data field as JSON
                                            try:
                                                sensor_data = json.loads(wrapper['data'])
                                                # If it parses as JSON with our expected structure, process it
                                                if 'measurement' in sensor_data and 'fields' in sensor_data:
                                                    print(f"[{timestamp}] Valid sensor reading found!")
                                                    process_event(wrapper['data'])
                                                else:
                                                    print(f"[{timestamp}] Event data is not sensor reading format")
                                            except (json.JSONDecodeError, TypeError):
                                                # Data field is not JSON or wrong format - skip it
                                                print(f"[{timestamp}] Event data is not JSON: {wrapper['data']}")
                                                pass
                                        else:
                                            print(f"[{timestamp}] Event has no data field")

                                    except json.JSONDecodeError as e:
                                        print(f"[{timestamp}] Failed to parse event wrapper: {e}")
                                        pass

        except KeyboardInterrupt:
            print("\nShutting down...")
            break
        except Exception as e:
            timestamp = time.strftime('%Y-%m-%d %H:%M:%S')
            print(f"[{timestamp}] Connection error: {e}")
            print(f"Retrying in {retry_delay} seconds...")
            time.sleep(retry_delay)

            # Exponential backoff
            retry_delay = min(retry_delay * 2, max_retry_delay)

if __name__ == '__main__':
    main()
