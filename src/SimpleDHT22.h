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

    // Timing parameter setters for DOE experiments
    void setStartSignal(uint16_t us) { _startSignal = us; }
    void setResponseTimeout(uint16_t us) { _responseTimeout = us; }
    void setBitTimeout(uint16_t us) { _bitTimeout = us; }
    void setBitThreshold(uint16_t us) { _bitThreshold = us; }

    // Timing parameter getters
    uint16_t getStartSignal() { return _startSignal; }
    uint16_t getResponseTimeout() { return _responseTimeout; }
    uint16_t getBitTimeout() { return _bitTimeout; }
    uint16_t getBitThreshold() { return _bitThreshold; }

    // Reset to default timing parameters
    void resetTimingDefaults();

private:
    pin_t _pin;
    float _lastTemperature;
    float _lastHumidity;
    bool _lastReadSuccess;

    // Timing parameters for DHT22 (in microseconds) - now configurable for DOE
    uint16_t _startSignal;      // 1-10ms start signal (default 1.1ms)
    uint16_t _responseTimeout;  // Sensor response timeout (default 200us)
    uint16_t _bitTimeout;       // Bit signal timeout (default 100us)
    uint16_t _bitThreshold;     // Bit decision threshold (default 50us)

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
