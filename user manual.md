# Manual de uso — Nodo IO (Aysafi)

Placa: **Heltec WiFi LoRa 32 V3**. Firmware: `Remote IO Node`.

El nodo es un módulo de entradas/salidas remoto: escucha por radio LoRa y
responde a las peticiones de un **maestro**, entregando el valor de sus entradas
y accionando sus relés.

---

## 1. Conexiones

| Señal | Pin | Notas |
|---|---|---|
| Entradas analógicas AI1–AI4 | GPIO2 / GPIO3 / GPIO4 / GPIO5 | 0–3.3 V, lectura cruda 0–4095 |
| Entradas digitales DI1–DI4 | GPIO38 / GPIO39 / GPIO40 / GPIO41 | con pull-up interno; activo a masa |
| Relés RO1–RO4 | GPIO33 / GPIO34 / GPIO45 / GPIO46 | salida activa en alto |
| Botón BUTTON_1 | GPIO47 | mantener pulsado → entra al portal de configuración |
| Botón PRG (integrado) | GPIO0 | conmuta el relé 1 localmente |
| USB-C | — | consola serie a 115200 baudios |

> Los relés 3 y 4 usan pines de arranque del ESP32-S3. El cableado del relé no
> debe forzar esos pines a un nivel fijo mientras la placa arranca o podría no
> encender / no programarse.

---

## 2. Primer encendido y adopción

Un nodo recién flasheado está **SIN ADOPTAR**: la OLED muestra su **MAC**
(`idUnico`) y "Esperando maestro...". No responde a peticiones normales; solo
espera a que un **Master IO** lo descubra.

1. Enciende el nodo cerca del Master IO.
2. En el portal del Master IO: **Descubrir nodos → Buscar**. Aparece la MAC de
   este nodo.
3. Asígnale una **dirección LoRa** y un nombre y pulsa "Agregar". El nodo guarda
   la dirección y el canal LoRa que le envía el maestro y **reinicia adoptado**.
4. A partir de ahí la OLED muestra `addr:N` y el nodo responde al polling.

Para desvincularlo: "Quitar" en el portal del Master, o "Anular adopción" en el
portal del propio nodo (BUTTON_1 largo). Vuelve a estado SIN ADOPTAR.

---

## 3. Operación normal

La pantalla OLED muestra:

```
NodoIO  addr:1            <- nombre y dirección del nodo
cmd:RD  rssi:-92          <- último comando recibido y nivel de señal (dBm)
AI 2048 10 4095 0         <- entradas analógicas (0–4095)
DI 1011   RO 1000         <- entradas digitales / estado de relés
rx:57 crc:0 tx:57         <- tramas válidas / con error de CRC / respuestas enviadas
```

- El nodo solo responde a tramas correctas dirigidas a su dirección (o a
  broadcast) y, si se configuró una dirección de maestro, solo a ese maestro.
- El botón **PRG** conmuta el relé 1 de forma local (además de por LoRa).
- Un relé marcado como **`x`** en pantalla o en la respuesta está deshabilitado
  en la configuración.

### Qué puede pedir el maestro

| Petición | Efecto |
|---|---|
| Leer IO | el nodo devuelve las 4 analógicas, las 4 digitales y los 4 relés |
| Escribir relés | fija RO1–RO4 (0 = abierto, 1 = cerrado, o "sin cambio") |
| Pulso de relé | cierra un relé durante un tiempo y lo vuelve a abrir |
| Ping | prueba de enlace; el nodo responde con su tiempo de encendido y el RSSI |

En modo **pulso** (configurable), una orden de "cerrar" genera un pulso del ancho
configurado en lugar de quedar enclavado.

---

## 4. MODO CONFIG (portal cautivo)

### Cómo entrar
- **Mantener pulsado BUTTON_1 unos 3 segundos** (esté adoptado o no). La radio
  LoRa se detiene y se levanta una red WiFi propia. Se sale reiniciando la placa.
- Normalmente **no hace falta**: la dirección y el canal LoRa los fija el Master
  IO al adoptar el nodo.

### Cómo configurar
1. Con un móvil o PC, conéctate a la red WiFi:
   - **SSID:** `NodoIO-Setup`  ·  **Clave:** `aysafi1234` (valores de fábrica).
2. Normalmente se abre solo la página de configuración. Si no, abre el navegador
   en `http://192.168.4.1`.
3. La pantalla OLED muestra el SSID y la IP mientras el portal está activo.
4. Rellena los campos y pulsa **Guardar y reiniciar**.

### Campos

| Grupo | Campo | Descripción |
|---|---|---|
| Estado | MAC (idUnico) / Adopción | solo lectura; botón "Anular adopción" si está adoptado |
| Identidad | Dirección de este nodo | 1–254; normalmente la fija el maestro al adoptar |
| | Dirección de maestro aceptada | 0 = acepta cualquier maestro |
| | Nombre / Ubicación | texto libre para identificar el nodo |
| WiFi del portal | SSID / Clave | red que crea el nodo para mostrar el portal (clave vacía = abierta, si no mínimo 8) |
| LoRa | Frecuencia, Ancho de banda, SF, CR, Sync word, Potencia TX | **deben ser idénticos en el maestro**; el maestro los reescribe al adoptar |
| Relés | Habilitación R1–R4 | un relé no habilitado ignora las órdenes y aparece como `x` |
| | Estado seguro R1–R4 | nivel al que quedan los relés al arrancar y al terminar un pulso |
| | Modo | Enclavado o Pulso |
| | Ancho de pulso | milisegundos del pulso (modo Pulso) |

---

## 5. Valores de fábrica

| Parámetro | Valor |
|---|---|
| Dirección de nodo | 1 |
| Dirección de maestro | 0 (cualquiera) |
| WiFi portal | `NodoIO-Setup` / `aysafi1234` |
| Frecuencia | 915.0 MHz |
| Ancho de banda | 125 kHz |
| Spreading factor | 9 |
| Coding rate | 4/5 |
| Sync word | 0x34 |
| Potencia TX | 14 dBm |
| Relés habilitados | los 4 |
| Estado seguro | todos abiertos |
| Modo de relé | Enclavado |
| Ancho de pulso | 500 ms |

---

## 6. Problemas frecuentes

| Síntoma | Causa probable / solución |
|---|---|
| El maestro nunca recibe respuesta | parámetros LoRa distintos entre nodo y maestro; o dirección de nodo equivocada en el maestro |
| `crc:` sube en pantalla | interferencia o tramas de otro sistema; revisa sync word / frecuencia |
| Un relé no acciona | está deshabilitado en la configuración (aparece `x`) |
| No aparece la red `NodoIO-Setup` | no estás en MODO CONFIG: mantén BUTTON_1 ~3 s |
| No abre el portal solo | entra a mano a `http://192.168.4.1` |
| Quiero volver a fábrica | entra al portal y reescribe los campos con los valores de la sección 5 |

Consola serie (USB, 115200 baudios) imprime el arranque, los eventos de entrada
y los mensajes del portal.
