# Changelog — nodeIO

Versión del canal OTA: `MAJOR.MINOR.PATCH` (semver numérico). El firmware embebe
`FW_SEMVER`; el CI lo sobreescribe desde el tag `vX.Y.Z`.

## 1.3.0 — OTA remota por comando LoRa + identidad NVS separada

- **Identidad NVS separada**: `adopted`, `nodeAddr`, `masterAddr` y el **canal
  LoRa** (`freq/bw/sf/cr/sync/pwr`) se guardan además como **claves sueltas** en
  NVS, sin `magic`. Un futuro firmware que suba `CFG_MAGIC` (features nuevas) ya
  **no** deja al nodo "sin adoptar" ni fuera de su canal. Migración transparente:
  este flasheo no sube `CFG_MAGIC` (se queda en 0xA75AF106); el blob viejo sigue
  cargando y las claves de identidad se escriben en el primer `configSave()`.
- **Comando LoRa `OTA,<mac>`** (`src/lora_proto.cpp`, salta el filtro de
  dirección como `DISC`/`ROLLCALL`): el nodo marca una bandera en NVS
  (`otapend`, independiente de `CFG_MAGIC`), responde `ACK,<mac>,OTA` y reinicia.
- **Modo OTA en el arranque** (`runOtaModeIfPending()` en `main.cpp`, antes de la
  radio LoRa): levanta la **WiFi de mantenimiento** (`cfg.otaSsid`/`otaPass`),
  descarga `firmware.bin` de GitHub Releases, verifica el **SHA-256** contra
  `firmware.sha256` mientras escribe la partición OTA libre, y reinicia. Si la
  WiFi no conecta en 30 s o no hay red configurada, apaga la WiFi y sigue el
  arranque LoRa normal. Progreso en el OLED.
- `node_config`: `CFG_MAGIC` 105 → **106**; nuevos campos `otaSsid[33]` /
  `otaPass[65]`. Editables en el **portal cautivo** (fieldset "WiFi de
  mantenimiento (OTA remota)"). Vacío = OTA remota deshabilitada.
- `src/ota_update.{h,cpp}`: módulo común (`ORCHESTRATION/tools/ota/`).
- `.github/workflows/release.yml`: tag `vX.Y.Z` sobre `main` → Release `latest`
  con los 3 assets. Sin secrets.
- `platformio.ini`: `build_flags = ${sysenv.EXTRA_BUILD_FLAGS}` para
  `FW_VERSION_OVERRIDE`.
- `PROTO_FW` `1.2026.005` → `1.2026.006`; `FW_VERSION` → `V1.2026.006`.

Lado maestro (`nodeIO_master` ≥ 1.2.0): botón **OTA** por fila de nodo en el
portal → `masterOtaTrigger(slot)` envía `OTA,<mac>` y espera el ACK.

> **Desde este flasheo el nodo se actualiza sin cable.** El último flasheo por
> USB debe llevar esta versión (o posterior) **y** la WiFi de mantenimiento
> configurada en el portal.
