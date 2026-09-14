#pragma once
#include <Arduino.h>

// Motor de escalado + filtro EMA + totalizador + alarmas de umbral, por canal
// analogico (ver node_config.h: ChannelCfg). Formula identica a la que
// documentaba ORCHESTRATION/PLC_LOGIC.md §3/§4 -- antes vivia en el LOGO!,
// ahora vive en el nodo (cambio de rumbo 2026-09).
//
// El nodo no tiene reloj confiable: los acumulados NUNCA se cierran solos.
// El cierre de dia/mes lo dispara un comando LoRa del gateway (que si tiene
// hora real), via channelsCloseDay()/channelsCloseMonth().

struct ChannelLive {
  int16_t eng    = 0;      // valor escalado x100, ya filtrado (EMA)
  float   accDia = 0.0f;   // acumulado del dia en curso (m3) -- solo si totDaily
  float   accMes = 0.0f;   // acumulado del mes en curso (m3) -- solo si totMonthly
  bool    almHi  = false;
  bool    almLo  = false;
};

extern ChannelLive chLive[4];

void channelsInit();                          // carga acumulados desde NVS
void channelsService();                       // llamar en cada loop(): escala + filtra
void channelsTick1s();                        // llamar 1x/segundo: integra el totalizador
void channelsCloseDay(uint8_t mask = 0x0F);    // bit i = cierra el dia del canal i
void channelsCloseMonth(uint8_t mask = 0x0F);  // bit i = cierra el mes del canal i
void channelsFlush();                          // fuerza guardado de acumulados a NVS
