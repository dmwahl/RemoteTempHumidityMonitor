/*
 * SimpleDHT22 - Interrupt-based DHT22 library for Particle Gen3 devices
 * Uses nRF52840 hardware timer (TIMER1) for precise microsecond timing
 */

#include "SimpleDHT22.h"

SimpleDHT22::SimpleDHT22(pin_t pin) : _pin(pin), _lastTemperature(0), _lastHumidity(0), _lastReadSuccess(false) {
    // Initialize timing parameters to defaults
    resetTimingDefaults();
}

void SimpleDHT22::resetTimingDefaults() {
    _startSignal = 1100;      // 1.1ms start signal (per DHT22 datasheet: 1-10ms)
    _responseTimeout = 200;   // 200us timeout for sensor response (conservative)
    _bitTimeout = 100;        // 100us timeout for bit signals (conservative)
    _bitThreshold = 50;       // 50us threshold for bit decision (per DHT22 datasheet)
}

void SimpleDHT22::begin() {
    pinMode(_pin, INPUT);  // No internal pull-up, use external resistor only
    Log.info("DHT22 Init: Using hardware timer + Particle GPIO on pin %d", _pin);
    delay(1000);    // DHT22 requires 1 second to stabilize after power-on
}

// Initialize hardware timer (TIMER1) for microsecond precision
// TIMER1 runs at 16MHz, we set prescaler to get 1MHz (1 tick = 1us)
void SimpleDHT22::initHardwareTimer() {
    NRF_TIMER1->MODE = TIMER_MODE_MODE_Timer;           // Timer mode
    NRF_TIMER1->BITMODE = TIMER_BITMODE_BITMODE_32Bit;  // 32-bit timer
    NRF_TIMER1->PRESCALER = 4;                          // Prescaler 4 = 16MHz / 2^4 = 1MHz (1us ticks)
}

// Start the hardware timer
void SimpleDHT22::startHardwareTimer() {
    NRF_TIMER1->TASKS_CLEAR = 1;  // Clear the timer
    NRF_TIMER1->TASKS_START = 1;  // Start the timer
}

// Get current microseconds from hardware timer
inline uint32_t SimpleDHT22::getHardwareMicros() {
    NRF_TIMER1->TASKS_CAPTURE[0] = 1;  // Capture current timer value
    return NRF_TIMER1->CC[0];           // Return captured value (in microseconds)
}

// Hardware-based microsecond delay
void SimpleDHT22::delayHardwareMicros(uint32_t us) {
    uint32_t start = getHardwareMicros();
    while (getHardwareMicros() - start < us) {
        // Tight loop - hardware timer ensures precision
    }
}

// Stop the hardware timer
void SimpleDHT22::stopHardwareTimer() {
    NRF_TIMER1->TASKS_STOP = 1;   // Stop the timer
}

bool SimpleDHT22::read(float &temperature, float &humidity) {
    uint8_t data[5] = {0, 0, 0, 0, 0};
    bool success = false;
    int attempts = 0;
    const int maxAttempts = 2;  // Try twice before giving up

    while (!success && attempts < maxAttempts) {
        attempts++;

        // Read raw data from sensor
        if (!readRawData(data)) {
            if (attempts < maxAttempts) {
                Log.warn("DHT22 read attempt %d failed, retrying...", attempts);
                delay(100);  // Short delay before retry
                continue;
            }
            _lastReadSuccess = false;
            return false;
        }

        // Verify checksum
        uint8_t checksum = data[0] + data[1] + data[2] + data[3];
        if (checksum != data[4]) {
            if (attempts < maxAttempts) {
                Log.warn("DHT22 checksum failed (attempt %d), retrying...", attempts);
                delay(100);  // Short delay before retry
                continue;
            }
            _lastReadSuccess = false;
            return false;
        }

        // Calculate humidity (first 2 bytes)
        uint16_t rawHumidity = ((uint16_t)data[0] << 8) | data[1];
        humidity = rawHumidity / 10.0;

        // Calculate temperature (next 2 bytes)
        uint16_t rawTemperature = ((uint16_t)(data[2] & 0x7F) << 8) | data[3];
        temperature = rawTemperature / 10.0;

        // Check if temperature is negative
        if (data[2] & 0x80) {
            temperature = -temperature;
        }

        // Validate ranges
        if (humidity < 0 || humidity > 100 || temperature < -40 || temperature > 80) {
            if (attempts < maxAttempts) {
                Log.warn("DHT22 values out of range (attempt %d), retrying...", attempts);
                delay(100);  // Short delay before retry
                continue;
            }
            _lastReadSuccess = false;
            return false;
        }

        // If we got here, read was successful
        success = true;
    }

    if (success && attempts > 1) {
        Log.info("DHT22 read succeeded on attempt %d", attempts);
    }

    // Store successful reading
    _lastTemperature = temperature;
    _lastHumidity = humidity;
    _lastReadSuccess = true;

    return true;
}

bool SimpleDHT22::readRawData(uint8_t data[5]) {
    // Ensure minimum 2 second interval between reads (DHT22 requirement)
    static uint32_t lastReadTime = 0;
    uint32_t now = millis();
    if (now - lastReadTime < 2000) {
        delay(2000 - (now - lastReadTime));
    }
    lastReadTime = millis();

    // Initialize and start hardware timer for precise timing
    initHardwareTimer();
    startHardwareTimer();

    // Disable interrupts for precise timing
    noInterrupts();

    // Step 1: Send start signal (pull low for 1-10ms, we use 1.1ms)
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, LOW);
    delayHardwareMicros(_startSignal);  // Hardware timer delay

    // Step 2: Release line (pull high briefly, then let pull-up take over)
    digitalWrite(_pin, HIGH);
    delayHardwareMicros(30);  // 20-40us per datasheet
    pinMode(_pin, INPUT);     // No internal pull-up, rely on external resistor
    delayHardwareMicros(10);  // Small settling time

    // Step 3: Wait for sensor response - DHT pulls low for ~80us
    if (!waitForState(LOW, _responseTimeout)) {
        interrupts();
        stopHardwareTimer();
        return false;
    }

    // Step 4: Wait for sensor to pull high for ~80us
    if (!waitForState(HIGH, _responseTimeout)) {
        interrupts();
        stopHardwareTimer();
        return false;
    }

    // Step 5: Wait for sensor to pull low (ready to send data)
    if (!waitForState(LOW, _responseTimeout)) {
        interrupts();
        stopHardwareTimer();
        return false;
    }

    // Step 6: Read 40 bits of data (5 bytes)
    for (int i = 0; i < 5; i++) {
        for (int j = 7; j >= 0; j--) {
            // Wait for low-to-high transition (start of bit)
            if (!waitForState(HIGH, _bitTimeout)) {
                interrupts();
                stopHardwareTimer();
                return false;
            }

            // Measure high pulse duration to determine bit value using hardware timer
            // Bit 0: ~26-28us high, Bit 1: ~70us high
            uint32_t highStart = getHardwareMicros();
            if (!waitForState(LOW, _bitTimeout)) {
                interrupts();
                stopHardwareTimer();
                return false;
            }
            uint32_t highDuration = getHardwareMicros() - highStart;

            // Threshold: >50us = 1, <50us = 0 (per DHT22 datasheet)
            if (highDuration > _bitThreshold) {
                data[i] |= (1 << j);
            }
        }
    }

    // Re-enable interrupts and stop hardware timer
    interrupts();
    stopHardwareTimer();

    return true;
}

inline bool SimpleDHT22::waitForState(uint8_t state, uint16_t timeout) {
    uint32_t startTime = getHardwareMicros();

    while (digitalRead(_pin) != state) {
        if (getHardwareMicros() - startTime > timeout) {
            return false;
        }
    }
    return true;
}
