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

// System mode - Use AUTOMATIC for reliable cloud connection
SYSTEM_MODE(AUTOMATIC);

// DHT sensor object - using custom interrupt-based library
SimpleDHT22 dht(DHTPIN);

// Global variables
unsigned long measurementInterval = 10000; // Default 10 seconds in milliseconds
unsigned long lastMeasurement = 0;
float lastTemperature = 0.0;
float lastHumidity = 0.0;
bool firstRun = true;

// Cloud variables (read-only from cloud)
String lastReading = "{}";
int currentInterval = 10; // In seconds for easier cloud reading

// Function prototypes
void takeMeasurement();
void publishReading(float temperature, float humidity);
String createJsonPayload(float temperature, float humidity);
int setInterval(String command);
int forceReading(String command);

void setup() {
    // Start serial for debugging
    Serial.begin(9600);

    // Register cloud functions (must be done in setup before cloud connects)
    Particle.function("setInterval", setInterval);
    Particle.function("forceReading", forceReading);

    // Register cloud variables
    Particle.variable("lastReading", lastReading);
    Particle.variable("intervalSec", currentInterval);

    // Initialize DHT sensor
    dht.begin();

    // Wait for sensor to stabilize
    delay(2000);

    Serial.println("Remote Temp/Humidity Monitor Initialized");
    Serial.printlnf("Measurement interval: %d seconds", currentInterval);
    Serial.println("Using custom interrupt-based DHT22 library");
    Serial.println("DHT22 on D3 - External 10k pullup REQUIRED (internal disabled)");

    // Wait for cloud connection before first reading
    waitFor(Particle.connected, 60000);

    if (Particle.connected()) {
        Serial.println("Cloud connected!");
    } else {
        Serial.println("WARNING: Cloud not connected");
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

    // Allow system to process cloud events
    delay(100);
}

void takeMeasurement() {
    Serial.println("\n--- Taking Measurement ---");

    float temperature = 0;
    float humidity = 0;

    // Read from DHT sensor using custom library
    bool success = dht.read(temperature, humidity);

    // Debug output
    Serial.printlnf("Raw values - Temp: %.2f°C, Humidity: %.2f%%, Success: %s",
                    temperature, humidity, success ? "YES" : "NO");

    // Check if reading was successful
    if (!success) {
        Serial.println("ERROR: Failed to read from DHT sensor!");
        Serial.println("Troubleshooting:");
        Serial.println("  - Add 10kΩ resistor between DATA (D3) and 3V3");
        Serial.println("  - Check wiring: DHT22 DATA -> D3");
        Serial.println("  - Verify DHT22 has power (3.3V)");
        Serial.println("  - Verify DHT22 GND is connected");
        Serial.println("  - Ensure proper DHT22 sensor (not DHT11)");
        Serial.println("  - Try different pin (D2, D4, D5)");

        // Publish error status (only if connected)
        if (Particle.connected()) {
            Particle.publish("sensor/error", "DHT22 read failed", PRIVATE);
        }
        return;
    }

    // Store last values
    lastTemperature = temperature;
    lastHumidity = humidity;

    // Create and store JSON reading
    lastReading = createJsonPayload(temperature, humidity);

    // Print to serial
    Serial.println("✓ Reading successful!");
    Serial.printlnf("  Temperature: %.2f°C (%.2f°F)", temperature, temperature * 9.0 / 5.0 + 32.0);
    Serial.printlnf("  Humidity: %.2f%%", humidity);
    Serial.printlnf("  Dew Point: %.2f°C", temperature - ((100 - humidity) / 5.0));
    Serial.println("JSON: " + lastReading);

    // Publish to cloud
    publishReading(temperature, humidity);
}

void publishReading(float temperature, float humidity) {
    // Check cloud connection before publishing
    if (!Particle.connected()) {
        Serial.println("WARNING: Not connected to cloud, skipping publish");
        return;
    }

    String jsonData = createJsonPayload(temperature, humidity);

    // Publish as event
    bool success = Particle.publish("sensor/reading", jsonData, PRIVATE);

    if (success) {
        Serial.println("Reading published successfully");
    } else {
        Serial.println("ERROR: Failed to publish reading");
    }
}

String createJsonPayload(float temperature, float humidity) {
    // Create InfluxDB-compatible JSON format using JSONBufferWriter
    // Format: {"measurement":"environment","tags":{"location":"default","device":"boron"},"fields":{"temperature":23.5,"humidity":45.2},"timestamp":1234567890}

    char buffer[256];
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

    return String(writer.buffer());
}

// Cloud function to set measurement interval (in seconds)
int setInterval(String command) {
    int newInterval = command.toInt();

    // Validate interval (minimum 10 seconds, maximum 1 hour)
    if (newInterval < 10 || newInterval > 3600) {
        Serial.printlnf("ERROR: Invalid interval %d seconds (must be 10-3600)", newInterval);
        return -1;
    }

    measurementInterval = newInterval * 1000; // Convert to milliseconds
    currentInterval = newInterval;

    Serial.printlnf("Measurement interval updated to %d seconds", newInterval);
    Particle.publish("config/interval", String(newInterval), PRIVATE);

    return newInterval;
}

// Cloud function to force an immediate reading
int forceReading(String command) {
    Serial.println("Force reading requested from cloud");
    takeMeasurement();
    return 1;
}
