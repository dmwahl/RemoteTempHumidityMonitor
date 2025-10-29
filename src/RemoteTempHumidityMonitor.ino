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
#define EEPROM_INTERVAL_ADDR 0  // Address to store measurement interval
#define EEPROM_MAGIC_ADDR 4     // Address to store magic number (validation)
#define EEPROM_MAGIC 0xA5B4C3D2 // Magic number to validate EEPROM data

// System mode - Use AUTOMATIC for reliable cloud connection
SYSTEM_MODE(AUTOMATIC);

// DHT sensor object - using custom interrupt-based library
SimpleDHT22 dht(DHTPIN);

// Global variables
unsigned long measurementInterval = 10000; // Default 10 seconds in milliseconds
unsigned long lastMeasurement = 0;
unsigned long lastSuccessfulReading = 0; // Timestamp of last successful reading
float lastTemperature = 0.0;
float lastHumidity = 0.0;
bool firstRun = true;

// Cloud variables (read-only from cloud)
String lastReading = "{}";
int currentInterval = 10; // In seconds for easier cloud reading
bool shortMsgEnabled = true; // Short message enabled status
unsigned long shortMsgStartTime = 0; // Track when short messages started (0 = not started)
double cloudTemperature = 0.0; // Cloud-accessible temperature value
double cloudHumidity = 0.0; // Cloud-accessible humidity value
int readingAge = 0; // Age of last reading in seconds

// Function prototypes
void takeMeasurement();
void publishReading(float temperature, float humidity);
String createJsonPayload(float temperature, float humidity);
String createShortPayload(float temperature, float humidity);
void loadIntervalFromEEPROM();
void saveIntervalToEEPROM(int intervalSeconds);
int setInterval(String command);
int forceReading(String command);
int enableShortMsg(String command);

void setup() {
    // Load saved measurement interval from EEPROM
    loadIntervalFromEEPROM();

    // Register cloud functions (must be done in setup before cloud connects)
    Particle.function("setInterval", setInterval);
    Particle.function("forceReading", forceReading);
    Particle.function("enableShort", enableShortMsg);

    // Register cloud variables
    Particle.variable("lastReading", lastReading);
    Particle.variable("intervalSec", currentInterval);
    Particle.variable("shortMsg", shortMsgEnabled);
    Particle.variable("temperature", cloudTemperature);
    Particle.variable("humidity", cloudHumidity);
    Particle.variable("readingAge", readingAge);

    // Initialize DHT sensor
    dht.begin();

    // Wait for sensor to stabilize
    delay(2000);

    Log.info("Remote Temp/Humidity Monitor Initialized");
    Log.info("Measurement interval: %d seconds", currentInterval);
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

    // Take first reading after 5 seconds
    lastMeasurement = millis() - measurementInterval + 5000;
}

void loop() {
    // Check if it's time for a measurement
    if (millis() - lastMeasurement >= measurementInterval || firstRun) {
        takeMeasurement();
        lastMeasurement = millis();
        firstRun = false;
    }

    // Update reading age if we have a successful reading
    if (lastSuccessfulReading > 0) {
        readingAge = Time.now() - lastSuccessfulReading;
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

    // Check if reading was successful
    if (!success) {
        Log.error("Failed to read from DHT sensor!");
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
    }

    // Store last values and update cloud variables
    lastTemperature = temperature;
    lastHumidity = humidity;
    cloudTemperature = temperature;
    cloudHumidity = humidity;
    lastSuccessfulReading = Time.now();
    readingAge = 0; // Just updated

    // Always create and store JSON format
    lastReading = createJsonPayload(temperature, humidity);

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

    // Log measurement results
    Log.info("Reading successful!");
    Log.info("  Temperature: %.2f°C (%.2f°F)", temperature, temperature * 9.0 / 5.0 + 32.0);
    Log.info("  Humidity: %.2f%%", humidity);
    Log.info("  Dew Point: %.2f°C", temperature - ((100 - humidity) / 5.0));
    Log.info("  Short message: %s", shortMsgEnabled ? "enabled" : "disabled");
    Log.info("JSON: %s", lastReading.c_str());

    // Publish to cloud
    publishReading(temperature, humidity);
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

// Load measurement interval from EEPROM
void loadIntervalFromEEPROM() {
    uint32_t magic;
    uint32_t storedInterval;

    // Check if EEPROM has valid data by reading magic number
    EEPROM.get(EEPROM_MAGIC_ADDR, magic);

    if (magic == EEPROM_MAGIC) {
        // Valid data exists, load the interval
        EEPROM.get(EEPROM_INTERVAL_ADDR, storedInterval);

        // Validate the loaded interval (10 seconds to 1 hour)
        if (storedInterval >= 10 && storedInterval <= 3600) {
            measurementInterval = storedInterval * 1000; // Convert to milliseconds
            currentInterval = storedInterval;
            Log.info("Loaded interval from EEPROM: %d seconds", storedInterval);
        } else {
            Log.warn("EEPROM interval invalid (%d), using default", storedInterval);
        }
    } else {
        Log.info("No valid EEPROM data found, using default interval");
        // Initialize EEPROM with default value
        saveIntervalToEEPROM(currentInterval);
    }
}

// Save measurement interval to EEPROM
void saveIntervalToEEPROM(int intervalSeconds) {
    uint32_t interval32 = (uint32_t)intervalSeconds;
    uint32_t magic = EEPROM_MAGIC;

    // Save interval and magic number
    EEPROM.put(EEPROM_INTERVAL_ADDR, interval32);
    EEPROM.put(EEPROM_MAGIC_ADDR, magic);

    Log.info("Saved interval to EEPROM: %d seconds", intervalSeconds);
}

// Cloud function to set measurement interval (in seconds)
int setInterval(String command) {
    int newInterval = command.toInt();

    // Validate interval (minimum 10 seconds, maximum 1 hour)
    if (newInterval < 10 || newInterval > 3600) {
        Log.error("Invalid interval %d seconds (must be 10-3600)", newInterval);
        return -1;
    }

    measurementInterval = newInterval * 1000; // Convert to milliseconds
    currentInterval = newInterval;

    // Save to EEPROM for persistence across reboots
    saveIntervalToEEPROM(newInterval);

    Log.info("Measurement interval updated to %d seconds", newInterval);
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
