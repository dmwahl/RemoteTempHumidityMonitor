/*
 * Remote Temperature and Humidity Monitor
 * Hardware: Particle Boron + DHT22 Sensor
 *
 * Wiring - External 10kΩ pull-up resistor REQUIRED:
 * DHT22 Pin 1 (VCC) -> Boron 3V3
 * DHT22 Pin 2 (DATA) -> Boron D3 + 10kΩ resistor to 3V3
 * DHT22 Pin 4 (GND) -> Boron GND
 *
 */

#include "Particle.h"
#include "SimpleDHT22.h"

// DHT22 Configuration
#define DHTPIN D3

// EEPROM Configuration
#define EEPROM_PUBLISH_INTERVAL_ADDR 0  // Address to store publish interval
#define EEPROM_MAGIC_ADDR 4             // Address to store magic number (validation)
#define EEPROM_MAGIC 0xA5B4C3D2         // Magic number to validate EEPROM data

// System mode - Use AUTOMATIC for reliable cloud connection
SYSTEM_MODE(AUTOMATIC);

// DHT sensor object - using custom interrupt-based library
SimpleDHT22 dht(DHTPIN);

// Timing Configuration
const unsigned long MEASUREMENT_INTERVAL = 10000; // Fixed 10 seconds in milliseconds
unsigned long publishInterval = 300; // Default 300 seconds (5 minutes), configurable
unsigned long lastMeasurement = 0;
unsigned long lastPublishTime = 0; // Track when we last published

// Moving Average Buffer
#define MAX_BUFFER_SIZE 360 // Maximum buffer size (3600s / 10s = 360 readings max)
float tempBuffer[MAX_BUFFER_SIZE];
float humidityBuffer[MAX_BUFFER_SIZE];
int bufferSize = 30; // Default buffer size (300s / 10s = 30 readings)
int bufferIndex = 0;
int bufferCount = 0; // Number of valid readings in buffer

// Sensor State
float lastValidatedTemp = 0.0; // Last temperature that passed validation
float lastValidatedHumidity = 0.0; // Last humidity that passed validation
float lastPublishedTemp = 0.0; // Last temperature we published
float lastPublishedHumidity = 0.0; // Last humidity we published
bool hasValidLastReading = false; // Track if we have a valid previous reading
bool firstRun = true;

// Cloud variables (read-only from cloud)
String lastReading = "{}";
int currentPublishInterval = 300; // Publish interval in seconds for cloud reading
bool shortMsgEnabled = true; // Short message enabled status
unsigned long shortMsgStartTime = 0; // Track when short messages started (0 = not started)
double cloudTemperature = 0.0; // Cloud-accessible temperature value (moving average)
double cloudHumidity = 0.0; // Cloud-accessible humidity value (moving average)
int readingAge = 0; // Age of last publish in seconds
int bufferFillPercent = 0; // Percentage of buffer filled (for monitoring)

// Function prototypes
void takeMeasurement();
void addToMovingAverage(float temperature, float humidity);
float calculateMovingAverage(float* buffer, int count);
bool shouldPublish(float avgTemp, float avgHumidity);
void publishReading(float temperature, float humidity);
String createJsonPayload(float temperature, float humidity);
String createShortPayload(float temperature, float humidity);
void loadPublishIntervalFromEEPROM();
void savePublishIntervalToEEPROM(int intervalSeconds);
void updateBufferSize();
int setPublishInterval(String command);
int forceReading(String command);
int enableShortMsg(String command);

void setup() {
    // Load saved publish interval from EEPROM
    loadPublishIntervalFromEEPROM();

    // Calculate buffer size based on publish interval
    updateBufferSize();

    // Register cloud functions (must be done in setup before cloud connects)
    Particle.function("setInterval", setPublishInterval);
    Particle.function("forceReading", forceReading);
    Particle.function("enableShort", enableShortMsg);

    // Register cloud variables
    Particle.variable("lastReading", lastReading);
    Particle.variable("publishSec", currentPublishInterval);
    Particle.variable("shortMsg", shortMsgEnabled);
    Particle.variable("temperature", cloudTemperature);
    Particle.variable("humidity", cloudHumidity);
    Particle.variable("readingAge", readingAge);
    Particle.variable("bufferFill", bufferFillPercent);

    // Initialize DHT sensor
    dht.begin();

    // Wait for sensor to stabilize
    delay(2000);

    Log.info("Remote Temp/Humidity Monitor v1.2.0");
    Log.info("Measurement interval: 10 seconds (fixed)");
    Log.info("Publish interval: %d seconds", currentPublishInterval);
    Log.info("Moving average buffer size: %d readings", bufferSize);
    Log.info("Using custom interrupt-based DHT22 library");
    Log.info("DHT22 on D3 - External 10k pullup REQUIRED (internal disabled)");

    // Initialize short message timer
    shortMsgStartTime = Time.now();

    // Wait for cloud connection before first reading
    waitFor(Particle.connected, 60000);

    if (Particle.connected()) {
        Log.info("Cloud connected!");
    } else {
        Log.warn("Cloud not connected");
    }

    // Initialize publish timer
    lastPublishTime = Time.now();

    // Take first reading after 5 seconds
    lastMeasurement = millis() - MEASUREMENT_INTERVAL + 5000;
}

void loop() {
    // Check if it's time for a measurement (every 10 seconds)
    if (millis() - lastMeasurement >= MEASUREMENT_INTERVAL || firstRun) {
        takeMeasurement();
        lastMeasurement = millis();
        firstRun = false;
    }

    // Update reading age (time since last publish)
    if (lastPublishTime > 0) {
        readingAge = Time.now() - lastPublishTime;
    }

    // Allow system to process cloud events
    delay(100);
}

void takeMeasurement() {
    Log.info("--- Taking Measurement ---");

    float temperature = 0;
    float humidity = 0;

    // Read from DHT sensor using custom library
    bool success = dht.read(temperature, humidity);

    // Debug output
    Log.info("Raw values - Temp: %.2f°C, Humidity: %.2f%%, Success: %s",
                    temperature, humidity, success ? "YES" : "NO");

    // Check if reading was successful - retry once if failed
    if (!success) {
        Log.warn("Initial DHT22 read failed, retrying once...");

        // Wait 2 seconds before retry (DHT22 requirement)
        delay(2000);

        // Retry reading
        success = dht.read(temperature, humidity);
        Log.info("Retry values - Temp: %.2f°C, Humidity: %.2f%%, Success: %s",
                        temperature, humidity, success ? "YES" : "NO");

        if (!success) {
            Log.error("DHT22 read failed after retry!");
            Log.info("Troubleshooting:");
            Log.info("  - Add 10kΩ resistor between DATA (D3) and 3V3");
            Log.info("  - Check wiring: DHT22 DATA -> D3");
            Log.info("  - Verify DHT22 has power (3.3V)");
            Log.info("  - Verify DHT22 GND is connected");
            Log.info("  - Ensure proper DHT22 sensor (not DHT11)");
            Log.info("  - Try different pin (D2, D4, D5)");

            // Publish error status (only if connected)
            if (Particle.connected()) {
                Particle.publish("sensor/error", "DHT22 read failed", PRIVATE);
            }
            return;
        } else {
            Log.info("Retry read succeeded!");
        }
    }

    // Validate temperature jump (only if we have a previous reading)
    if (hasValidLastReading) {
        float tempDiff = abs(temperature - lastValidatedTemp);
        if (tempDiff > 1.0) {
            Log.warn("Temperature jump detected: %.2f°C -> %.2f°C (diff: %.2f°C)",
                     lastValidatedTemp, temperature, tempDiff);
            Log.warn("Discarding and re-reading once...");

            // Wait 2 seconds (DHT22 requirement)
            delay(2000);

            // Retry reading once
            float retryTemp = 0;
            float retryHumidity = 0;
            bool retrySuccess = dht.read(retryTemp, retryHumidity);

            if (retrySuccess) {
                float retryDiff = abs(retryTemp - lastValidatedTemp);
                Log.info("Retry read: %.2f°C (diff from last: %.2f°C)", retryTemp, retryDiff);

                if (retryDiff <= 1.0) {
                    // Retry reading is valid, use it
                    Log.info("Retry reading is valid, using it");
                    temperature = retryTemp;
                    humidity = retryHumidity;
                } else {
                    // Both readings show large jump, accept the retry value
                    Log.warn("Retry still shows large jump (%.2f°C), accepting anyway", retryDiff);
                    temperature = retryTemp;
                    humidity = retryHumidity;
                }
            } else {
                // Retry failed, accept original reading
                Log.warn("Retry read failed, accepting original reading");
            }
        }
    }

    // Store validated reading
    hasValidLastReading = true;
    lastValidatedTemp = temperature;
    lastValidatedHumidity = humidity;

    // Add to moving average buffer
    addToMovingAverage(temperature, humidity);

    // Calculate moving averages
    float avgTemp = calculateMovingAverage(tempBuffer, bufferCount);
    float avgHumidity = calculateMovingAverage(humidityBuffer, bufferCount);

    // Update cloud variables with moving averages
    cloudTemperature = avgTemp;
    cloudHumidity = avgHumidity;

    // Log measurement results
    Log.info("Reading successful!");
    Log.info("  Temperature: %.2f°C (%.2f°F)", temperature, temperature * 9.0 / 5.0 + 32.0);
    Log.info("  Humidity: %.2f%%", humidity);
    Log.info("  Moving avg temp: %.2f°C, humidity: %.2f%%", avgTemp, avgHumidity);
    Log.info("  Buffer: %d/%d readings (%.0f%% full)", bufferCount, bufferSize, bufferFillPercent);

    // Check if we should publish
    if (shouldPublish(avgTemp, avgHumidity)) {
        // Update last reading JSON
        lastReading = createJsonPayload(avgTemp, avgHumidity);

        // Check if short message should be disabled (after 1 hour)
        if (shortMsgEnabled && shortMsgStartTime > 0) {
            unsigned long elapsed = Time.now() - shortMsgStartTime;
            if (elapsed >= 3600) {  // 3600 seconds = 1 hour
                shortMsgEnabled = false;
                Log.info("Short message disabled after 1 hour");
                if (Particle.connected()) {
                    Particle.publish("sensor/info", "Short messages disabled", PRIVATE);
                }
            }
        }

        publishReading(avgTemp, avgHumidity);
        lastPublishedTemp = avgTemp;
        lastPublishedHumidity = avgHumidity;
        lastPublishTime = Time.now();
    } else {
        Log.info("Skipping publish (no significant change)");
    }
}

void publishReading(float temperature, float humidity) {
    // Check cloud connection before publishing
    if (!Particle.connected()) {
        Log.warn("Not connected to cloud, skipping publish");
        return;
    }

    // Always publish JSON format for InfluxDB/Grafana
    String jsonData = createJsonPayload(temperature, humidity);
    bool jsonSuccess = Particle.publish("sensor/reading", jsonData, PRIVATE);

    if (jsonSuccess) {
        Log.info("JSON reading published successfully");
    } else {
        Log.error("Failed to publish JSON reading");
    }

    // Additionally publish short message if enabled (within 1 hour)
    if (shortMsgEnabled) {
        String shortData = createShortPayload(temperature, humidity);
        bool shortSuccess = Particle.publish("sensor/short", shortData, PRIVATE);

        if (shortSuccess) {
            Log.info("Short message published: %s", shortData.c_str());
        } else {
            Log.error("Failed to publish short message");
        }
    }
}

String createJsonPayload(float temperature, float humidity) {
    // Create InfluxDB-compatible JSON format using JSONBufferWriter
    // Format: {"measurement":"environment","tags":{"location":"default","device":"boron"},"fields":{"temperature":23.5,"humidity":45.2},"timestamp":1234567890}

    char buffer[256];
    memset(buffer, 0, sizeof(buffer));  // Zero out buffer first
    JSONBufferWriter writer(buffer, sizeof(buffer));

    writer.beginObject();
        writer.name("measurement").value("environment");

        writer.name("tags").beginObject();
            writer.name("location").value("default");
            writer.name("device").value(System.deviceID().c_str());
        writer.endObject();

        writer.name("fields").beginObject();
            writer.name("temperature").value(temperature, 2);
            writer.name("humidity").value(humidity, 2);
        writer.endObject();

        writer.name("timestamp").value(Time.now());
    writer.endObject();

    // Ensure null termination
    writer.buffer()[writer.dataSize()] = '\0';

    return String(writer.buffer());
}

String createShortPayload(float temperature, float humidity) {
    // Create short human-readable format (20 characters or less)
    // Format: "23.5C 45.6%" (max 13 chars for this format)
    char shortBuffer[21];  // 20 chars + null terminator
    snprintf(shortBuffer, sizeof(shortBuffer), "%.1fC %.1f%%", temperature, humidity);
    return String(shortBuffer);
}

// Add reading to moving average buffer
void addToMovingAverage(float temperature, float humidity) {
    // Add to circular buffer
    tempBuffer[bufferIndex] = temperature;
    humidityBuffer[bufferIndex] = humidity;

    // Update index (circular buffer)
    bufferIndex = (bufferIndex + 1) % bufferSize;

    // Update count (up to bufferSize)
    if (bufferCount < bufferSize) {
        bufferCount++;
    }

    // Update buffer fill percentage
    bufferFillPercent = (bufferCount * 100) / bufferSize;
}

// Calculate moving average from buffer
float calculateMovingAverage(float* buffer, int count) {
    if (count == 0) return 0.0;

    float sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += buffer[i];
    }
    return sum / count;
}

// Determine if we should publish based on temperature change or time elapsed
bool shouldPublish(float avgTemp, float avgHumidity) {
    // Always publish the first reading
    if (lastPublishTime == 0) {
        Log.info("Publishing first reading");
        return true;
    }

    // Calculate time since last publish
    unsigned long timeSincePublish = Time.now() - lastPublishTime;

    // Check if 5x publish interval has elapsed (force publish)
    if (timeSincePublish >= (publishInterval * 5)) {
        Log.info("Publishing: 5x interval elapsed (%lu >= %lu seconds)",
                 timeSincePublish, publishInterval * 5);
        return true;
    }

    // Check if temperature changed by >= 0.25°C
    float tempChange = abs(avgTemp - lastPublishedTemp);
    if (tempChange >= 0.25) {
        Log.info("Publishing: temp changed %.2f°C (>= 0.25°C)", tempChange);
        return true;
    }

    // No significant change
    return false;
}

// Load publish interval from EEPROM
void loadPublishIntervalFromEEPROM() {
    uint32_t magic;
    uint32_t storedInterval;

    // Check if EEPROM has valid data by reading magic number
    EEPROM.get(EEPROM_MAGIC_ADDR, magic);

    if (magic == EEPROM_MAGIC) {
        // Valid data exists, load the interval
        EEPROM.get(EEPROM_PUBLISH_INTERVAL_ADDR, storedInterval);

        // Validate the loaded interval (30 seconds to 1 hour)
        if (storedInterval >= 30 && storedInterval <= 3600) {
            publishInterval = storedInterval;
            currentPublishInterval = storedInterval;
            Log.info("Loaded publish interval from EEPROM: %d seconds", storedInterval);
        } else {
            Log.warn("EEPROM publish interval invalid (%d), using default", storedInterval);
        }
    } else {
        Log.info("No valid EEPROM data found, using default publish interval");
        // Initialize EEPROM with default value
        savePublishIntervalToEEPROM(currentPublishInterval);
    }
}

// Save publish interval to EEPROM
void savePublishIntervalToEEPROM(int intervalSeconds) {
    uint32_t interval32 = (uint32_t)intervalSeconds;
    uint32_t magic = EEPROM_MAGIC;

    // Save interval and magic number
    EEPROM.put(EEPROM_PUBLISH_INTERVAL_ADDR, interval32);
    EEPROM.put(EEPROM_MAGIC_ADDR, magic);

    Log.info("Saved publish interval to EEPROM: %d seconds", intervalSeconds);
}

// Update buffer size based on publish interval
void updateBufferSize() {
    // Buffer size = publishInterval / 10 seconds (measurement interval)
    // Minimum 3 readings, maximum 360 readings
    int newBufferSize = publishInterval / 10;
    if (newBufferSize < 3) newBufferSize = 3;
    if (newBufferSize > MAX_BUFFER_SIZE) newBufferSize = MAX_BUFFER_SIZE;

    bufferSize = newBufferSize;
    bufferFillPercent = (bufferCount * 100) / bufferSize;

    Log.info("Buffer size updated to %d readings", bufferSize);
}

// Cloud function to set publish interval (in seconds)
int setPublishInterval(String command) {
    int newInterval = command.toInt();

    // Validate interval (minimum 30 seconds, maximum 1 hour)
    if (newInterval < 30 || newInterval > 3600) {
        Log.error("Invalid publish interval %d seconds (must be 30-3600)", newInterval);
        return -1;
    }

    publishInterval = newInterval;
    currentPublishInterval = newInterval;

    // Update buffer size to match new interval
    updateBufferSize();

    // Save to EEPROM for persistence across reboots
    savePublishIntervalToEEPROM(newInterval);

    Log.info("Publish interval updated to %d seconds", newInterval);
    Particle.publish("config/interval", String(newInterval), PRIVATE);

    return newInterval;
}

// Cloud function to force an immediate reading
int forceReading(String command) {
    Log.info("Force reading requested from cloud");
    takeMeasurement();
    return 1;
}

// Cloud function to enable/disable short messages
int enableShortMsg(String command) {
    // Parse command: empty, "1", or no input = enable, "0" = disable
    bool shouldEnable = true;

    if (command.length() > 0) {
        int value = command.toInt();
        shouldEnable = (value != 0);
    }

    if (shouldEnable) {
        // Enable short messages and reset timer
        shortMsgEnabled = true;
        shortMsgStartTime = Time.now();
        Log.info("Short messages enabled");
        Particle.publish("config/shortmsg", "enabled", PRIVATE);
        return 1;
    } else {
        // Disable short messages
        shortMsgEnabled = false;
        Log.info("Short messages disabled");
        Particle.publish("config/shortmsg", "disabled", PRIVATE);
        return 0;
    }
}
