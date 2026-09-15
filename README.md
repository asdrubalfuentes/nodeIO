# nodeIO — README del programador

Firmware del **Nodo IO** (responder LoRa) para **Heltec WiFi LoRa 32 V3**
(ESP32-S3 + SX1262). Atiende peticiones de un maestro por LoRa: reporta 4
entradas analógicas + 4 digitales y escribe 4 relés. Se configura por portal
cautivo (NVS).

Proyecto hermano: `../nodeIO_master` (maestro de prueba).

---

## 1. Toolchain

- PlatformIO (`platform = espressif32`, `framework = arduino`, board
  `heltec_wifi_lora_32_V3`).
- Dependencias (`platformio.ini`):
  - `ropg/Heltec_ESP32_LoRa_v3` — wrapper "heltec_unofficial": trae **RadioLib**
    (radio `SX1262`), **SSD1306Wire** (OLED `display`), **HotButton** (`button`)
    y helpers de energía.
  - `bakercp/CRC32` — CRC32 del payload.
- `WiFi`, `WebServer`, `DNSServer`, `Preferences` vienen en el core arduino-esp32.

```sh
pio run                 # compilar
pio run -t upload       # flashear
pio device monitor -b 115200
```

---

## 2. Arquitectura

| Módulo | Responsabilidad |
|---|---|
| `src/main.cpp` | Máquina de estados `MODE_NORMAL` / `MODE_PORTAL` / `MODE_WAIT_ADOPT`, splash OLED, render de estado, long-press de BUTTON_1, botón PRG local. **Único TU que incluye `heltec_unofficial.h`.** |
| `src/io.{h,cpp}` | Mapa de pines, init, ISR compartida de entradas digitales/botones, lectura de AI/DI, control de relés con máscara de habilitación + estado seguro + pulsos. |
| `src/node_config.{h,cpp}` | `struct NodeConfig` persistida en NVS (namespace `nodeio`, un blob + `magic`), incluye `bool adopted`. `nodeMac()` = idUnico (12 hex efuse). Sustituye los stubs `rescueFlashConfig()/saveFlashConfig()` del scaffold original. |
| `src/portal.{h,cpp}` | Portal cautivo: `WiFi.softAP` + `DNSServer` (:53, `*`) + `WebServer` (:80). `POST /save` valida, persiste y reinicia. `POST /release` anula la adopción. Muestra MAC + estado de adopción. `GET /live`: página de solo lectura con los mismos datos de la trama `ST` (crudo/ingeniería/acumulados/alarmas/DI/relés/enlace), auto-refrescada cada 2 s — para ver el nodo desde el celular parado junto al equipo. |
| `src/lora_proto.{h,cpp}` | Protocolo responder: `begin()` desde `cfg`, RX por interrupción (`setDio1Action`), verificación CRC32. Aprovisionamiento `DISC`/`ROLLCALL`/`ADOPT`/`RELEASE`/**`OTA`**. Dispatch de `RD`/`WR`/`WP`/`PING` solo si `cfg.adopted`. |
| `src/ota_update.{h,cpp}` | Cliente **OTA "GitHub Releases pull"** (módulo común de `../ORCHESTRATION/tools/ota/`): `version.txt` → `firmware.bin` + verificación `SHA-256`. Lo dispara el comando LoRa `OTA` (ver §5). |
| `src/images.h` | XBM del splash (`poweredBy`, `aysafi_Logo_bits`). |
| `PROTOCOL.md` | Especificación de la trama y los comandos. |

### Regla de include de `heltec_unofficial.h`

El header **define objetos globales y funciones no-inline** (`radio`, `display`,
`button`, `both`, `heltec_*`). Si se incluye en más de un `.cpp` → error de
símbolos duplicados en el enlace. Por eso:

- Solo `main.cpp` lo incluye.
- `lora_proto.cpp` hace `#include <RadioLib.h>` y `extern SX1262 radio;` para usar
  la instancia global sin redefinirla.
- Todo el dibujo en OLED vive en `main.cpp` (los demás módulos exponen estado,
  no dibujan).

---

## 3. Flujo de ejecución

```
setup(): heltec_setup()  -> configLoad() -> runOtaModeIfPending()
         -> ioInit(cfg.relayEnable, cfg.relaySafe) -> splash()
         -> loraBegin() ? (cfg.adopted ? MODE_NORMAL : MODE_WAIT_ADOPT) : enterPortal()

loop():  heltec_loop()                     // HotButton
         BUTTON_1 > 3 s (salvo en portal) -> enterPortal()
         MODE_WAIT_ADOPT: loraLoop() (atiende DISC/ADOPT) + pantalla "SIN ADOPTAR"
         MODE_NORMAL:
           loraLoop()                       // atiende 1 paquete RX y responde
           ioServicePulses(cfg.relaySafe)   // cierra pulsos vencidos
           serviceLocalInputs()             // PRG conmuta relé 1, limpia flags DI
           drawStatusScreen()               // OLED throttled 250 ms
         MODE_PORTAL: portalLoop() + drawPortalScreen()
```

RX es **half-duplex**: la ISR marca `rxFlag`; `loraLoop()` hace
`getPacketLength()` + `readData()`, re-arma con `startReceive()`, valida CRC y
despacha. La respuesta se transmite con `radio.transmit()` y se vuelve a
`startReceive()`.

---

## 4. Protocolo (resumen)

```
"<dst>,<src>,<seq>,<cmd>[,<arg>...]"  + CRC32(texto, 4 bytes LE)  + '\n'
```

- Aprovisionamiento (salta el filtro de dirección): `DISC` → `IAM,<mac>,<fw>` (solo sin adoptar);
  `ROLLCALL` → `HERE,<mac>,<addr>,<masterAddr>` (solo adoptado, el maestro reconstruye su tabla);
  `ADOPT,<mac>,<addr>,<canal>` → `ACK` + reinicio; `RELEASE,<mac>` → `ACK` + reinicio;
  `OTA,<mac>` → `ACK,<mac>,OTA` + reinicio en **modo actualización** (§ siguiente).
- El nodo adoptado **no se des-adopta por silencio del maestro** (`adoptTimeoutS = 0`
  por defecto); con `> 0` emite una baliza `HERE` y re-arma, sin liberar.
- Operación (solo si `cfg.adopted`): CRC ok · `dst == cfg.nodeAddr || dst == 255` ·
  `cfg.masterAddr == 0 || src == cfg.masterAddr`.
  `RD` → `ST,<a1..a4>,<d1..d4>,<o1..o4>` · `WR,<r1..r4>` (`0/1/-`) ·
  `WP,<idx1..4>,<ms>` · `PING` → `PONG,<uptime_s>,<rssi>`.
- `src = 0` en una trama del nodo = "sin adoptar". Errores: `ERR,<FMT|CMD|DIS|RANGE>`.
- Detalle completo en `PROTOCOL.md`.

---

## 5. OTA (actualización remota sin cable)

Módulo `src/ota_update.{h,cpp}` + CI `.github/workflows/release.yml` (modelo de
`../ORCHESTRATION/OTA_ROLLOUT.md`).

- **Publicar:** `git tag vX.Y.Z` sobre `main` → el workflow compila e inyecta
  `-D FW_VERSION_OVERRIDE=X.Y.Z`, y publica un Release `latest` con
  `firmware.bin` + `version.txt` + `firmware.sha256`. `FW_SEMVER` (`main.cpp`) es
  la versión de respaldo cuando no hay CI.
- **Aplicar:** el maestro manda `OTA,<mac>` por LoRa. El nodo marca una bandera
  en NVS (`otaTakePending()`, clave `otapend`, independiente de `CFG_MAGIC`) y
  reinicia. `runOtaModeIfPending()` (antes de la radio) levanta la **WiFi de
  mantenimiento** (`cfg.otaSsid`/`otaPass`, se fija en el portal), descarga,
  verifica el SHA-256 mientras escribe la partición OTA libre y reinicia. Si la
  WiFi no conecta en 30 s o no hay red → apaga WiFi y sigue el arranque LoRa.
- **WiFi de mantenimiento — IP fija opcional:** el portal también acepta
  **IP fija + Gateway/Máscara/DNS 1/DNS 2** (vacío = DHCP). Con IP fija hay
  que dar el DNS explícito o el nodo queda sin resolver `github.com`
  (`wifiConfigStaticIfSet()`, mismo bug ya corregido en
  `nodeIO_master v1.5.3`).
- La partición por defecto de la placa (`default_8MB.csv`) ya es **dual-OTA**
  (app0/app1 de 3.19 MB); no hay que tocarla.
- **Chequeo manual (banco / puesta en marcha)**, sin esperar el comando LoRa
  del maestro — ambos reusan `runOtaCheckNow()` y muestran el progreso en el
  OLED (`otaOled()`):
  - **F2 mantenido 4-5s** (modo normal, toque corto cicla las pantallas de
    diagnóstico por canal).
  - **Comando por Serial/USB** (115200 baud): escribir `buscar actualizacion`
    (o `ota`) + Enter. Útil con el nodo solo conectado por USB, o para
    automatizarlo desde un script.
  - Ambos requieren `cfg.otaSsid` configurada en el portal; si no, avisan por
    OLED/Serial en vez de intentar conectar.

> El **último flasheo por USB** debe llevar el cliente OTA **y** la WiFi de
> mantenimiento configurada en el portal; a partir de ahí, sin cable.

## 6. Cómo extender

**Añadir un comando LoRa:** en `lora_proto.cpp::handleFrame()`, añade una rama
`else if (!strcmp(t_cmd, "XX"))`, parsea args con `strtok_r(nullptr, ",", &save)`
y responde con `reply(src, t_seq, body)`. Documenta en `PROTOCOL.md`.

**Añadir un campo de configuración:**
1. Campo en `struct NodeConfig` (`node_config.h`).
2. Default en `configFactory()` y **sube `CFG_MAGIC`** en `node_config.cpp`
   (invalida blobs viejos con layout distinto).
3. Input en el formulario (`portal.cpp::buildPage()`) y parseo en `handleSave()`.

**Cambiar la pantalla:** `drawStatusScreen()` / `drawPortalScreen()` /
`drawWaitAdoptScreen()` en `main.cpp` (API `SSD1306Wire`: `clear()`,
`setFont(ArialMT_Plain_10)`, `drawString()`, `drawXbm()`, `display()`).

---

## 7. Relación con nodeIO_master

`nodeIO_master` es la **pasarela LoRa ↔ Modbus** (servidor TCP :502 / RTU): descubre
y adopta nodos, los sondea y publica su IO como el **MAPA A** del contrato
`../ORCHESTRATION/REGISTER_MAP.md`; si pierde su tabla la reconstruye con `ROLLCALL`.
Solo comparte con este proyecto `src/io.{h,cpp}` y `src/images.h` **byte a byte**
(mantener en sync a mano). Todo lo demás diverge: aquí `lora_proto.*` +
`node_config.*` + `portal.*`; allí `lora_master.*` + `master_config.*` +
`portal_master.*` + `modbus_gw.*` + `net_master.*`.

---

## 8. Notas de hardware

- **GPIO45 / GPIO46** (relés 3/4) son *strapping pins* del ESP32-S3 (VDD_SPI /
  boot). `ioInit()` los deja en el estado seguro; el hardware externo no debe
  forzarlos durante el arranque/flasheo.
- AI1–AI4 = GPIO2–5 = **ADC1** (funciona con WiFi activo; ADC2 no se usa).
- `PIN_PWR_MGM` (GPIO36 = VEXT) se pone LOW en `ioInit()` para habilitar el riel
  externo (equivalente a `heltec_ve(true)`).
- `heltec_setup()` hace su propio `Serial.begin(115200)` e inicializa la OLED
  (`display.init/flipScreenVertically/setContrast`).
