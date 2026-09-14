#include "lora_proto.h"
#include "node_config.h"
#include "io.h"
#include "channels.h"
#include <RadioLib.h>
#include <CRC32.h>

// SX1262 instance lives in heltec_unofficial.h (included only by main.cpp).
extern SX1262 radio;

LoraStats loraStats = {0, 0, 0, 0, 0, "", 0};

#define PROTO_FW "1.2026.007"   // + ST trae escalado/acumulados/alarma (ch0=nivel,ch1=caudal);
                                // + comandos CD/CM (cierre de dia/mes, dispara el gateway)

static volatile bool rxFlag = false;
static const uint8_t BCAST  = 255;

static void IRAM_ATTR onDio1() { rxFlag = true; }

bool loraBegin() {
  int16_t st = radio.begin(cfg.loraFreq, cfg.loraBw, cfg.loraSf,
                           cfg.loraCr, cfg.loraSync, cfg.loraPwr, 8);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("[lora] begin() fallo, code %d\n", st);
    return false;
  }
  radio.setDio1Action(onDio1);
  radio.startReceive();
  loraStats.lastRxMs = millis();          // start the adoption watchdog fresh on boot
  randomSeed((uint32_t)ESP.getEfuseMac());
  Serial.printf("[lora] RX %.1f MHz SF%u BW%.0f CR4/%u sync 0x%02X addr %u %s\n",
                cfg.loraFreq, cfg.loraSf, cfg.loraBw, cfg.loraCr, cfg.loraSync,
                cfg.nodeAddr, cfg.adopted ? "ADOPTADO" : "SIN ADOPTAR");
  return true;
}

void loraStandby() {
  radio.standby();
}

// ---- payload helpers -----------------------------------------------------
static void appendCrcAndSend(char* text, size_t textLen) {
  uint32_t crc = CRC32::calculate((const uint8_t*)text, textLen);
  uint8_t frame[255];
  if (textLen + 5 > sizeof(frame)) return;
  memcpy(frame, text, textLen);
  memcpy(frame + textLen, &crc, 4);      // little-endian raw, matches house style
  frame[textLen + 4] = '\n';
  int16_t st = radio.transmit(frame, textLen + 5);
  radio.startReceive();
  if (st == RADIOLIB_ERR_NONE) loraStats.txCount++;
}

static void reply(uint8_t dst, const char* seq, const char* body) {
  uint8_t src = cfg.adopted ? cfg.nodeAddr : 0;   // src 0 = "unadopted node"
  char text[220];
  int n = snprintf(text, sizeof(text), "%u,%u,%s,%s", dst, src, seq, body);
  if (n > 0 && n < (int)sizeof(text)) appendCrcAndSend(text, n);
}

// Campos nuevos (desde PROTO_FW 1.2026.007): escalado + acumulados + alarma
// de los canales 0 (nivel) y 1 (caudal) -- los unicos con sentido hoy. 2/3
// siguen solo-crudo, igual que antes. Acumulados van x1000 (m3) como entero,
// para no depender de locale/precision de punto flotante en el texto.
static void buildStatus(char* out, size_t n) {
  uint8_t almBits = (chLive[0].almLo ? 1 : 0) | (chLive[0].almHi ? 2 : 0)
                  | (chLive[1].almLo ? 4 : 0) | (chLive[1].almHi ? 8 : 0);
  snprintf(out, n,
    "ST,%u,%u,%u,%u,%u,%u,%u,%u,%c,%c,%c,%c,%d,%d,%ld,%ld,%ld,%ld,%u",
    ioReadAnalog(0), ioReadAnalog(1), ioReadAnalog(2), ioReadAnalog(3),
    ioReadDigital(0), ioReadDigital(1), ioReadDigital(2), ioReadDigital(3),
    (cfg.relayEnable & 1) ? ('0' + ioGetRelay(0)) : 'x',
    (cfg.relayEnable & 2) ? ('0' + ioGetRelay(1)) : 'x',
    (cfg.relayEnable & 4) ? ('0' + ioGetRelay(2)) : 'x',
    (cfg.relayEnable & 8) ? ('0' + ioGetRelay(3)) : 'x',
    chLive[0].eng, chLive[1].eng,
    lroundf(chLive[0].accDia * 1000.0f), lroundf(chLive[0].accMes * 1000.0f),
    lroundf(chLive[1].accDia * 1000.0f), lroundf(chLive[1].accMes * 1000.0f),
    almBits);
}

// ---- provisioning (discovery / adoption) ------------------------------
static void sendIAM(const char* seq, uint8_t master) {
  delay(random(0, 800));                 // spread replies from many unadopted nodes
  char body[48];
  snprintf(body, sizeof(body), "IAM,%s,%s", nodeMac().c_str(), PROTO_FW);
  reply(master, seq, body);
}

// ROLLCALL: un maestro que perdio su tabla la reconstruye desde el campo.
// Solo responden los nodos ADOPTADOS, con su MAC, direccion y el maestro al que
// creen pertenecer. No cambia nada del nodo (sigue adoptado).
static void sendHERE(const char* seq, uint8_t master) {
  delay(random(0, 800));                 // spread replies from many adopted nodes
  char body[56];
  snprintf(body, sizeof(body), "HERE,%s,%u,%u",
           nodeMac().c_str(), cfg.nodeAddr, cfg.masterAddr);
  reply(master, seq, body);
}

static void handleAdopt(char*& save, const char* seq, uint8_t master) {
  char* mac  = strtok_r(nullptr, ",", &save);
  char* addr = strtok_r(nullptr, ",", &save);
  if (!mac || !addr || cfg.adopted) return;
  if (!nodeMac().equalsIgnoreCase(mac)) return;      // ADOPT is for another node

  cfg.nodeAddr = (uint8_t)atoi(addr);
  char* f;
  if ((f = strtok_r(nullptr, ",", &save))) cfg.loraFreq = atof(f);
  if ((f = strtok_r(nullptr, ",", &save))) cfg.loraSf   = (uint8_t)atoi(f);
  if ((f = strtok_r(nullptr, ",", &save))) cfg.loraBw   = atof(f);
  if ((f = strtok_r(nullptr, ",", &save))) cfg.loraCr   = (uint8_t)atoi(f);
  if ((f = strtok_r(nullptr, ",", &save))) cfg.loraSync = (uint8_t)strtol(f, nullptr, 0);
  if ((f = strtok_r(nullptr, ",", &save))) cfg.loraPwr  = (int8_t)atoi(f);
  cfg.adopted = true;
  configSave();

  // Re-init the radio on the master's channel BEFORE acking, so the ACK (and
  // everything after the reboot) is heard even if the master runs a custom channel.
  radio.begin(cfg.loraFreq, cfg.loraBw, cfg.loraSf, cfg.loraCr, cfg.loraSync, cfg.loraPwr, 8);
  radio.setDio1Action(onDio1);
  radio.startReceive();

  char body[32];
  snprintf(body, sizeof(body), "ACK,%s", nodeMac().c_str());
  reply(master, seq, body);              // src is now cfg.nodeAddr, on the new channel
  Serial.printf("[prov] ADOPTADO addr %u -> reinicio\n", cfg.nodeAddr);
  delay(300);
  ESP.restart();
}

// back to the default discovery channel so any master can find the node again
static void resetToDiscoveryChannel() {
  cfg.loraFreq = 915.0f; cfg.loraBw = 125.0f; cfg.loraSf = 9;
  cfg.loraCr = 5; cfg.loraSync = 0x34; cfg.loraPwr = 14;
}

// OTA: el maestro pide al nodo que reinicie en "modo actualizacion". El nodo
// marca una bandera en NVS, hace ACK y reinicia; en el arranque levanta la WiFi
// de mantenimiento (cfg.otaSsid, configurada por el portal), descarga el
// firmware de GitHub Releases, lo verifica y flashea. Formato:
//   dst,src,seq,OTA,<mac>          (sin credenciales por el aire)
static void handleOta(char*& save, const char* seq, uint8_t master) {
  char* mac = strtok_r(nullptr, ",", &save);
  if (!mac || !nodeMac().equalsIgnoreCase(mac)) return;   // OTA para otro nodo

  if (cfg.otaSsid[0] == '\0') {
    reply(master, seq, "ERR,NOWIFI");                     // no hay red de mantenimiento
    return;
  }
  otaSetPending(true);
  char body[40];
  snprintf(body, sizeof(body), "ACK,%s,OTA", nodeMac().c_str());
  reply(master, seq, body);
  Serial.println("[ota] pendiente -> reinicio a modo OTA");
  delay(400);
  ESP.restart();
}

static void handleRelease(char*& save, const char* seq, uint8_t master) {
  char* mac = strtok_r(nullptr, ",", &save);
  if (!mac || !nodeMac().equalsIgnoreCase(mac)) return;
  cfg.adopted = false;
  resetToDiscoveryChannel();
  configSave();
  char body[32];
  snprintf(body, sizeof(body), "ACK,%s", nodeMac().c_str());
  reply(master, seq, body);
  Serial.println("[prov] RELEASE -> reinicio sin adoptar");
  delay(300);
  ESP.restart();
}

// ---- command dispatch --------------------------------------------------
static void handleFrame(char* text) {
  // tokenize: dst,src,seq,cmd,arg,arg,...
  char* save = nullptr;
  char* t_dst = strtok_r(text, ",", &save);
  char* t_src = strtok_r(nullptr, ",", &save);
  char* t_seq = strtok_r(nullptr, ",", &save);
  char* t_cmd = strtok_r(nullptr, ",", &save);
  if (!t_dst || !t_src || !t_seq || !t_cmd) { loraStats.rxCrcBad++; return; }

  uint8_t dst = (uint8_t)atoi(t_dst);
  uint8_t src = (uint8_t)atoi(t_src);

  // provisioning commands bypass the address filter
  if (!strcmp(t_cmd, "DISC"))     { if (!cfg.adopted) sendIAM(t_seq, src);  return; }
  if (!strcmp(t_cmd, "ROLLCALL")) { if (cfg.adopted)  sendHERE(t_seq, src); return; }
  if (!strcmp(t_cmd, "ADOPT"))    { handleAdopt(save, t_seq, src);          return; }
  if (!strcmp(t_cmd, "RELEASE"))  { handleRelease(save, t_seq, src);        return; }
  if (!strcmp(t_cmd, "OTA"))      { handleOta(save, t_seq, src);            return; }

  if (!cfg.adopted)                                    { loraStats.rxNotForUs++; return; }
  if (dst != cfg.nodeAddr && dst != BCAST)             { loraStats.rxNotForUs++; return; }
  if (cfg.masterAddr != 0 && src != cfg.masterAddr)    { loraStats.rxNotForUs++; return; }

  loraStats.rxOk++;
  loraStats.lastRssi = (int16_t)radio.getRSSI();
  loraStats.lastRxMs = millis();
  strncpy(loraStats.lastCmd, t_cmd, sizeof(loraStats.lastCmd) - 1);
  loraStats.lastCmd[sizeof(loraStats.lastCmd) - 1] = '\0';

  char body[220];

  if (!strcmp(t_cmd, "RD")) {
    buildStatus(body, sizeof(body));
    reply(src, t_seq, body);

  } else if (!strcmp(t_cmd, "WR")) {
    for (uint8_t i = 0; i < 4; i++) {
      char* a = strtok_r(nullptr, ",", &save);
      if (!a || a[0] == '-') continue;
      uint8_t on = (a[0] == '1') ? 1 : 0;
      if (cfg.relayMode == 1 && on)
        ioPulseRelay(i, cfg.relayPulseMs, cfg.relayEnable, cfg.relaySafe);
      else
        ioSetRelay(i, on, cfg.relayEnable);
    }
    buildStatus(body, sizeof(body));
    reply(src, t_seq, body);

  } else if (!strcmp(t_cmd, "WP")) {
    char* a_idx = strtok_r(nullptr, ",", &save);
    char* a_ms  = strtok_r(nullptr, ",", &save);
    if (!a_idx || !a_ms) { reply(src, t_seq, "ERR,FMT"); return; }
    int idx = atoi(a_idx);
    int ms  = atoi(a_ms);
    if (idx < 1 || idx > 4 || ms < 1) { reply(src, t_seq, "ERR,RANGE"); return; }
    if (!ioPulseRelay(idx - 1, (uint16_t)ms, cfg.relayEnable, cfg.relaySafe)) {
      reply(src, t_seq, "ERR,DIS"); return;
    }
    buildStatus(body, sizeof(body));
    reply(src, t_seq, body);

  } else if (!strcmp(t_cmd, "CD")) {          // cierre de dia -- lo dispara el gateway (con hora real)
    char* a_mask = strtok_r(nullptr, ",", &save);
    channelsCloseDay(a_mask ? (uint8_t)atoi(a_mask) : 0x0F);
    buildStatus(body, sizeof(body));
    reply(src, t_seq, body);

  } else if (!strcmp(t_cmd, "CM")) {          // cierre de mes -- idem
    char* a_mask = strtok_r(nullptr, ",", &save);
    channelsCloseMonth(a_mask ? (uint8_t)atoi(a_mask) : 0x0F);
    buildStatus(body, sizeof(body));
    reply(src, t_seq, body);

  } else if (!strcmp(t_cmd, "PING")) {
    snprintf(body, sizeof(body), "PONG,%lu,%d",
             (unsigned long)(millis() / 1000), loraStats.lastRssi);
    reply(src, t_seq, body);

  } else {
    reply(src, t_seq, "ERR,CMD");
  }
}

// ---- adoption watchdog --------------------------------------------------
// Un nodo adoptado SOLO sale de ese estado con un RELEASE explicito. Si el
// maestro lo olvida (reflasheado, NVS borrada, quitado de su lista) el nodo se
// queda adoptado y espera: el maestro reconstruye su tabla con ROLLCALL.
//
// Con cfg.adoptTimeoutS > 0, tras ese silencio el nodo emite UNA baliza HERE a
// 255 (se anuncia) y re-arma el contador. Nunca libera la adopcion ni reinicia.
static void serviceAdoptionWatchdog() {
  if (!cfg.adopted || cfg.adoptTimeoutS == 0) return;
  if (millis() - loraStats.lastRxMs < (uint32_t)cfg.adoptTimeoutS * 1000UL) return;

  Serial.printf("[prov] maestro callado %us -> baliza HERE\n", cfg.adoptTimeoutS);
  char body[56];
  snprintf(body, sizeof(body), "HERE,%s,%u,%u",
           nodeMac().c_str(), cfg.nodeAddr, cfg.masterAddr);
  reply(BCAST, "0", body);
  loraStats.lastRxMs = millis();          // re-arma; no floodear
}

void loraLoop() {
  serviceAdoptionWatchdog();

  if (!rxFlag) return;
  rxFlag = false;

  uint8_t buf[255];
  int len = radio.getPacketLength();
  int16_t st = radio.readData(buf, len);
  radio.startReceive();               // re-arm regardless of outcome

  if (st != RADIOLIB_ERR_NONE || len < 5) { loraStats.rxCrcBad++; return; }

  if (buf[len - 1] == '\n') len--;    // optional trailing newline
  if (len < 5) { loraStats.rxCrcBad++; return; }

  size_t textLen = len - 4;
  uint32_t rxCrc;
  memcpy(&rxCrc, buf + textLen, 4);
  if (CRC32::calculate(buf, textLen) != rxCrc) { loraStats.rxCrcBad++; return; }

  char text[220];
  if (textLen >= sizeof(text)) { loraStats.rxCrcBad++; return; }
  memcpy(text, buf, textLen);
  text[textLen] = '\0';
  handleFrame(text);
}
