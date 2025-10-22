/*
 * SimpleDHT22 - Interrupt-based DHT22 library for Particle Gen3 devices
 * Optimized for nRF52840 (Boron, Argon, Xenon)
 * Uses hardware timer for precise microsecond timing
 */

#ifndef SIMPLE_DHT22_H
#define SIMPLE_DHT22_H

#include "Particle.h"
#include "nrf52840.h"

class SimpleDHT22 {
public:
    SimpleDHT22(pin_t pin);

    // Initialize the sensor
    void begin();

    // Read temperature and humidity (blocking call, takes ~5ms)
    bool read(float &temperature, float &humidity);

    // Get last successful readings
    float getTemperature() { return _lastTemperature; }
    float getHumidity() { return _lastHumidity; }
    bool isValid() { return _lastReadSuccess; }

private:
    pin_t _pin;
    float _lastTemperature;
    float _lastHumidity;
    bool _lastReadSuccess;

    // Timing constants for DHT22 (in microseconds) - per datasheet
    static const uint16_t DHT22_START_SIGNAL = 1100;      // 1-10ms start signal (using 1.1ms)
    static const uint16_t DHT22_RESPONSE_TIMEOUT = 200;   // Sensor response 80us low + 80us high
    static const uint16_t DHT22_BIT_TIMEOUT = 100;        // Bit signal timeout
    static const uint16_t DHT22_BIT_THRESHOLD = 50;       // Bit decision: >50us = 1, <50us = 0

    // Hardware timer functions (nRF52840 TIMER1) for precise timing
    void initHardwareTimer();
    void startHardwareTimer();
    inline uint32_t getHardwareMicros();
    void delayHardwareMicros(uint32_t us);
    void stopHardwareTimer();

    // Read raw data from sensor
    bool readRawData(uint8_t data[5]);

    // Wait for pin state change with timeout (hardware timer version)
    inline bool waitForState(uint8_t state, uint16_t timeout);
};

#endif // SIMPLE_DHT22_H
