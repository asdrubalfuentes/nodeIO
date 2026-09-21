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

// Escala raw->ingenieria (formula del contrato, REGISTER_MAP.md §5) + filtro EMA.
//
// Throttled a un periodo fijo (SVC_PERIOD_MS). La formula del EMA es una
// recurrencia "por llamada" (eng_filt += (x - eng_filt) * (1 - filtro/101)),
// heredada de cuando corria en el LOGO! a un ciclo de scan mas o menos fijo.
// loop() aqui NO tiene delay y corre a una tasa altisima y muy irregular
// (miles de veces/seg cuando esta libre, mucho mas lento cuando loraLoop()
// atiende SPI/radio) -- llamar la recurrencia una vez por vuelta de loop()
// hace que la constante de tiempo real del filtro dependa de esa tasa, no del
// "filtro" configurado: con el default (15) el alfa efectivo por llamada es
// ~0.85, y a miles de llamadas/seg eso converge al crudo en microsegundos ->
// el filtro no filtra casi nada, y ademas cambia de "fuerza" segun cuanto
// trabajo tenga loraLoop() en cada momento (de ahi la sensacion de lectura
// "inestable"). Al fijar el periodo, cada llamada representa un paso de
// tiempo constante y el filtro se comporta igual siempre.
static const uint32_t SVC_PERIOD_MS = 150;   // ok para nivel/caudal (usuario: "me conformo
                                              // con lectura 4x/seg o 150ms"); deja mas margen
                                              // de tiempo para el oversampling de ioReadAnalog()
static uint32_t       lastSvcMs = 0;

void channelsService() {
  uint32_t now = millis();
  if (now - lastSvcMs < SVC_PERIOD_MS) return;
  lastSvcMs = now;

  for (uint8_t i = 0; i < 4; i++) {
    const ChannelCfg &c = cfg.ch[i];
    uint16_t raw = ioReadAnalog(i);

    float y;
    float span = (float)c.rawMax - (float)c.rawMin;
    if (span == 0 || c.engMax == c.engMin) {
      y = c.engMin;                         // SCALE_BAD: sin rango valido, no inventes nada
    } else {
      y = c.engMin + ((float)raw - (float)c.rawMin) * (float)(c.engMax - c.engMin) / span;
      // clamp (REGISTER_MAP.md §5) -- un crudo fuera de [rawMin,rawMax] (ruido
      // de ADC, lazo sin conectar) no debe poder mandar la lectura ni el
      // filtro fuera del rango de ingenieria configurado.
      float lo = c.engMin < c.engMax ? c.engMin : c.engMax;
      float hi = c.engMin < c.engMax ? c.engMax : c.engMin;
      if (y < lo) y = lo; else if (y > hi) y = hi;
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
