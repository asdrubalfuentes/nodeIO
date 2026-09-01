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
//   RD                         -> reply  ST,<a1..a4>,<d1..d4>,<o1..o4>
//   WR,<r1>,<r2>,<r3>,<r4>     -> set relays (0/1, or '-' = keep), reply ST
//   WP,<idx>,<ms>              -> pulse relay idx (1..4) for ms,       reply ST
//   PING                       -> reply  PONG,<uptime_s>,<rssi_dBm>
// Errors: ERR,<FMT|CMD|DIS|RANGE>
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
