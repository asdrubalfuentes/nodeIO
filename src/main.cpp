#include <Arduino.h>
#include <heltec_unofficial.h>   // provides: radio (SX1262), display (SSD1306Wire), button (HotButton)
#include <WiFi.h>
#include "images.h"
#include "io.h"
#include "channels.h"
#include "node_config.h"
#include "portal.h"
#include "lora_proto.h"
#include "ota_update.h"

// Remote IO Node - by Aysafi
// Responds over the LoRa link to a master: reports the 4 analog + 4 digital
// inputs and writes the 4 relay outputs. Configured through a captive portal.
#define FW_VERSION "V1.2026.007"

// Version semver (X.Y.Z) para el canal OTA (GitHub Releases). El CI la
// sobreescribe desde el tag; sin CI vale este literal.
//   1.4.0  escalado/filtro/totalizador/alarma en el nodo (cambio de rumbo v3)
//   1.4.1  comando serial "buscar actualizacion" (alternativa al F2 mantenido)
#ifdef FW_VERSION_OVERRIDE
#define FW_SEMVER FW_VERSION_OVERRIDE
#else
#define FW_SEMVER "1.4.1"
#endif

enum Mode { MODE_NORMAL, MODE_PORTAL, MODE_WAIT_ADOPT };
static Mode mode = MODE_NORMAL;

static uint32_t btn1DownSince = 0;   // BUTTON_1 long-press -> open portal
static uint32_t btn2DownSince = 0;   // BUTTON_2 (F2): toque corto = ciclar pantalla,
static bool     btn2OtaFired  = false;  //   mantenido 4-5s = forzar chequeo OTA
static uint8_t  diagScreen    = 0;   // 0 = pantalla normal, 1 = nivel, 2 = caudal
static uint32_t lastDrawMs    = 0;
static uint32_t last1sTickMs  = 0;

static const char *UNIT_LEVEL_S[4] = { "%", "m", "cm", "mca" };
static const char *UNIT_FLOW_S[4]  = { "L/s", "m3/h", "L/min", "GPM" };

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

// ---- modo OTA (lo dispara el comando LoRa 'OTA') -------------------------
static void otaOled(ota::Phase ph, int pct, const char *d) {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "MODO OTA " FW_SEMVER);
  const char *m = ""; char buf[24];
  switch (ph) {
    case ota::Phase::Check:    m = "buscando...";  break;
    case ota::Phase::UpToDate: m = "al dia";       break;
    case ota::Phase::Download: snprintf(buf, sizeof(buf), "bajando %d%%", pct); m = buf; break;
    case ota::Phase::Verify:   m = "verificando";  break;
    case ota::Phase::Flash:    m = "escribiendo";  break;
    case ota::Phase::Done:     m = "OK, reinicia"; break;
    case ota::Phase::Error:    m = "error";        break;
  }
  display.drawString(0, 22, m);
  if (d && *d) display.drawString(0, 40, d);
  display.display();
}

// F2 mantenido 4-5s (ver loop()): fuerza un chequeo OTA ya, sin esperar el
// comando LoRa del maestro -- util en banco/puesta en marcha. Reutiliza el
// mismo modulo ota:: y el mismo callback otaOled() del arranque.
static void runOtaCheckNow() {
  if (cfg.otaSsid[0] == '\0') {
    display.clear(); display.setFont(ArialMT_Plain_10);
    display.drawString(0, 0,  "MODO OTA (F2)");
    display.drawString(0, 22, "Sin WiFi de");
    display.drawString(0, 34, "mantenimiento configurada");
    display.display();
    delay(1500);
    return;
  }
  loraStandby();
  display.clear(); display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0,  "MODO OTA (F2)");
  display.drawString(0, 22, String("WiFi: ") + cfg.otaSsid);
  display.display();

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.otaSsid, cfg.otaPass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) delay(200);

  if (WiFi.status() == WL_CONNECTED) {
    ota::Config oc;
    oc.owner = "asdrubalfuentes";
    oc.repo  = "nodeIO";
    oc.currentVersion = FW_SEMVER;
    ota::Result r = ota::run(oc, otaOled);      // si actualiza, reinicia aqui dentro
    if (r.ok && !r.hasUpdate) { otaOled(ota::Phase::UpToDate, 0, "al dia"); delay(1200); }
  } else {
    otaOled(ota::Phase::Error, 0, "sin WiFi");
    delay(1500);
  }
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  loraBegin();                     // retoma LoRa normal (si no se reinicio por la actualizacion)
}

// ---- comando por Serial: "buscar actualizacion" ---------------------------
// Alternativa de banco al F2 mantenido 4-5s: util con el nodo solo conectado
// por USB (sin acceso al boton, o automatizando desde un script). Requiere
// cfg.otaSsid configurado en el portal -- runOtaCheckNow() ya avisa por OLED
// si no lo esta.
static bool serialCmdIs(const char *line, const char *cmd) {
  while (*line == ' ') line++;
  size_t n = strlen(cmd);
  if (strncasecmp(line, cmd, n) != 0) return false;
  char c = line[n];
  return c == '\0' || c == '\r' || c == '\n' || c == ' ';
}

static void serviceSerialCommands() {
  static char buf[64];
  static uint8_t len = 0;

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (len > 0) {
        buf[len] = '\0';
        len = 0;
        if (serialCmdIs(buf, "buscar actualizacion") ||
            serialCmdIs(buf, "buscar actualizaci\xC3\xB3n") ||   // con tilde (UTF-8)
            serialCmdIs(buf, "ota")) {
          Serial.println("[serial] buscar actualizacion -> forzando chequeo OTA");
          runOtaCheckNow();
        } else {
          Serial.printf("[serial] comando no reconocido: \"%s\" (probar: buscar actualizacion)\n", buf);
        }
      }
      continue;
    }
    if (len < sizeof(buf) - 1) buf[len++] = c;
  }
}

// Barra 0-100% (posicion del crudo entre rawMin/rawMax, el lazo 4-20mA
// calibrado) + valor ya escalado en su unidad de ingenieria. ch = 0 (nivel)
// o 1 (caudal) -- los unicos con calibracion propia hoy.
static void drawChannelScreen(uint8_t ch) {
  if (millis() - lastDrawMs < 200) return;
  lastDrawMs = millis();

  const ChannelCfg &c = cfg.ch[ch];
  uint16_t raw = ioReadAnalog(ch);
  float span = (float)c.rawMax - (float)c.rawMin;
  int pct = (span != 0) ? (int)lroundf(((float)raw - c.rawMin) / span * 100.0f) : 0;
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;

  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, String(c.name[0] ? c.name : (ch == 0 ? "Nivel" : "Caudal")));

  const int barX = 0, barY = 14, barW = 122, barH = 12;
  display.drawRect(barX, barY, barW, barH);
  display.fillRect(barX + 1, barY + 1, (barW - 2) * pct / 100, barH - 2);
  char pctbuf[8]; snprintf(pctbuf, sizeof(pctbuf), "%d%%", pct);
  display.drawString(barW / 2 - 8, barY + 1, pctbuf);

  const char *u = (ch == 0) ? UNIT_LEVEL_S[c.unit] : UNIT_FLOW_S[c.unit];
  char l[32]; snprintf(l, sizeof(l), "%.2f %s", chLive[ch].eng / 100.0f, u);
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 30, l);

  display.setFont(ArialMT_Plain_10);
  snprintf(l, sizeof(l), "raw:%u   F2=siguiente", raw);
  display.drawString(0, 52, l);
  display.display();
}

// Si hay bandera "OTA pendiente" en NVS: levanta la WiFi de mantenimiento,
// actualiza desde GitHub Releases y reinicia. Si no hay que actualizar o falla,
// apaga la WiFi y retorna para seguir el arranque normal (LoRa).
static void runOtaModeIfPending() {
  if (!otaTakePending()) return;
  Serial.println("[ota] arranque en MODO OTA");

  if (cfg.otaSsid[0] == '\0') {
    Serial.println("[ota] sin red de mantenimiento configurada");
    return;
  }

  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "MODO OTA");
  display.drawString(0, 22, String("WiFi: ") + cfg.otaSsid);
  display.display();

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.otaSsid, cfg.otaPass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) delay(200);

  if (WiFi.status() == WL_CONNECTED) {
    ota::Config oc;
    oc.owner = "asdrubalfuentes";
    oc.repo  = "nodeIO";
    oc.currentVersion = FW_SEMVER;
    ota::Result r = ota::run(oc, otaOled);      // si actualiza, reinicia aqui dentro
    Serial.printf("[ota] ok=%d update=%d %s\n", r.ok, r.hasUpdate, r.error);
    if (r.ok && !r.hasUpdate) { otaOled(ota::Phase::UpToDate, 0, "al dia"); delay(1200); }
  } else {
    Serial.println("[ota] la WiFi de mantenimiento no conecto");
    otaOled(ota::Phase::Error, 0, "sin WiFi");
    delay(1500);
  }

  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
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
  runOtaModeIfPending();          // si el maestro pidio OTA: actualiza y reinicia
  ioInit(cfg.relayEnable, cfg.relaySafe);
  channelsInit();                 // recupera acumulados del dia/mes desde NVS
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

  // BUTTON_2 (F2): toque corto = ciclar pantalla de diagnostico (normal ->
  // nivel -> caudal -> normal); mantenido 4-5s = forzar chequeo OTA ya.
  if (mode == MODE_NORMAL) {
    if (digitalRead(PIN_BUTTON_2) == LOW) {
      if (btn2DownSince == 0) btn2DownSince = millis();
      else if (!btn2OtaFired && millis() - btn2DownSince > 4000) {
        btn2OtaFired = true;
        runOtaCheckNow();
      }
    } else {
      if (btn2DownSince != 0 && !btn2OtaFired) diagScreen = (diagScreen + 1) % 3;
      btn2DownSince = 0;
      btn2OtaFired  = false;
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
  serviceSerialCommands();
  ioServicePulses(cfg.relaySafe);
  serviceLocalInputs();
  channelsService();               // escala + filtra EMA, cada vuelta
  if (millis() - last1sTickMs >= 1000) {
    last1sTickMs = millis();
    channelsTick1s();               // integra el totalizador, 1x/seg
  }
  if (diagScreen == 1)      drawChannelScreen(0);   // nivel
  else if (diagScreen == 2) drawChannelScreen(1);   // caudal
  else                      drawStatusScreen();
}
