#include <Arduino.h>
#include <heltec_unofficial.h>   // provides: radio (SX1262), display (SSD1306Wire), button (HotButton)
#include "images.h"
#include "io.h"
#include "node_config.h"
#include "portal.h"
#include "lora_proto.h"

// Remote IO Node - by Aysafi
// Responds over the LoRa link to a master: reports the 4 analog + 4 digital
// inputs and writes the 4 relay outputs. Configured through a captive portal.
#define FW_VERSION "V1.2026.004"

enum Mode { MODE_NORMAL, MODE_PORTAL, MODE_WAIT_ADOPT };
static Mode mode = MODE_NORMAL;

static uint32_t btn1DownSince = 0;   // BUTTON_1 long-press -> open portal
static uint32_t lastDrawMs    = 0;

// ---------------------------------------------------------------------------
static void splash() {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0,  "Heltec LoRa V3");
  display.drawString(0, 20, "Aysafi " FW_VERSION);
  display.drawString(0, 40, "Iniciando Nodo IO...");
  display.display();
  delay(1500);
  display.clear();
  display.drawXbm(0, 8, poweredBy_width, poweredBy_height, poweredBy);
  display.display();
  delay(1500);
}

static void enterPortal() {
  loraStandby();
  portalStart();
  mode = MODE_PORTAL;
}

// Former atiendeInterrupciones(): PRG (built-in) button toggles relay 1,
// DI edges just get logged and cleared.
static void serviceLocalInputs() {
  bool any = btn1Event || btn2Event || btnBuiltinEvent;
  for (uint8_t i = 0; i < 4; i++) any |= diEvent[i];
  if (!any) return;

  if (btnBuiltinEvent)
    ioSetRelay(0, !ioGetRelay(0), cfg.relayEnable);

  for (uint8_t i = 0; i < 4; i++) diEvent[i] = false;
  btn1Event = btn2Event = btnBuiltinEvent = false;
  Serial.println("[io] evento de entrada digital");
}

static void drawPortalScreen() {
  if (millis() - lastDrawMs < 500) return;
  lastDrawMs = millis();
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0,  "MODO CONFIG");
  display.drawString(0, 16, String("SSID: ") + cfg.apSsid);
  display.drawString(0, 30, String("IP: ") + portalIP());
  display.drawString(0, 46, "Abre el portal y guarda");
  display.display();
}

static void drawWaitAdoptScreen() {
  if (millis() - lastDrawMs < 500) return;
  lastDrawMs = millis();
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0,  "SIN ADOPTAR");
  display.drawString(0, 16, String("MAC: ") + nodeMac());
  display.drawString(0, 32, "Esperando maestro...");
  display.drawString(0, 48, "BUTTON_1 = portal");
  display.display();
}

static void drawStatusScreen() {
  if (millis() - lastDrawMs < 250) return;
  lastDrawMs = millis();

  char l[48];
  display.clear();
  display.setFont(ArialMT_Plain_10);

  snprintf(l, sizeof(l), "%s  addr:%u", cfg.nodeName, cfg.nodeAddr);
  display.drawString(0, 0, l);

  snprintf(l, sizeof(l), "cmd:%s  rssi:%d",
           loraStats.lastCmd[0] ? loraStats.lastCmd : "-", loraStats.lastRssi);
  display.drawString(0, 12, l);

  snprintf(l, sizeof(l), "AI %u %u %u %u",
           ioReadAnalog(0), ioReadAnalog(1), ioReadAnalog(2), ioReadAnalog(3));
  display.drawString(0, 26, l);

  snprintf(l, sizeof(l), "DI %u%u%u%u   RO %u%u%u%u",
           ioReadDigital(0), ioReadDigital(1), ioReadDigital(2), ioReadDigital(3),
           ioGetRelay(0), ioGetRelay(1), ioGetRelay(2), ioGetRelay(3));
  display.drawString(0, 40, l);

  snprintf(l, sizeof(l), "rx:%lu crc:%lu tx:%lu",
           (unsigned long)loraStats.rxOk, (unsigned long)loraStats.rxCrcBad,
           (unsigned long)loraStats.txCount);
  display.drawString(0, 52, l);

  display.display();
}

// ---------------------------------------------------------------------------
void setup() {
  heltec_setup();                       // Serial + OLED (display.init / flip / contrast)
  Serial.println("\nRemote IO Node, by Aysafi " FW_VERSION);

  configLoad();
  ioInit(cfg.relayEnable, cfg.relaySafe);
  splash();

  if (!loraBegin()) {
    Serial.println("[lora] fallo de init -> portal");
    enterPortal();
  } else {
    mode = cfg.adopted ? MODE_NORMAL : MODE_WAIT_ADOPT;
    Serial.printf("[cfg] %s\n", cfg.adopted ? "adoptado" : "sin adoptar (esperando ADOPT)");
  }
}

void loop() {
  heltec_loop();                        // updates HotButton `button`

  // BUTTON_1 held ~3 s -> stop LoRa and raise the captive portal
  if (mode != MODE_PORTAL) {
    if (digitalRead(PIN_BUTTON_1) == LOW) {
      if (btn1DownSince == 0) btn1DownSince = millis();
      else if (millis() - btn1DownSince > 3000) enterPortal();
    } else {
      btn1DownSince = 0;
    }
  }

  if (mode == MODE_PORTAL) {
    portalLoop();
    drawPortalScreen();
    return;
  }

  if (mode == MODE_WAIT_ADOPT) {
    loraLoop();                         // handles DISC / ADOPT (reboots on adopt)
    drawWaitAdoptScreen();
    return;
  }

  loraLoop();
  ioServicePulses(cfg.relaySafe);
  serviceLocalInputs();
  drawStatusScreen();
}
