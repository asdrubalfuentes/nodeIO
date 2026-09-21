# Changelog — nodeIO

Versión del canal OTA: `MAJOR.MINOR.PATCH` (semver numérico). El firmware embebe
`FW_SEMVER`; el CI lo sobreescribe desde el tag `vX.Y.Z`.

## 1.4.6 — oversampling por RMS en vez de promedio simple

- A pedido del usuario, tras confirmar con osciloscopio que la señal de los
  canales analógicos trae ruido real (no solo el propio del ADC):
  `ioReadAnalog()` combina las 64 muestras del oversampling por **RMS**
  (`sqrt(mean(x²))`) en lugar de la media aritmética, para quedarse con la
  medida "integral" de la señal ruidosa.
- Nota técnica dejada en el código: con ruido simétrico el RMS es siempre
  ≥ la media real (por la desigualdad de Jensen); para la amplitud de ruido
  observada en banco (decenas de cuentas sobre una base de cientos) ese
  sesgo es menor a 1 cuenta — despreciable frente al ruido que se filtra.
- Sin cambios de `CFG_MAGIC` ni de protocolo.

## 1.4.5 — comando serial "medir" (diagnóstico crudo/pre-filtro/post-filtro)

- Escribir `medir` por Serial/USB (115200 baud) alterna un stream que imprime,
  cada 300 ms, las 3 etapas de cada canal: el crudo del ADC ya oversampleado
  (`ioReadAnalog()`), el valor escalado+clamped **antes** del EMA
  (`chLive[].engRaw`, nuevo) y el valor final filtrado (`chLive[].eng`). Útil
  en banco para comparar contra un multímetro en las entradas y ver cuánto
  está aportando cada etapa del "doble filtraje" de 1.4.4. Se desactiva
  escribiendo `medir` de nuevo.
- Sin cambios de `CFG_MAGIC` ni de protocolo.

## 1.4.4 — fix: filtro EMA inestable (lecturas ruidosas)

- **Reporte de campo:** "el nodo lee muy ruidoso, el filtro parece inestable".
  Causa: `channelsService()` (escalado + EMA) se llamaba una vez por cada
  vuelta de `loop()`, y `loop()` no tiene `delay()` — corre miles de
  veces/segundo cuando está libre, y mucho más lento cuando `loraLoop()` tiene
  trabajo de radio/SPI. La fórmula del EMA (`eng_filt += (x - eng_filt) *
  (1 - filtro/101)`, `REGISTER_MAP.md` §5) es una recurrencia **por llamada**,
  heredada de cuando corría en el LOGO! a un ciclo de scan más o menos fijo:
  con el filtro por defecto (15, alfa≈0.85 por llamada) y miles de
  llamadas/seg, la constante de tiempo real caía a microsegundos — el filtro
  prácticamente no filtraba nada, y además su "fuerza" cambiaba según cuánto
  trabajo tuviera `loraLoop()` en cada momento (de ahí la sensación de lectura
  inestable, no solo ruidosa).
- Fix: `channelsService()` ahora corre a un período fijo (150 ms ≈ 6.7
  lecturas/seg, holgado para nivel/caudal). Cada llamada representa un paso de
  tiempo constante, así que el filtro configurado (0..100) se comporta igual
  siempre, sin importar la carga del loop.
- **Bug adicional encontrado de paso:** faltaba el `clamp()` del crudo
  escalado a `[engMin,engMax]` que exige la fórmula del contrato
  (`REGISTER_MAP.md` §5) antes de filtrar — un pico de ruido del ADC fuera de
  `[rawMin,rawMax]` (lazo 4-20mA sin conectar, transitorio) podía mandar la
  lectura (y el propio EMA) fuera del rango de ingeniería configurado. Ya se
  agregó.
- **Segundo hallazgo (verificado en banco):** con el voltaje de entrada
  fijo por multímetro (1.7988V / 0.8879V, sin variar), `analogRead()` de una
  sola muestra igual saltaba bastante — ruido propio del ADC del ESP32-S3, no
  de la señal. `ioReadAnalog()` ahora promedia 64 lecturas consecutivas
  (oversampling) antes de escalar, reduciendo ese ruido en ~8x (√64). Con
  esto hay **doble filtraje**: oversampling del crudo (ruido instantáneo del
  ADC) + EMA a período fijo (ruido/transitorios residuales en el tiempo).
- Sin cambios de `CFG_MAGIC` ni de protocolo — solo firmware, mismo
  comportamiento configurado desde el portal.

## 1.4.3 — página "En vivo" en el portal cautivo

- Nueva ruta `/live` (enlazada desde la página de configuración): muestra en
  el celular, de solo lectura y auto-refrescada cada 2 s, exactamente lo
  mismo que el nodo ya manda por LoRa en la trama `ST` — crudo e ingeniería
  por canal, acumulados día/mes, alarmas activas, estado de DI/relés y
  estadísticas del enlace (RSSI, último comando, antigüedad, rx/crc/tx).
  Reutiliza las variables vivas de `channels.h`/`io.h`/`lora_proto.h`, no
  agrega ningún estado nuevo. Costo: ~4.5 KB de flash, RAM sin cambios.
- Solo disponible mientras el portal está levantado (mantener BUTTON_1 3 s),
  que **pausa el LoRa** igual que antes — no se cambió ese comportamiento.
  Ver alternativas evaluadas (incl. BLE) en `ORCHESTRATION`.

## 1.4.2 — IP fija + DNS para la WiFi de mantenimiento

- Portal cautivo: la sección "WiFi de mantenimiento (OTA remota)" gana **IP
  fija** (vacío = DHCP), **Gateway**, **Máscara**, **DNS 1**, **DNS 2**.
  Con DHCP no cambia nada (el router entrega el DNS solo); con IP fija, sin
  estos campos, `WiFi.config()` dejaba `dns1`/`dns2` en `0.0.0.0` y el nodo
  quedaba sin ningún DNS — rompía la resolución de `github.com` para el OTA.
  Mismo bug ya corregido en `nodeIO_master v1.5.3` (ver `ORCHESTRATION`).
- DNS 1 vacío con IP fija configurada usa el propio Gateway (la mayoría de
  los routers hacen de proxy DNS); DNS 2 vacío usa `8.8.8.8` de respaldo.
- `node_config.h/.cpp`: `otaIp/otaGw/otaMask/otaDns1/otaDns2` (magic
  `0xA75AF108`). `main.cpp`: `wifiConfigStaticIfSet()` compartido entre el
  chequeo manual (F2/serial) y el arranque en modo OTA por comando LoRa.

## 1.4.1 — comando serial "buscar actualizacion"

- Alternativa de banco al F2 mantenido 4-5s: escribir `buscar actualizacion`
  (o `ota`) por Serial/USB dispara el mismo `runOtaCheckNow()` (requiere
  `cfg.otaSsid` configurado en el portal; si no, avisa por OLED como ya
  hacía el botón). Útil con el nodo conectado solo por USB o para
  automatizar el chequeo desde un script.

## 1.4.0 — escalado/totalizador/alarma en el nodo (cambio de rumbo)

El nodo ahora escala, filtra, totaliza y discretiza alarma de nivel/caudal —
antes era trabajo del PLC. Ver `ORCHESTRATION` para el detalle del contrato.

- `channels.{h,cpp}` (nuevo): escala raw→ingeniería + filtro EMA (misma fórmula
  del contrato), totalizador día/mes en m³ (con factor `k` por unidad),
  alarmas de umbral alto/bajo. Acumulados persistidos en NVS (flush cada 60s).
  El nodo **nunca** cierra día/mes solo (sin hora confiable) — expone
  `channelsCloseDay()`/`CloseMonth()` para que el gateway lo dispare.
- `node_config`: `ChannelCfg[4]` (nombre, calibración 4-20mA, unidad, filtro,
  totalizar día/mes, límites de alarma) + nombres de DI/DO. `CFG_MAGIC`
  106→107 (identidad de emparejamiento sobrevive, como siempre).
- Protocolo LoRa (`PROTO_FW` 1.2026.007): `ST` ahora lleva nivel/caudal
  escalados, los 4 acumulados y `almBits`; comandos nuevos `CD`/`CM` (cierre
  de día/mes, los dispara el gateway).
- Portal cautivo: configuración completa por canal (nivel/caudal) + nombres
  DI/DO.
- OLED: pantallas de diagnóstico por canal (barra 0-100% + valor escalado),
  navegables con F2 corto. F2 mantenido 4-5s fuerza un chequeo OTA ya.
- Caudal fijo en **m³/h** (antes L/s) en toda la cadena.

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
