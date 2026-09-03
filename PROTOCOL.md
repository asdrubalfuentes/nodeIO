# Protocolo LoRa - Nodo IO Aysafi

Radio: SX1262 (RadioLib). Ambos extremos deben usar los **mismos** parámetros
(se configuran en el portal cautivo). Por defecto:
`915.0 MHz, BW 125 kHz, SF 9, CR 4/5, sync word 0x34, TX 14 dBm, preámbulo 8`.

## Trama

```
"<dst>,<src>,<seq>,<cmd>[,<arg>...]"   (texto ASCII)
<CRC32 : 4 bytes little-endian>        (binario, CRC de ese texto)
'\n'
```

- `dst` / `src`: direcciones de 1 byte en decimal. `255` = broadcast.
- `seq`: contador 0-255 del maestro; el nodo lo devuelve igual en la respuesta.
- CRC32: algoritmo estándar (lib `bakercp/CRC32`), calculado sobre el texto,
  añadido como 4 bytes crudos en orden little-endian.
- El nodo procesa los comandos de operación solo si: CRC correcto **y** está
  **adoptado** **y** (`dst` == su dirección o `255`) **y** (`src` == dirección de
  maestro aceptada, o ésta es `0` = cualquiera). Si no, la descarta en silencio.
- Los comandos de **aprovisionamiento** (`DISC` / `ROLLCALL` / `ADOPT` /
  `RELEASE`) saltan el filtro de dirección (llegan a `dst 255`).
- `src = 0` en una trama enviada por el nodo = "nodo sin adoptar".
- Un nodo adoptado **nunca se des-adopta solo**. Solo `RELEASE` lo libera. Si el
  maestro pierde su tabla, la reconstruye con `ROLLCALL` (ver abajo).

## Aprovisionamiento (descubrimiento y adopción)

Un nodo recién flasheado arranca **sin adoptar**: no responde a `RD/WR/WP/PING`,
solo escucha en el canal por defecto y muestra su MAC en la OLED.

| cmd | sentido | efecto |
|---|---|---|
| `DISC` | maestro -> `255` | cada nodo **sin adoptar** responde `IAM,<mac>,<fw>` tras un retardo aleatorio 0-800 ms (evita colisiones) |
| `ROLLCALL` | maestro -> `255` | cada nodo **adoptado** responde `HERE,<mac>,<addr>,<masterAddr>` tras un retardo aleatorio 0-800 ms. No cambia nada del nodo. Sirve para que un maestro que perdió su tabla la reconstruya |
| `ADOPT,<mac>,<addr>,<freq>,<sf>,<bw>,<cr>,<sync>,<pwr>` | maestro -> `255` | el nodo cuya MAC coincide guarda dirección y canal LoRa, responde `ACK,<mac>` y **reinicia** ya adoptado |
| `RELEASE,<mac>` | maestro -> `addr` o `255` | el nodo cuya MAC coincide vuelve a "sin adoptar", responde `ACK,<mac>` y reinicia |

`HERE,<mac>,<addr>,<masterAddr>`: `<addr>` es la dirección LoRa asignada al nodo;
`<masterAddr>` es el maestro al que cree pertenecer (`cfg.masterAddr`, `0` =
cualquiera). El maestro solo incorpora a su tabla los `HERE` cuyo `<masterAddr>`
es `0` o coincide con el suyo, y solo si la dirección está libre. Con
`cfg.adoptTimeoutS > 0` el nodo además emite una baliza `HERE` a `255` (con
`src` = su dirección, `seq` = `0`) tras ese tiempo sin tramas del maestro, y
re-arma el contador — **sin** liberar la adopción.

`<mac>` = `idUnico` = 12 hex del efuse (`ESP.getEfuseMac()`). El canal por defecto
(915/125/SF9/CR5/0x34/14 dBm) es idéntico en nodo y maestro para que `DISC`
funcione de fábrica; `ADOPT` fija el canal definitivo.

## Comandos (maestro -> nodo, solo si adoptado)

| cmd | argumentos | respuesta |
|---|---|---|
| `RD` | — | `ST,...` |
| `WR` | `<r1>,<r2>,<r3>,<r4>` cada uno `0` / `1` / `-` (sin cambio) | `ST,...` |
| `WP` | `<idx 1..4>,<ms>` (pulso; respeta modo/enable) | `ST,...` o `ERR,...` |
| `PING` | — | `PONG,<uptime_s>,<rssi_dBm>` |

## Respuestas (nodo -> maestro)

```
ST,<a1>,<a2>,<a3>,<a4>,<d1>,<d2>,<d3>,<d4>,<o1>,<o2>,<o3>,<o4>
```
- `a1..a4`: entradas analógicas, cuentas ADC crudas 0-4095 (GPIO2/3/4/5).
- `d1..d4`: entradas digitales, nivel 0/1 (GPIO38/39/40/41, `INPUT_PULLUP`).
- `o1..o4`: relés (GPIO33/34/45/46). `0`/`1`, o `x` si el relé está deshabilitado.

```
ERR,<FMT|CMD|DIS|RANGE>
```

## Ejemplo

Maestro pide todo el IO al nodo 1 (maestro = dir 0, seq 42):

```
texto:  "1,0,42,RD"
en aire: 31 2C 30 2C 34 32 2C 52 44 | <crc32 LE> | 0A
```

Respuesta del nodo 1:

```
"0,1,42,ST,2048,10,4095,0,1,0,1,1,0,0,0" | <crc32 LE> | 0A
```

## Configuración (portal cautivo)

Pulsación larga (~3 s) de `BUTTON_1` (GPIO47) -> se levanta un SoftAP
(`NodoIO-Setup` / `aysafi1234` por defecto) con formulario web para ajustar WiFi
del AP, parámetros LoRa y comportamiento/habilitación de los relés, ver la MAC y
el estado de adopción, y "Anular adopción". Normalmente **no hace falta**: la
dirección y el canal los fija el maestro al adoptar.

El proyecto hermano `nodeIO_master` es la **pasarela LoRa <-> Modbus** (servidor
**TCP :502** sobre WiFi, o RTU de respaldo): descubre y adopta nodos desde su
propio portal, y si pierde su tabla la reconstruye con `ROLLCALL`. Luego los
sondea (`RD`) y publica su IO como el **MAPA A** del contrato
`../ORCHESTRATION/REGISTER_MAP.md`. Ver `nodeIO_master/user manual.md`.
