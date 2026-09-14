#include "channels.h"
#include "node_config.h"
#include "io.h"
#include <Preferences.h>

ChannelLive chLive[4];

static float emaState[4] = {0, 0, 0, 0};   // valor filtrado interno (float, previo al x100)
static bool  emaPrimed[4] = {false, false, false, false};

static const char *ACC_NS = "nodeio_acc";
static uint32_t    lastFlushMs = 0;
static const uint32_t FLUSH_EVERY_MS = 60000;   // no escribir NVS cada 1s -- desgasta flash

// factor caudal -> m3/s segun unidad (ORCHESTRATION/REGISTER_MAP.md §5 / recipe §7)
static float kFactor(uint8_t unit) {
  switch (unit) {
    case 0: return 1.0f / 1000.0f;                  // L/s
    case 1: return 1.0f / 3600.0f;                   // m3/h
    case 2: return 1.0f / 60000.0f;                  // L/min
    case 3: return 3.785411784f / 60000.0f;           // GPM
    default: return 1.0f / 1000.0f;
  }
}

void channelsInit() {
  Preferences p;
  if (p.begin(ACC_NS, true)) {
    for (uint8_t i = 0; i < 4; i++) {
      char k[8];
      snprintf(k, sizeof(k), "d%u", i); chLive[i].accDia = p.getFloat(k, 0.0f);
      snprintf(k, sizeof(k), "m%u", i); chLive[i].accMes = p.getFloat(k, 0.0f);
    }
    p.end();
  }
  lastFlushMs = millis();
}

void channelsFlush() {
  Preferences p;
  if (!p.begin(ACC_NS, false)) return;
  for (uint8_t i = 0; i < 4; i++) {
    char k[8];
    snprintf(k, sizeof(k), "d%u", i); p.putFloat(k, chLive[i].accDia);
    snprintf(k, sizeof(k), "m%u", i); p.putFloat(k, chLive[i].accMes);
  }
  p.end();
  lastFlushMs = millis();
}

// Escala raw->ingenieria (formula del contrato) + filtro EMA. Se llama seguido
// desde loop() para que el filtro se sienta suave; el escalado en si no cuesta
// nada, no hace falta limitarlo.
void channelsService() {
  for (uint8_t i = 0; i < 4; i++) {
    const ChannelCfg &c = cfg.ch[i];
    uint16_t raw = ioReadAnalog(i);

    float y;
    float span = (float)c.rawMax - (float)c.rawMin;
    if (span == 0 || c.engMax == c.engMin) {
      y = c.engMin;                         // SCALE_BAD: sin rango valido, no inventes nada
    } else {
      y = c.engMin + ((float)raw - (float)c.rawMin) * (float)(c.engMax - c.engMin) / span;
    }

    if (c.filter > 0) {
      if (!emaPrimed[i]) { emaState[i] = y; emaPrimed[i] = true; }   // arranca sin transitorio
      float a = 1.0f - (float)c.filter / 101.0f;
      emaState[i] += (y - emaState[i]) * a;
      y = emaState[i];
    }

    chLive[i].eng = (int16_t)lroundf(y);
    chLive[i].almHi = (c.almHi != CH_ALM_OFF) && (chLive[i].eng >= c.almHi);
    chLive[i].almLo = (c.almLo != CH_ALM_OFF) && (chLive[i].eng <= c.almLo);
  }
}

void channelsTick1s() {
  bool dirty = false;
  for (uint8_t i = 0; i < 4; i++) {
    const ChannelCfg &c = cfg.ch[i];
    if (!c.totDaily && !c.totMonthly) continue;
    float incrM3 = (chLive[i].eng / 100.0f) * kFactor(c.unit) * 1.0f;   // dt = 1 s
    if (c.totDaily)   { chLive[i].accDia += incrM3; dirty = true; }
    if (c.totMonthly) { chLive[i].accMes += incrM3; dirty = true; }
  }
  if (dirty && millis() - lastFlushMs >= FLUSH_EVERY_MS) channelsFlush();
}

void channelsCloseDay(uint8_t mask) {
  for (uint8_t i = 0; i < 4; i++)
    if (mask & (1 << i)) chLive[i].accDia = 0.0f;
  channelsFlush();
}

void channelsCloseMonth(uint8_t mask) {
  for (uint8_t i = 0; i < 4; i++)
    if (mask & (1 << i)) chLive[i].accMes = 0.0f;
  channelsFlush();
}
