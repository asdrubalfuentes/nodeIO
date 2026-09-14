#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// LoRa request/response protocol (node / responder side).
//
// Wire frame:  "<dst>,<src>,<seq>,<cmd>[,<arg>...]" + CRC32(4 bytes LE) + '\n'
//   dst/src : 1-byte decimal addresses, 255 = broadcast
//   seq     : rolling id from the master, echoed back in the reply
//
// Master -> node commands
//   RD                         -> reply  ST,<...>  (ver formato abajo)
//   WR,<r1>,<r2>,<r3>,<r4>     -> set relays (0/1, or '-' = keep), reply ST
//   WP,<idx>,<ms>              -> pulse relay idx (1..4) for ms,       reply ST
//   CD[,<mask>]                -> cierra el acumulado del DIA (mask, bit i = canal i;
//                                  sin mask = 0x0F = todos). Lo manda el gateway/HMI,
//                                  que si tienen hora real -- el nodo no cierra solo.
//   CM[,<mask>]                -> idem, cierre de MES.
//   PING                       -> reply  PONG,<uptime_s>,<rssi_dBm>
// Errors: ERR,<FMT|CMD|DIS|RANGE>
//
// ST,<ai1>,<ai2>,<ai3>,<ai4>,<di1>,<di2>,<di3>,<di4>,<ro1..4|x>,
//    <eng0>,<eng1>,<accDia0>,<accMes0>,<accDia1>,<accMes1>,<almBits>
//   ai1..4   : crudo ADC 0..4095 (canal 2/3 sin escalar, quedan reservados)
//   eng0/1   : nivel/caudal ya escalados x100, filtrados (EMA) -- ver channels.h
//   accDia/Mes: acumulado del dia/mes en curso, m3 x1000 (entero)
//   almBits  : bit0 nivel.almLo · bit1 nivel.almHi · bit2 caudal.almLo · bit3 caudal.almHi
// ---------------------------------------------------------------------------

struct LoraStats {
  uint32_t rxOk;
  uint32_t rxCrcBad;
  uint32_t rxNotForUs;
  uint32_t txCount;
  int16_t  lastRssi;      // dBm of the last accepted frame
  char     lastCmd[12];   // last command handled (for the OLED)
  uint32_t lastRxMs;      // millis() of the last accepted frame
};
extern LoraStats loraStats;

bool loraBegin();     // configure SX1262 from cfg and enter continuous RX
void loraStandby();   // stop RX (used before starting the captive portal)
void loraLoop();      // service a pending RX packet and answer it
