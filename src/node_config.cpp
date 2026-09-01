#include "node_config.h"
#include <Preferences.h>

NodeConfig cfg;

static const char*    NVS_NS    = "nodeio";
static const uint32_t CFG_MAGIC = 0xA75AF104;   // bump if the struct layout changes

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
  cfg.adoptTimeoutS = 1800;   // 30 min without a master frame -> wait for re-adoption
  cfg.adopted      = false;
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
  if (p.getUInt("magic", 0) == CFG_MAGIC &&
      p.getBytesLength("blob") == sizeof(NodeConfig)) {
    p.getBytes("blob", &cfg, sizeof(NodeConfig));
  }
  p.end();
}

bool configSave() {
  Preferences p;
  if (!p.begin(NVS_NS, false)) return false;
  size_t n = p.putBytes("blob", &cfg, sizeof(NodeConfig));
  p.putUInt("magic", CFG_MAGIC);
  p.end();
  return n == sizeof(NodeConfig);
}

bool configStored() {
  Preferences p;
  if (!p.begin(NVS_NS, true)) return false;
  bool ok = (p.getUInt("magic", 0) == CFG_MAGIC);
  p.end();
  return ok;
}
