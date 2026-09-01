#include "portal.h"
#include "node_config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

bool portalActive = false;

static DNSServer  dns;
static WebServer  web(80);
static const byte DNS_PORT = 53;

// --------------------------------------------------------------------------
// HTML helpers
// --------------------------------------------------------------------------
static String chk(bool on) { return on ? " checked" : ""; }

static String buildPage() {
  String h;
  h.reserve(4096);
  h += F("<!doctype html><html><head><meta charset=utf-8>"
         "<meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>Nodo IO - Config</title><style>"
         "body{font-family:sans-serif;margin:0;background:#111;color:#eee}"
         "form{max-width:520px;margin:auto;padding:16px}"
         "h1{font-size:19px}fieldset{border:1px solid #444;margin:12px 0;border-radius:8px}"
         "legend{color:#8cf;padding:0 6px}label{display:block;margin:8px 0 2px;font-size:14px}"
         "input,select{width:100%;padding:8px;box-sizing:border-box;background:#222;color:#eee;"
         "border:1px solid #555;border-radius:6px}"
         ".row{display:flex;gap:10px}.row>div{flex:1}"
         ".cb{display:inline-block;width:auto;margin-right:6px}"
         "button{width:100%;padding:12px;margin-top:14px;font-size:16px;background:#08f;color:#fff;"
         "border:0;border-radius:8px}</style></head><body><form method=POST action=/save>"
         "<h1>Nodo IO &mdash; Configuraci&oacute;n</h1>");

  h += F("<fieldset><legend>Estado</legend>");
  h += "<label>MAC (idUnico)</label><input value='" + nodeMac() + "' readonly>";
  h += String("<label>Adopci&oacute;n</label><input value='") +
       (cfg.adopted ? ("ADOPTADO  addr " + String(cfg.nodeAddr)) : String("SIN ADOPTAR")) +
       "' readonly>";
  if (cfg.adopted)
    h += F("<button type=submit formaction=/release formmethod=post "
           "style='background:#a33'>Anular adopci&oacute;n</button>");
  h += F("</fieldset>");

  h += F("<fieldset><legend>Identidad</legend>");
  h += "<label>Direcci&oacute;n de este nodo (1-254, la fija el maestro al adoptar)</label><input name=naddr type=number min=1 max=254 value=" + String(cfg.nodeAddr) + ">";
  h += "<label>Direcci&oacute;n de maestro aceptada (0 = cualquiera)</label><input name=maddr type=number min=0 max=254 value=" + String(cfg.masterAddr) + ">";
  h += "<label>Timeout sin maestro (s, 0 = desactivado; pasa a esperar adopci&oacute;n)</label><input name=atmo type=number min=0 max=65535 value=" + String(cfg.adoptTimeoutS) + ">";
  h += "<label>Nombre</label><input name=nname maxlength=23 value='" + String(cfg.nodeName) + "'>";
  h += "<label>Ubicaci&oacute;n</label><input name=nloc maxlength=23 value='" + String(cfg.nodeLoc) + "'>";
  h += F("</fieldset>");

  h += F("<fieldset><legend>WiFi del portal (SoftAP)</legend>");
  h += "<label>SSID</label><input name=apssid maxlength=23 value='" + String(cfg.apSsid) + "'>";
  h += "<label>Clave (vac&iacute;o = abierta, si no min 8)</label><input name=appass maxlength=23 value='" + String(cfg.apPass) + "'>";
  h += F("</fieldset>");

  h += F("<fieldset><legend>LoRa (debe coincidir con el maestro)</legend>");
  h += "<label>Frecuencia MHz</label><input name=lfreq type=number step=0.1 value=" + String(cfg.loraFreq, 1) + ">";
  h += "<label>Ancho de banda kHz</label><input name=lbw type=number step=0.1 value=" + String(cfg.loraBw, 1) + ">";
  h += "<div class=row><div><label>SF (7-12)</label><input name=lsf type=number min=7 max=12 value=" + String(cfg.loraSf) + "></div>";
  h += "<div><label>CR (5-8)</label><input name=lcr type=number min=5 max=8 value=" + String(cfg.loraCr) + "></div></div>";
  h += "<div class=row><div><label>Sync word (hex)</label><input name=lsync value=0x" + String(cfg.loraSync, HEX) + "></div>";
  h += "<div><label>Potencia TX dBm (2-22)</label><input name=lpwr type=number min=2 max=22 value=" + String(cfg.loraPwr) + "></div></div>";
  h += F("</fieldset>");

  h += F("<fieldset><legend>Rel&eacute;s</legend><label>Habilitaci&oacute;n</label>");
  for (int i = 0; i < 4; i++)
    h += "<label class=cb><input class=cb type=checkbox name=en" + String(i + 1) + chk((cfg.relayEnable >> i) & 1) + ">R" + String(i + 1) + "</label>";
  h += F("<label>Estado seguro / arranque</label>");
  for (int i = 0; i < 4; i++)
    h += "<label class=cb><input class=cb type=checkbox name=sf" + String(i + 1) + chk((cfg.relaySafe >> i) & 1) + ">R" + String(i + 1) + "</label>";
  h += "<label>Modo</label><select name=rmode><option value=0" + String(cfg.relayMode == 0 ? " selected" : "") +
       ">Enclavado</option><option value=1" + String(cfg.relayMode == 1 ? " selected" : "") + ">Pulso</option></select>";
  h += "<label>Ancho de pulso ms</label><input name=rpulse type=number min=10 max=60000 value=" + String(cfg.relayPulseMs) + ">";
  h += F("</fieldset>");

  h += F("<button type=submit>Guardar y reiniciar</button></form></body></html>");
  return h;
}

// --------------------------------------------------------------------------
// Request handlers
// --------------------------------------------------------------------------
static void handleRoot() { web.send(200, "text/html", buildPage()); }

static void handleRedirect() {
  web.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
  web.send(302, "text/plain", "");
}

static uint8_t argU8(const char* k, uint8_t def, uint8_t lo, uint8_t hi) {
  if (!web.hasArg(k)) return def;
  long v = web.arg(k).toInt();
  if (v < lo) v = lo; if (v > hi) v = hi;
  return (uint8_t)v;
}

static void handleSave() {
  cfg.nodeAddr   = argU8("naddr", cfg.nodeAddr, 1, 254);
  cfg.masterAddr = argU8("maddr", cfg.masterAddr, 0, 254);
  if (web.hasArg("atmo")) cfg.adoptTimeoutS = (uint16_t)constrain(web.arg("atmo").toInt(), 0, 65535);
  web.arg("nname").toCharArray(cfg.nodeName, sizeof(cfg.nodeName));
  web.arg("nloc").toCharArray(cfg.nodeLoc, sizeof(cfg.nodeLoc));
  web.arg("apssid").toCharArray(cfg.apSsid, sizeof(cfg.apSsid));
  web.arg("appass").toCharArray(cfg.apPass, sizeof(cfg.apPass));

  if (web.hasArg("lfreq")) cfg.loraFreq = web.arg("lfreq").toFloat();
  if (web.hasArg("lbw"))   cfg.loraBw   = web.arg("lbw").toFloat();
  cfg.loraSf  = argU8("lsf", cfg.loraSf, 7, 12);
  cfg.loraCr  = argU8("lcr", cfg.loraCr, 5, 8);
  cfg.loraPwr = (int8_t)argU8("lpwr", cfg.loraPwr, 2, 22);
  if (web.hasArg("lsync")) cfg.loraSync = (uint8_t)strtol(web.arg("lsync").c_str(), nullptr, 0);

  uint8_t en = 0, sf = 0;
  for (int i = 0; i < 4; i++) {
    if (web.hasArg((String("en") + (i + 1)).c_str())) en |= (1 << i);
    if (web.hasArg((String("sf") + (i + 1)).c_str())) sf |= (1 << i);
  }
  cfg.relayEnable  = en;
  cfg.relaySafe    = sf;
  cfg.relayMode    = argU8("rmode", cfg.relayMode, 0, 1);
  if (web.hasArg("rpulse")) cfg.relayPulseMs = (uint16_t)constrain(web.arg("rpulse").toInt(), 10, 60000);

  bool ok = configSave();
  web.send(200, "text/html",
           String(F("<!doctype html><meta charset=utf-8><body style='font-family:sans-serif'>")) +
           (ok ? F("Configuraci&oacute;n guardada. Reiniciando...") : F("ERROR al guardar.")) +
           F("</body>"));
  delay(600);
  ESP.restart();
}

static void handleRelease() {
  cfg.adopted = false;
  configSave();
  web.send(200, "text/html",
           F("<!doctype html><meta charset=utf-8><body style='font-family:sans-serif'>"
             "Adopci&oacute;n anulada. Reiniciando...</body>"));
  delay(600);
  ESP.restart();
}

// --------------------------------------------------------------------------
void portalStart() {
  WiFi.mode(WIFI_AP);
  if (strlen(cfg.apPass) >= 8) WiFi.softAP(cfg.apSsid, cfg.apPass);
  else                         WiFi.softAP(cfg.apSsid);

  IPAddress ip = WiFi.softAPIP();
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(DNS_PORT, "*", ip);

  web.on("/", handleRoot);
  web.on("/save", HTTP_POST, handleSave);
  web.on("/release", HTTP_POST, handleRelease);
  web.on("/generate_204", handleRedirect);       // Android
  web.on("/gen_204", handleRedirect);            // Android
  web.on("/hotspot-detect.html", handleRedirect);// iOS / macOS
  web.on("/ncsi.txt", handleRedirect);           // Windows
  web.on("/connecttest.txt", handleRedirect);    // Windows
  web.onNotFound(handleRedirect);
  web.begin();

  portalActive = true;
  Serial.printf("[portal] AP '%s' en %s\n", cfg.apSsid, ip.toString().c_str());
}

void portalStop() {
  web.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  portalActive = false;
}

void portalLoop() {
  dns.processNextRequest();
  web.handleClient();
}

String portalIP() { return WiFi.softAPIP().toString(); }
