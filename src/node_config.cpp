#include "node_config.h"
#include <Preferences.h>

NodeConfig cfg;

static const char*    NVS_NS    = "nodeio";
static const uint32_t CFG_MAGIC = 0xA75AF107;   // bump if the struct layout changes
                                                // 105: adoptTimeoutS default 0 (el nodo ya no
                                                //      se des-adopta por silencio; ver ROLLCALL)
                                                // 106: + otaSsid/otaPass (WiFi de mantenimiento OTA)
                                                // 107: + ch[4]/diName/doName (escalado+totalizador
                                                //      en el nodo). El bump reinicia SOLO estos
                                                //      campos nuevos a fabrica -- la identidad
                                                //      (adopted/nodeAddr/canal LoRa) sobrevive por
                                                //      el split de identityLoad/Save de abajo.

// --- Identidad / emparejamiento -------------------------------------------
// Estos campos se guardan TAMBIEN como claves sueltas (sin magic): sobreviven a
// un cambio de CFG_MAGIC (nuevo firmware con features nuevas). Sin ellos, cada
// bump de magic dejaba al nodo "sin adoptar" y fuera de su canal LoRa.
static void identityLoad(Preferences& p) {
  cfg.adopted    = p.getBool ("id_adopted", cfg.adopted);
  cfg.nodeAddr   = p.getUChar("id_naddr",   cfg.nodeAddr);
  cfg.masterAddr = p.getUChar("id_maddr",   cfg.masterAddr);
  cfg.loraFreq   = p.getFloat("id_lfreq",   cfg.loraFreq);
  cfg.loraBw     = p.getFloat("id_lbw",     cfg.loraBw);
  cfg.loraSf     = p.getUChar("id_lsf",     cfg.loraSf);
  cfg.loraCr     = p.getUChar("id_lcr",     cfg.loraCr);
  cfg.loraSync   = p.getUChar("id_lsync",   cfg.loraSync);
  cfg.loraPwr    = (int8_t)p.getChar("id_lpwr", cfg.loraPwr);
}

static void identitySave(Preferences& p) {
  p.putBool ("id_adopted", cfg.adopted);
  p.putUChar("id_naddr",   cfg.nodeAddr);
  p.putUChar("id_maddr",   cfg.masterAddr);
  p.putFloat("id_lfreq",   cfg.loraFreq);
  p.putFloat("id_lbw",     cfg.loraBw);
  p.putUChar("id_lsf",     cfg.loraSf);
  p.putUChar("id_lcr",     cfg.loraCr);
  p.putUChar("id_lsync",   cfg.loraSync);
  p.putChar ("id_lpwr",    cfg.loraPwr);
  p.putBool ("id_set",     true);
}

void configFactory() {
  cfg = NodeConfig{};
  cfg.nodeAddr     = 1;
  cfg.masterAddr   = 0;
  strncpy(cfg.nodeName, "NodoIO", sizeof(cfg.nodeName));
  cfg.nodeLoc[0]   = '\0';
  strncpy(cfg.apSsid, "NodoIO-Setup", sizeof(cfg.apSsid));
  strncpy(cfg.apPass, "aysafi1234", sizeof(cfg.apPass));
  cfg.loraFreq     = 915.0f;
  cfg.loraBw       = 125.0f;
  cfg.loraSf       = 9;
  cfg.loraCr       = 5;
  cfg.loraSync     = 0x34;
  cfg.loraPwr      = 14;
  cfg.relayEnable  = 0x0F;
  cfg.relaySafe    = 0x00;
  cfg.relayMode    = 0;
  cfg.relayPulseMs = 500;
  cfg.adoptTimeoutS = 0;      // 0 = el nodo NUNCA se des-adopta por silencio del maestro.
                              // Solo RELEASE des-adopta. Si el maestro pierde su tabla la
                              // reconstruye con ROLLCALL. >0 = baliza HERE cada N s (no libera).
  cfg.adopted      = false;
  cfg.otaSsid[0]   = '\0';    // OTA remota deshabilitada hasta configurar la red de mantenimiento
  cfg.otaPass[0]   = '\0';

  // Canal 0 = Nivel, canal 1 = Caudal (los dos que existen hoy en el pozo);
  // 2/3 quedan reservados/deshabilitados hasta que haya sensores ahi.
  for (uint8_t i = 0; i < 4; i++) {
    ChannelCfg &c = cfg.ch[i];
    c.name[0]    = '\0';
    c.rawMin     = 800;
    c.rawMax     = 4000;
    c.engMin     = 0;
    c.engMax     = 10000;
    c.unit       = 0;
    c.filter     = 15;
    c.totDaily   = false;
    c.totMonthly = false;
    c.almHi      = CH_ALM_OFF;
    c.almLo      = CH_ALM_OFF;
  }
  strncpy(cfg.ch[0].name, "Nivel", sizeof(cfg.ch[0].name));
  cfg.ch[0].unit  = 1;              // metros
  cfg.ch[0].almLo = 500;            // 5,00 m -- nivel bajo de referencia, ajustar en terreno

  strncpy(cfg.ch[1].name, "Caudal", sizeof(cfg.ch[1].name));
  cfg.ch[1].engMax     = 10000;     // 0..100,00 m3/h
  cfg.ch[1].unit       = 1;         // m3/h (fijo en todo el sistema, ver miHMI/screen_well)
  cfg.ch[1].totDaily   = true;
  cfg.ch[1].totMonthly = true;
  cfg.ch[1].almHi      = 9000;      // 90,00 m3/h -- caudal alto de referencia

  for (uint8_t i = 0; i < 4; i++) {
    snprintf(cfg.diName[i], sizeof(cfg.diName[i]), "DI%u", (unsigned)(i + 1));
    snprintf(cfg.doName[i], sizeof(cfg.doName[i]), "RO%u", (unsigned)(i + 1));
  }
}

String nodeMac() {
  char b[13];
  snprintf(b, sizeof(b), "%012llX", (unsigned long long)ESP.getEfuseMac());
  return String(b);
}

void configLoad() {
  configFactory();
  Preferences p;
  if (!p.begin(NVS_NS, true)) return;
  // 1) blob de features (solo si el layout coincide con este firmware)
  if (p.getUInt("magic", 0) == CFG_MAGIC &&
      p.getBytesLength("blob") == sizeof(NodeConfig)) {
    p.getBytes("blob", &cfg, sizeof(NodeConfig));
  }
  // 2) identidad: siempre (con lo anterior como default) -> gana
  identityLoad(p);
  p.end();
}

bool configSave() {
  Preferences p;
  if (!p.begin(NVS_NS, false)) return false;
  size_t n = p.putBytes("blob", &cfg, sizeof(NodeConfig));
  p.putUInt("magic", CFG_MAGIC);
  identitySave(p);
  p.end();
  return n == sizeof(NodeConfig);
}

bool configStored() {
  Preferences p;
  if (!p.begin(NVS_NS, true)) return false;
  bool ok = (p.getUInt("magic", 0) == CFG_MAGIC) || p.getBool("id_set", false);
  p.end();
  return ok;
}

void otaSetPending(bool v) {
  Preferences p;
  if (!p.begin(NVS_NS, false)) return;
  p.putUChar("otapend", v ? 1 : 0);
  p.end();
}

bool otaTakePending() {
  Preferences p;
  if (!p.begin(NVS_NS, false)) return false;
  bool v = p.getUChar("otapend", 0) != 0;
  if (v) p.putUChar("otapend", 0);
  p.end();
  return v;
}
