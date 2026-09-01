#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Pin map - Heltec WiFi LoRa 32 V3 + Aysafi "Remote IO" carrier board.
// (moved here from main.cpp so every module shares one definition)
// ---------------------------------------------------------------------------
#define PIN_LED             GPIO_NUM_35
#define PIN_ADC_CH1         GPIO_NUM_2
#define PIN_ADC_CH2         GPIO_NUM_3
#define PIN_ADC_CH3         GPIO_NUM_4
#define PIN_ADC_CH4         GPIO_NUM_5
#define PIN_DI_1            GPIO_NUM_38
#define PIN_DI_2            GPIO_NUM_39
#define PIN_DI_3            GPIO_NUM_40
#define PIN_DI_4            GPIO_NUM_41
#define PIN_RELAY_1         GPIO_NUM_33
#define PIN_RELAY_2         GPIO_NUM_34
#define PIN_RELAY_3         GPIO_NUM_45   // ESP32-S3 strapping pin (VDD_SPI) - see plan
#define PIN_RELAY_4         GPIO_NUM_46   // ESP32-S3 strapping pin (boot)    - see plan
#define PIN_PWR_MGM         GPIO_NUM_36   // = Heltec VEXT, LOW enables external power rail
#define PIN_BUTTON_1        GPIO_NUM_47
#define PIN_BUTTON_2        GPIO_NUM_48
#define PIN_BUTTON_BUILTIN  GPIO_NUM_0

extern const uint8_t ADC_PINS[4];
extern const uint8_t DI_PINS[4];
extern const uint8_t RELAY_PINS[4];

// Edge events, set from a shared ISR (kept for compatibility with the
// original firmware behaviour: PRG button toggles relay 1, DI edges logged).
extern volatile bool diEvent[4];
extern volatile bool btn1Event;
extern volatile bool btn2Event;
extern volatile bool btnBuiltinEvent;

// enableMask / safeMask: bit0..bit3 -> relay 1..4
void     ioInit(uint8_t enableMask, uint8_t safeMask);

uint16_t ioReadAnalog(uint8_t ch);   // ch 0..3  -> raw ADC 0..4095
uint8_t  ioReadDigital(uint8_t ch);  // ch 0..3  -> raw level 0/1
uint8_t  ioGetRelay(uint8_t idx);    // idx 0..3 -> 0/1 (shadow state)

// Returns false when the relay is disabled in enableMask (nothing driven).
bool     ioSetRelay(uint8_t idx, uint8_t on, uint8_t enableMask);
bool     ioPulseRelay(uint8_t idx, uint16_t ms, uint8_t enableMask, uint8_t safeMask);

// Call from loop(): reverts pulsed relays to their safe level when time is up.
void     ioServicePulses(uint8_t safeMask);
