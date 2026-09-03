#pragma once
#include <Arduino.h>

// Persistent node configuration (NVS namespace "nodeio", stored as one blob).
// Replaces the never-implemented rescueFlashConfig()/saveFlashConfig() stubs.
struct NodeConfig {
  uint8_t  nodeAddr;        // this node's LoRa address (1..254)
  uint8_t  masterAddr;      // accepted master address; 0 = accept any
  char     nodeName[24];
  char     nodeLoc[24];
  char     apSsid[24];      // captive-portal SoftAP SSID
  char     apPass[24];      // captive-portal SoftAP password (>= 8 chars, or "" = open)
  float    loraFreq;        // MHz
  float    loraBw;          // kHz
  uint8_t  loraSf;          // 7..12
  uint8_t  loraCr;          // 5..8  (coding rate 4/x)
  uint8_t  loraSync;        // 1-byte sync word
  int8_t   loraPwr;         // dBm, 2..22
  uint8_t  relayEnable;     // bit0..bit3 -> relay 1..4 enabled
  uint8_t  relaySafe;       // bit0..bit3 -> relay 1..4 power-on / safe level
  uint8_t  relayMode;       // 0 = latched, 1 = pulse
  uint16_t relayPulseMs;    // pulse width when relayMode == 1
  uint16_t adoptTimeoutS;   // 0 = nunca se des-adopta por silencio (recomendado; el maestro
                            // reconstruye con ROLLCALL). >0 = emite una baliza HERE a 255 tras
                            // ese silencio y re-arma; NO libera la adopcion.

  bool     adopted;         // false -> node advertises its MAC and waits for ADOPT
};

extern NodeConfig cfg;

void   configFactory();   // load built-in defaults into cfg (RAM only)
void   configLoad();      // fill cfg from NVS, or defaults if missing/invalid
bool   configSave();      // persist cfg to NVS
bool   configStored();    // true if a valid config blob exists in NVS
String nodeMac();         // this chip's 12-hex efuse MAC ("idUnico")
