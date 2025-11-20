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
#define EEPROM_PUBLISH_INTERVAL_ADDR 0  // Address to store publish interval (4 bytes)
#define EEPROM_MAGIC_ADDR 4             // Address to store magic number (4 bytes)
#define EEPROM_START_SIGNAL_ADDR 8      // Address to store start signal timing (2 bytes)
#define EEPROM_RESPONSE_TIMEOUT_ADDR 10 // Address to store response timeout (2 bytes)
#define EEPROM_BIT_TIMEOUT_ADDR 12      // Address to store bit timeout (2 bytes)
#define EEPROM_BIT_THRESHOLD_ADDR 14    // Address to store bit threshold (2 bytes)
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

// DOE (Design of Experiments) State
bool doeActive = false; // DOE experiment is running
String doeStatus = "idle"; // Current DOE status
int doeProgress = 0; // Progress percentage (0-100)
unsigned long doeStartTime = 0; // When DOE started

// DOE Phase Results (stored as cloud-accessible JSON strings)
String doePhase1Summary = "{}"; // Start signal phase summary
String doePhase2Summary = "{}"; // Response timeout phase summary
String doePhase3Summary = "{}"; // Bit timeout phase summary
String doePhase4Summary = "{}"; // Bit threshold phase summary

// DOE Configuration
struct DOEConfig {
    // Parameter ranges for testing (in microseconds)
    uint16_t startSignalMin = 800;
    uint16_t startSignalMax = 2000;
    uint16_t startSignalStep = 100;

    uint16_t responseTimeoutMin = 150;
    uint16_t responseTimeoutMax = 300;
    uint16_t responseTimeoutStep = 10;

    uint16_t bitTimeoutMin = 80;
    uint16_t bitTimeoutMax = 150;
    uint16_t bitTimeoutStep = 5;

    uint16_t bitThresholdMin = 40;
    uint16_t bitThresholdMax = 60;
    uint16_t bitThresholdStep = 2;

    int testsPerConfig = 30; // Number of reads per configuration
};

DOEConfig doeConfig;

// DOE Results
struct DOEResult {
    uint16_t startSignal;
    uint16_t responseTimeout;
    uint16_t bitTimeout;
    uint16_t bitThreshold;
    int successCount;
    int failCount;
    float successRate;
};

// Best result tracking (initialized with DOE-optimized defaults)
DOEResult bestResult = {1600, 240, 115, 46, 0, 0, 0.0};

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
void loadTimingParametersFromEEPROM();
void saveTimingParametersToEEPROM();
void updateBufferSize();
int setPublishInterval(String command);
int forceReading(String command);
int enableShortMsg(String command);
int setStartSignalTiming(String command);
int setResponseTimeoutTiming(String command);
int setBitTimeoutTiming(String command);
int setBitThresholdTiming(String command);

// DOE function prototypes
int startDOE(String command);
int stopDOE(String command);
void runDOEExperiment();
DOEResult testParameterSet(uint16_t startSignal, uint16_t responseTimeout,
                           uint16_t bitTimeout, uint16_t bitThreshold);
void publishDOEStatus(String status);
void publishDOEResult(DOEResult result, bool isBest);
void publishPhaseSummary(String paramName, DOEResult* results, int resultCount);

void setup() {
    // Load saved publish interval from EEPROM
    loadPublishIntervalFromEEPROM();

    // Calculate buffer size based on publish interval
    updateBufferSize();

    // Register cloud functions (must be done in setup before cloud connects)
    Particle.function("setInterval", setPublishInterval);
    Particle.function("forceReading", forceReading);
    Particle.function("enableShort", enableShortMsg);
    Particle.function("startDOE", startDOE);
    Particle.function("stopDOE", stopDOE);
    Particle.function("setStartSig", setStartSignalTiming);
    Particle.function("setRespTO", setResponseTimeoutTiming);
    Particle.function("setBitTO", setBitTimeoutTiming);
    Particle.function("setBitThr", setBitThresholdTiming);

    // Register cloud variables
    Particle.variable("lastReading", lastReading);
    Particle.variable("publishSec", currentPublishInterval);
    Particle.variable("shortMsg", shortMsgEnabled);
    Particle.variable("temperature", cloudTemperature);
    Particle.variable("humidity", cloudHumidity);
    Particle.variable("readingAge", readingAge);
    Particle.variable("bufferFill", bufferFillPercent);
    Particle.variable("doeStatus", doeStatus);
    Particle.variable("doeProgress", doeProgress);
    Particle.variable("doePhase1", doePhase1Summary);
    Particle.variable("doePhase2", doePhase2Summary);
    Particle.variable("doePhase3", doePhase3Summary);
    Particle.variable("doePhase4", doePhase4Summary);

    // Initialize DHT sensor
    dht.begin();

    // Load saved timing parameters from EEPROM
    loadTimingParametersFromEEPROM();

    // Wait for sensor to stabilize
    delay(2000);

    Log.info("Remote Temp/Humidity Monitor v1.3.0");
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
    // If DOE experiment is active, run it instead of normal measurements
    if (doeActive) {
        runDOEExperiment();
        // DOE will set doeActive to false when complete
        return;
    }

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

// ====================================================================
// Timing Parameter Configuration Functions
// ====================================================================

// Load timing parameters from EEPROM
void loadTimingParametersFromEEPROM() {
    uint32_t magic;
    uint16_t startSignal, responseTimeout, bitTimeout, bitThreshold;

    // Check if EEPROM has valid data by reading magic number
    EEPROM.get(EEPROM_MAGIC_ADDR, magic);

    if (magic == EEPROM_MAGIC) {
        // Valid data exists, load the timing parameters
        EEPROM.get(EEPROM_START_SIGNAL_ADDR, startSignal);
        EEPROM.get(EEPROM_RESPONSE_TIMEOUT_ADDR, responseTimeout);
        EEPROM.get(EEPROM_BIT_TIMEOUT_ADDR, bitTimeout);
        EEPROM.get(EEPROM_BIT_THRESHOLD_ADDR, bitThreshold);

        // Validate loaded values against DOE limits
        bool valid = true;
        if (startSignal < doeConfig.startSignalMin || startSignal > doeConfig.startSignalMax) {
            Log.warn("EEPROM start signal %d out of range, using default", startSignal);
            valid = false;
        }
        if (responseTimeout < doeConfig.responseTimeoutMin || responseTimeout > doeConfig.responseTimeoutMax) {
            Log.warn("EEPROM response timeout %d out of range, using default", responseTimeout);
            valid = false;
        }
        if (bitTimeout < doeConfig.bitTimeoutMin || bitTimeout > doeConfig.bitTimeoutMax) {
            Log.warn("EEPROM bit timeout %d out of range, using default", bitTimeout);
            valid = false;
        }
        if (bitThreshold < doeConfig.bitThresholdMin || bitThreshold > doeConfig.bitThresholdMax) {
            Log.warn("EEPROM bit threshold %d out of range, using default", bitThreshold);
            valid = false;
        }

        if (valid) {
            // Apply loaded parameters to DHT22
            dht.setStartSignal(startSignal);
            dht.setResponseTimeout(responseTimeout);
            dht.setBitTimeout(bitTimeout);
            dht.setBitThreshold(bitThreshold);

            Log.info("Loaded timing parameters from EEPROM:");
            Log.info("  Start Signal: %d us", startSignal);
            Log.info("  Response Timeout: %d us", responseTimeout);
            Log.info("  Bit Timeout: %d us", bitTimeout);
            Log.info("  Bit Threshold: %d us", bitThreshold);
        } else {
            Log.info("Using default timing parameters");
        }
    } else {
        Log.info("No valid timing parameters in EEPROM, using defaults");
    }
}

// Save current timing parameters to EEPROM
void saveTimingParametersToEEPROM() {
    uint16_t startSignal = dht.getStartSignal();
    uint16_t responseTimeout = dht.getResponseTimeout();
    uint16_t bitTimeout = dht.getBitTimeout();
    uint16_t bitThreshold = dht.getBitThreshold();

    // Save parameters
    EEPROM.put(EEPROM_START_SIGNAL_ADDR, startSignal);
    EEPROM.put(EEPROM_RESPONSE_TIMEOUT_ADDR, responseTimeout);
    EEPROM.put(EEPROM_BIT_TIMEOUT_ADDR, bitTimeout);
    EEPROM.put(EEPROM_BIT_THRESHOLD_ADDR, bitThreshold);

    Log.info("Saved timing parameters to EEPROM:");
    Log.info("  Start Signal: %d us", startSignal);
    Log.info("  Response Timeout: %d us", responseTimeout);
    Log.info("  Bit Timeout: %d us", bitTimeout);
    Log.info("  Bit Threshold: %d us", bitThreshold);
}

// Cloud function to set start signal timing
int setStartSignalTiming(String command) {
    int value = command.toInt();

    // Validate against DOE limits
    if (value < doeConfig.startSignalMin || value > doeConfig.startSignalMax) {
        Log.error("Start signal %d out of range (%d-%d us)",
                  value, doeConfig.startSignalMin, doeConfig.startSignalMax);
        return -1;
    }

    // Apply new value
    dht.setStartSignal((uint16_t)value);

    // Save to EEPROM
    saveTimingParametersToEEPROM();

    Log.info("Start signal timing updated to %d us", value);
    Particle.publish("config/timing", String::format("start_signal=%d", value), PRIVATE);

    return value;
}

// Cloud function to set response timeout timing
int setResponseTimeoutTiming(String command) {
    int value = command.toInt();

    // Validate against DOE limits
    if (value < doeConfig.responseTimeoutMin || value > doeConfig.responseTimeoutMax) {
        Log.error("Response timeout %d out of range (%d-%d us)",
                  value, doeConfig.responseTimeoutMin, doeConfig.responseTimeoutMax);
        return -1;
    }

    // Apply new value
    dht.setResponseTimeout((uint16_t)value);

    // Save to EEPROM
    saveTimingParametersToEEPROM();

    Log.info("Response timeout updated to %d us", value);
    Particle.publish("config/timing", String::format("response_timeout=%d", value), PRIVATE);

    return value;
}

// Cloud function to set bit timeout timing
int setBitTimeoutTiming(String command) {
    int value = command.toInt();

    // Validate against DOE limits
    if (value < doeConfig.bitTimeoutMin || value > doeConfig.bitTimeoutMax) {
        Log.error("Bit timeout %d out of range (%d-%d us)",
                  value, doeConfig.bitTimeoutMin, doeConfig.bitTimeoutMax);
        return -1;
    }

    // Apply new value
    dht.setBitTimeout((uint16_t)value);

    // Save to EEPROM
    saveTimingParametersToEEPROM();

    Log.info("Bit timeout updated to %d us", value);
    Particle.publish("config/timing", String::format("bit_timeout=%d", value), PRIVATE);

    return value;
}

// Cloud function to set bit threshold timing
int setBitThresholdTiming(String command) {
    int value = command.toInt();

    // Validate against DOE limits
    if (value < doeConfig.bitThresholdMin || value > doeConfig.bitThresholdMax) {
        Log.error("Bit threshold %d out of range (%d-%d us)",
                  value, doeConfig.bitThresholdMin, doeConfig.bitThresholdMax);
        return -1;
    }

    // Apply new value
    dht.setBitThreshold((uint16_t)value);

    // Save to EEPROM
    saveTimingParametersToEEPROM();

    Log.info("Bit threshold updated to %d us", value);
    Particle.publish("config/timing", String::format("bit_threshold=%d", value), PRIVATE);

    return value;
}

// ====================================================================
// DOE (Design of Experiments) Functions
// ====================================================================

// Cloud function to start DOE experiment
int startDOE(String command) {
    if (doeActive) {
        Log.warn("DOE already running");
        return -1;
    }

    Log.info("Starting DOE experiment for 1-wire timing optimization");
    doeActive = true;
    doeStatus = "starting";
    doeProgress = 0;
    doeStartTime = Time.now();

    // Reset best result tracking
    bestResult.successCount = 0;
    bestResult.failCount = 0;
    bestResult.successRate = 0.0;

    // Clear previous phase summaries
    doePhase1Summary = "{}";
    doePhase2Summary = "{}";
    doePhase3Summary = "{}";
    doePhase4Summary = "{}";

    publishDOEStatus("DOE experiment started");

    return 1;
}

// Cloud function to stop DOE experiment
int stopDOE(String command) {
    if (!doeActive) {
        Log.warn("DOE not running");
        return -1;
    }

    Log.info("Stopping DOE experiment");
    doeActive = false;
    doeStatus = "stopped";

    // Restore default timing parameters
    dht.resetTimingDefaults();

    publishDOEStatus("DOE experiment stopped by user");

    return 1;
}

// Main DOE experiment - runs through all parameter combinations
void runDOEExperiment() {
    Log.info("=== DOE Experiment Running ===");
    doeStatus = "running";

    // Calculate total number of tests
    int startSignalSteps = (doeConfig.startSignalMax - doeConfig.startSignalMin) / doeConfig.startSignalStep + 1;
    int responseTimeoutSteps = (doeConfig.responseTimeoutMax - doeConfig.responseTimeoutMin) / doeConfig.responseTimeoutStep + 1;
    int bitTimeoutSteps = (doeConfig.bitTimeoutMax - doeConfig.bitTimeoutMin) / doeConfig.bitTimeoutStep + 1;
    int bitThresholdSteps = (doeConfig.bitThresholdMax - doeConfig.bitThresholdMin) / doeConfig.bitThresholdStep + 1;

    int totalTests = startSignalSteps * responseTimeoutSteps * bitTimeoutSteps * bitThresholdSteps;
    int testsCompleted = 0;

    Log.info("DOE Configuration:");
    Log.info("  Start Signal: %d-%d us (step %d) = %d tests",
             doeConfig.startSignalMin, doeConfig.startSignalMax, doeConfig.startSignalStep, startSignalSteps);
    Log.info("  Response Timeout: %d-%d us (step %d) = %d tests",
             doeConfig.responseTimeoutMin, doeConfig.responseTimeoutMax, doeConfig.responseTimeoutStep, responseTimeoutSteps);
    Log.info("  Bit Timeout: %d-%d us (step %d) = %d tests",
             doeConfig.bitTimeoutMin, doeConfig.bitTimeoutMax, doeConfig.bitTimeoutStep, bitTimeoutSteps);
    Log.info("  Bit Threshold: %d-%d us (step %d) = %d tests",
             doeConfig.bitThresholdMin, doeConfig.bitThresholdMax, doeConfig.bitThresholdStep, bitThresholdSteps);
    Log.info("  Tests per config: %d", doeConfig.testsPerConfig);
    Log.info("  Total configurations: %d", totalTests);

    // Test each parameter independently (one-factor-at-a-time design)
    // This is more manageable than full factorial design

    // Phase 1: Test Start Signal parameter
    doeStatus = "testing_start_signal";
    Log.info("--- Phase 1: Testing Start Signal Parameter ---");
    publishDOEStatus("Phase 1/4: Testing start signal timing");

    uint16_t bestStartSignal = 1600; // DOE-optimized default
    float bestStartSignalRate = 0.0;

    // Collect all results for this phase
    DOEResult startSignalResults[20]; // Max 20 results (generous for 800-2000 step 100)
    int startSignalResultCount = 0;

    for (uint16_t startSignal = doeConfig.startSignalMin;
         startSignal <= doeConfig.startSignalMax;
         startSignal += doeConfig.startSignalStep) {

        DOEResult result = testParameterSet(startSignal, 240, 115, 46);

        // Store result for summary
        if (startSignalResultCount < 20) {
            startSignalResults[startSignalResultCount++] = result;
        }

        if (result.successRate > bestStartSignalRate) {
            bestStartSignalRate = result.successRate;
            bestStartSignal = result.startSignal;
            bestResult = result;
            publishDOEResult(result, true);
        } else {
            publishDOEResult(result, false);
        }

        testsCompleted++;
        doeProgress = (testsCompleted * 100) / (startSignalSteps + responseTimeoutSteps + bitTimeoutSteps + bitThresholdSteps);

        // Allow cloud communication
        Particle.process();
        delay(100);

        // Check if stopped
        if (!doeActive) {
            Log.info("DOE stopped during start signal testing");
            return;
        }
    }

    Log.info("Best start signal: %d us (%.1f%% success)", bestStartSignal, bestStartSignalRate);

    // Publish phase 1 summary statistics
    publishPhaseSummary("start_signal", startSignalResults, startSignalResultCount);

    // Phase 2: Test Response Timeout parameter (using best start signal)
    doeStatus = "testing_response_timeout";
    Log.info("--- Phase 2: Testing Response Timeout Parameter ---");
    publishDOEStatus("Phase 2/4: Testing response timeout");

    uint16_t bestResponseTimeout = 240;
    float bestResponseTimeoutRate = 0.0;

    // Collect all results for this phase
    DOEResult responseTimeoutResults[20]; // Max 20 results
    int responseTimeoutResultCount = 0;

    for (uint16_t responseTimeout = doeConfig.responseTimeoutMin;
         responseTimeout <= doeConfig.responseTimeoutMax;
         responseTimeout += doeConfig.responseTimeoutStep) {

        DOEResult result = testParameterSet(bestStartSignal, responseTimeout, 115, 46);

        // Store result for summary
        if (responseTimeoutResultCount < 20) {
            responseTimeoutResults[responseTimeoutResultCount++] = result;
        }

        if (result.successRate > bestResponseTimeoutRate) {
            bestResponseTimeoutRate = result.successRate;
            bestResponseTimeout = result.responseTimeout;
            bestResult = result;
            publishDOEResult(result, true);
        } else {
            publishDOEResult(result, false);
        }

        testsCompleted++;
        doeProgress = (testsCompleted * 100) / (startSignalSteps + responseTimeoutSteps + bitTimeoutSteps + bitThresholdSteps);

        Particle.process();
        delay(100);

        if (!doeActive) {
            Log.info("DOE stopped during response timeout testing");
            return;
        }
    }

    Log.info("Best response timeout: %d us (%.1f%% success)", bestResponseTimeout, bestResponseTimeoutRate);

    // Publish phase 2 summary statistics
    publishPhaseSummary("response_timeout", responseTimeoutResults, responseTimeoutResultCount);

    // Phase 3: Test Bit Timeout parameter
    doeStatus = "testing_bit_timeout";
    Log.info("--- Phase 3: Testing Bit Timeout Parameter ---");
    publishDOEStatus("Phase 3/4: Testing bit timeout");

    uint16_t bestBitTimeout = 115;
    float bestBitTimeoutRate = 0.0;

    // Collect all results for this phase
    DOEResult bitTimeoutResults[20]; // Max 20 results
    int bitTimeoutResultCount = 0;

    for (uint16_t bitTimeout = doeConfig.bitTimeoutMin;
         bitTimeout <= doeConfig.bitTimeoutMax;
         bitTimeout += doeConfig.bitTimeoutStep) {

        DOEResult result = testParameterSet(bestStartSignal, bestResponseTimeout, bitTimeout, 46);

        // Store result for summary
        if (bitTimeoutResultCount < 20) {
            bitTimeoutResults[bitTimeoutResultCount++] = result;
        }

        if (result.successRate > bestBitTimeoutRate) {
            bestBitTimeoutRate = result.successRate;
            bestBitTimeout = result.bitTimeout;
            bestResult = result;
            publishDOEResult(result, true);
        } else {
            publishDOEResult(result, false);
        }

        testsCompleted++;
        doeProgress = (testsCompleted * 100) / (startSignalSteps + responseTimeoutSteps + bitTimeoutSteps + bitThresholdSteps);

        Particle.process();
        delay(100);

        if (!doeActive) {
            Log.info("DOE stopped during bit timeout testing");
            return;
        }
    }

    Log.info("Best bit timeout: %d us (%.1f%% success)", bestBitTimeout, bestBitTimeoutRate);

    // Publish phase 3 summary statistics
    publishPhaseSummary("bit_timeout", bitTimeoutResults, bitTimeoutResultCount);

    // Phase 4: Test Bit Threshold parameter
    doeStatus = "testing_bit_threshold";
    Log.info("--- Phase 4: Testing Bit Threshold Parameter ---");
    publishDOEStatus("Phase 4/4: Testing bit threshold");

    uint16_t bestBitThreshold = 46;
    float bestBitThresholdRate = 0.0;

    // Collect all results for this phase
    DOEResult bitThresholdResults[20]; // Max 20 results
    int bitThresholdResultCount = 0;

    for (uint16_t bitThreshold = doeConfig.bitThresholdMin;
         bitThreshold <= doeConfig.bitThresholdMax;
         bitThreshold += doeConfig.bitThresholdStep) {

        DOEResult result = testParameterSet(bestStartSignal, bestResponseTimeout, bestBitTimeout, bitThreshold);

        // Store result for summary
        if (bitThresholdResultCount < 20) {
            bitThresholdResults[bitThresholdResultCount++] = result;
        }

        if (result.successRate > bestBitThresholdRate) {
            bestBitThresholdRate = result.successRate;
            bestBitThreshold = result.bitThreshold;
            bestResult = result;
            publishDOEResult(result, true);
        } else {
            publishDOEResult(result, false);
        }

        testsCompleted++;
        doeProgress = (testsCompleted * 100) / (startSignalSteps + responseTimeoutSteps + bitTimeoutSteps + bitThresholdSteps);

        Particle.process();
        delay(100);

        if (!doeActive) {
            Log.info("DOE stopped during bit threshold testing");
            return;
        }
    }

    Log.info("Best bit threshold: %d us (%.1f%% success)", bestBitThreshold, bestBitThresholdRate);

    // Publish phase 4 summary statistics
    publishPhaseSummary("bit_threshold", bitThresholdResults, bitThresholdResultCount);

    // DOE Complete!
    doeStatus = "complete";
    doeProgress = 100;
    doeActive = false;

    Log.info("=== DOE Experiment Complete ===");
    Log.info("Optimal Parameters:");
    Log.info("  Start Signal: %d us", bestResult.startSignal);
    Log.info("  Response Timeout: %d us", bestResult.responseTimeout);
    Log.info("  Bit Timeout: %d us", bestResult.bitTimeout);
    Log.info("  Bit Threshold: %d us", bestResult.bitThreshold);
    Log.info("  Success Rate: %.1f%% (%d/%d)",
             bestResult.successRate, bestResult.successCount,
             bestResult.successCount + bestResult.failCount);

    // Apply optimal parameters
    dht.setStartSignal(bestResult.startSignal);
    dht.setResponseTimeout(bestResult.responseTimeout);
    dht.setBitTimeout(bestResult.bitTimeout);
    dht.setBitThreshold(bestResult.bitThreshold);

    // Save optimal parameters to EEPROM for persistence
    saveTimingParametersToEEPROM();

    // Publish final results
    char finalMsg[256];
    snprintf(finalMsg, sizeof(finalMsg),
             "DOE Complete! Best: SS=%d RT=%d BT=%d BTh=%d Rate=%.1f%%",
             bestResult.startSignal, bestResult.responseTimeout,
             bestResult.bitTimeout, bestResult.bitThreshold,
             bestResult.successRate);

    publishDOEStatus(finalMsg);

    Log.info("Optimal parameters have been applied and saved to EEPROM");
}

// Test a specific parameter set
DOEResult testParameterSet(uint16_t startSignal, uint16_t responseTimeout,
                           uint16_t bitTimeout, uint16_t bitThreshold) {

    DOEResult result;
    result.startSignal = startSignal;
    result.responseTimeout = responseTimeout;
    result.bitTimeout = bitTimeout;
    result.bitThreshold = bitThreshold;
    result.successCount = 0;
    result.failCount = 0;

    // Configure DHT22 with test parameters
    dht.setStartSignal(startSignal);
    dht.setResponseTimeout(responseTimeout);
    dht.setBitTimeout(bitTimeout);
    dht.setBitThreshold(bitThreshold);

    Log.info("Testing: SS=%d RT=%d BT=%d BTh=%d",
             startSignal, responseTimeout, bitTimeout, bitThreshold);

    // Perform multiple reads
    for (int i = 0; i < doeConfig.testsPerConfig; i++) {
        float temp, humidity;
        bool success = dht.read(temp, humidity);

        if (success) {
            result.successCount++;
        } else {
            result.failCount++;
        }

        // Wait 2 seconds between reads (DHT22 requirement)
        delay(2000);
    }

    result.successRate = (result.successCount * 100.0) / (result.successCount + result.failCount);

    Log.info("Result: %d/%d success (%.1f%%)",
             result.successCount, result.successCount + result.failCount,
             result.successRate);

    return result;
}

// Publish DOE status update
void publishDOEStatus(String status) {
    if (!Particle.connected()) {
        return;
    }

    char msg[256];
    snprintf(msg, sizeof(msg), "{\"status\":\"%s\",\"progress\":%d,\"elapsed\":%lu}",
             status.c_str(), doeProgress, Time.now() - doeStartTime);

    Particle.publish("doe/status", msg, PRIVATE);
    Log.info("DOE Status: %s", msg);
}

// Publish DOE result
void publishDOEResult(DOEResult result, bool isBest) {
    if (!Particle.connected()) {
        return;
    }

    char msg[256];
    snprintf(msg, sizeof(msg),
             "{\"ss\":%d,\"rt\":%d,\"bt\":%d,\"bth\":%d,\"success\":%d,\"fail\":%d,\"rate\":%.1f,\"best\":%s}",
             result.startSignal, result.responseTimeout, result.bitTimeout, result.bitThreshold,
             result.successCount, result.failCount, result.successRate,
             isBest ? "true" : "false");

    Particle.publish("doe/result", msg, PRIVATE);

    if (isBest) {
        Log.info("NEW BEST: %s", msg);
    }
}

// Publish phase summary with statistics (for spreadsheet export)
void publishPhaseSummary(String paramName, DOEResult* results, int resultCount) {
    if (!Particle.connected() || resultCount == 0) {
        return;
    }

    // Calculate statistics
    float sumFailRate = 0.0;
    float minFailRate = 100.0;
    float maxFailRate = 0.0;
    uint16_t bestValue = 0;

    // First pass: sum and find min/max
    for (int i = 0; i < resultCount; i++) {
        float failRate = 100.0 - results[i].successRate;
        sumFailRate += failRate;

        if (failRate < minFailRate) {
            minFailRate = failRate;
            // Determine which parameter value this result represents
            if (paramName == "start_signal") {
                bestValue = results[i].startSignal;
            } else if (paramName == "response_timeout") {
                bestValue = results[i].responseTimeout;
            } else if (paramName == "bit_timeout") {
                bestValue = results[i].bitTimeout;
            } else if (paramName == "bit_threshold") {
                bestValue = results[i].bitThreshold;
            }
        }

        if (failRate > maxFailRate) {
            maxFailRate = failRate;
        }
    }

    float avgFailRate = sumFailRate / resultCount;

    // Second pass: calculate standard deviation
    float sumSquaredDiff = 0.0;
    for (int i = 0; i < resultCount; i++) {
        float failRate = 100.0 - results[i].successRate;
        float diff = failRate - avgFailRate;
        sumSquaredDiff += diff * diff;
    }
    float stdDev = sqrt(sumSquaredDiff / resultCount);

    // Calculate coefficient of variation (CV)
    // CV = (stdDev / mean) * 100%
    // Measures relative variability; lower CV = more consistent results
    float cv = (avgFailRate > 0.001) ? (stdDev / avgFailRate) * 100.0 : 0.0;

    // Calculate statistical significance (p-value approximation)
    // Using z-score: z = (best - mean) / (stdDev / sqrt(n))
    // This tests if the best result is significantly different from the mean
    float zScore = 0.0;
    float pValue = 1.0;

    if (stdDev > 0.001 && resultCount > 1) {
        // Standard error of the mean
        float sem = stdDev / sqrt(resultCount);

        // Z-score for best result vs. average
        zScore = (avgFailRate - minFailRate) / sem;

        // Convert z-score to approximate p-value using error function approximation
        // This is a simplified one-tailed test
        // For z > 0, p-value approximation using normal distribution
        if (zScore > 0) {
            // Simplified p-value approximation (good for z > 0)
            // Using: p ≈ 0.5 * erfc(z / sqrt(2))
            // Approximation: erfc(x) ≈ exp(-x²) / (x * sqrt(π))
            float x = zScore / sqrt(2.0);
            if (x > 0.1) {
                // More accurate for larger z
                pValue = 0.5 * exp(-x * x) / (x * sqrt(M_PI));
            } else {
                // For small z, use direct approximation
                pValue = 0.5 * (1.0 - 0.5 * zScore * sqrt(2.0 / M_PI));
            }

            // Clamp p-value to valid range
            if (pValue < 0.0001) pValue = 0.0001;
            if (pValue > 1.0) pValue = 1.0;
        }
    }

    // Build CSV-style data for spreadsheet export
    // Format: value,success,fail,success_rate,fail_rate (one line per test)
    String csvData = "";

    for (int i = 0; i < resultCount; i++) {
        uint16_t value;
        if (paramName == "start_signal") {
            value = results[i].startSignal;
        } else if (paramName == "response_timeout") {
            value = results[i].responseTimeout;
        } else if (paramName == "bit_timeout") {
            value = results[i].bitTimeout;
        } else {
            value = results[i].bitThreshold;
        }

        float failRate = 100.0 - results[i].successRate;

        char line[80];
        snprintf(line, sizeof(line), "%d,%d,%d,%.1f,%.1f\\n",
                 value, results[i].successCount, results[i].failCount,
                 results[i].successRate, failRate);
        csvData += line;

        // Particle publish has size limits, so we'll publish in chunks if needed
        // or just send summary statistics in a separate message
    }

    // Publish summary statistics
    char summaryMsg[622];
    snprintf(summaryMsg, sizeof(summaryMsg),
             "{\"param\":\"%s\",\"count\":%d,\"avg_fail\":%.2f,\"best_fail\":%.2f,\"worst_fail\":%.2f,"
             "\"std_dev\":%.2f,\"cv\":%.2f,\"z_score\":%.2f,\"p_value\":%.4f,\"best_value\":%d}",
             paramName.c_str(), resultCount, avgFailRate, minFailRate, maxFailRate,
             stdDev, cv, zScore, pValue, bestValue);

    Particle.publish("doe/phase_summary", summaryMsg, PRIVATE);

    // Store summary in appropriate cloud variable for later retrieval
    if (paramName == "start_signal") {
        doePhase1Summary = String(summaryMsg);
    } else if (paramName == "response_timeout") {
        doePhase2Summary = String(summaryMsg);
    } else if (paramName == "bit_timeout") {
        doePhase3Summary = String(summaryMsg);
    } else if (paramName == "bit_threshold") {
        doePhase4Summary = String(summaryMsg);
    }

    Log.info("Phase Summary [%s]:", paramName.c_str());
    Log.info("  Avg Fail: %.2f%%  Best: %.2f%%  Worst: %.2f%%", avgFailRate, minFailRate, maxFailRate);
    Log.info("  StdDev: %.2f%%  CV: %.2f%%", stdDev, cv);
    Log.info("  Z-Score: %.2f  P-Value: %.4f  Best Value: %d", zScore, pValue, bestValue);

    // Publish detailed CSV data (may be split into multiple events if needed)
    // Due to Particle event size limits (622 bytes for data), we publish in chunks
    const int MAX_CSV_SIZE = 600;
    int csvLength = csvData.length();
    int chunks = (csvLength + MAX_CSV_SIZE - 1) / MAX_CSV_SIZE;

    for (int chunk = 0; chunk < chunks; chunk++) {
        int start = chunk * MAX_CSV_SIZE;
        int end = min(start + MAX_CSV_SIZE, csvLength);
        String csvChunk = csvData.substring(start, end);

        char csvMsg[650];
        snprintf(csvMsg, sizeof(csvMsg), "{\"param\":\"%s\",\"chunk\":%d,\"total\":%d,\"data\":\"%s\"}",
                 paramName.c_str(), chunk + 1, chunks, csvChunk.c_str());

        Particle.publish("doe/phase_data", csvMsg, PRIVATE);

        // Small delay between chunks to avoid rate limiting
        if (chunk < chunks - 1) {
            delay(1000);
        }
    }
}
