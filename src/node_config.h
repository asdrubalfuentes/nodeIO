#pragma once
#include <Arduino.h>

// Calibracion/config de un canal analogico (AI1..AI4). Solo AI1 (nivel) y AI2
// (caudal) tienen sentido hoy, pero se deja generico para las 4 entradas.
// Ver ORCHESTRATION/REGISTER_MAP.md §5 (formula) y §7 (factor k del totalizador).
struct ChannelCfg {
  char     name[16];      // etiqueta libre ("Nivel pozo", "Caudal salida"...)
  uint16_t rawMin;        // cuentas ADC en el extremo bajo del lazo 4-20mA
  uint16_t rawMax;        // cuentas ADC en el extremo alto
  int16_t  engMin;        // valor de ingenieria x100 en rawMin
  int16_t  engMax;        // valor de ingenieria x100 en rawMax
  uint8_t  unit;          // nivel: 0=% 1=m 2=cm 3=mca | caudal: 0=L/s 1=m3/h 2=L/min 3=GPM
  uint8_t  filter;        // EMA 0..100 (0 = sin filtro), misma formula que el contrato
  bool     totDaily;      // totalizar acumulado del dia (solo tiene sentido en caudal)
  bool     totMonthly;    // totalizar acumulado del mes en curso
  int16_t  almHi;         // limite alto x100 (caudal); 0x7FFF = deshabilitado
  int16_t  almLo;         // limite bajo x100 (nivel);  0x7FFF = deshabilitado
};
#define CH_ALM_OFF  ((int16_t)0x7FFF)

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

  // --- WiFi de mantenimiento (solo para OTA por comando; ver lora_proto OTA) ---
  char     otaSsid[33];     // "" = OTA remota deshabilitada
  char     otaPass[65];
  // IP fija (otaIp vacio = DHCP). Con DHCP el DNS lo entrega el router solo;
  // con IP fija hay que darlo explicito o WiFi.config() lo deja en 0.0.0.0 y
  // el nodo queda sin DNS (rompe la resolucion de github.com para OTA -- ver
  // el mismo bug ya corregido en nodeIO_master v1.5.3). otaDns1 vacio con
  // otaIp fijado usa otaGw como DNS (la mayoria de los routers hacen de
  // proxy DNS); otaDns2 es respaldo opcional.
  char     otaIp[16];
  char     otaGw[16];
  char     otaMask[16];
  char     otaDns1[16];
  char     otaDns2[16];

  // --- Escalado/totalizacion/alarma por canal analogico + tags de DI/DO ---
  // (cambio de rumbo 2026-09: escalar y totalizar en el nodo, no en el PLC)
  ChannelCfg ch[4];
  char       diName[4][16];
  char       doName[4][16];
};

extern NodeConfig cfg;

void   configFactory();   // load built-in defaults into cfg (RAM only)
void   configLoad();      // fill cfg from NVS, or defaults if missing/invalid
bool   configSave();      // persist cfg to NVS
bool   configStored();    // true if a valid config blob exists in NVS
String nodeMac();         // this chip's 12-hex efuse MAC ("idUnico")

// Bandera "arrancar en modo OTA" (clave suelta en NVS, independiente del blob:
// sobrevive a un cambio de CFG_MAGIC). La pone el comando LoRa 'OTA'.
void   otaSetPending(bool v);
bool   otaTakePending();  // lee y LIMPIA la bandera (para no reintentar en bucle)
