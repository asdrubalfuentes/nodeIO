/**
 * ota_update.h  -  OTA "GitHub Releases pull" para los firmwares Aysafi.
 *
 * Modelo (igual que LoraSenderAysafi):
 *   - CI publica un Release "latest" con  firmware.bin + version.txt + firmware.sha256
 *     al empujar un tag vX.Y.Z sobre main  (.github/workflows/release.yml).
 *   - El equipo, con WiFi ya conectada, consulta version.txt; si es mayor que la
 *     versión embebida, descarga firmware.bin verificando el SHA-256 sobre la
 *     marcha y, si cuadra, flashea la partición OTA libre y reinicia.
 *
 * Copia este par de archivos a  <proyecto>/src/  (o lib/aysafi_ota/) y llama a
 * ota::run() una vez tras conectar la WiFi.  Requiere arduino-esp32 (HTTPClient,
 * Update, mbedtls) y una tabla de particiones con app0/app1 (todas las placas
 * del sistema ya la tienen: min_spiffs.csv y default_8MB.csv son dual-OTA).
 */
#pragma once
#include <Arduino.h>

namespace ota {

struct Config {
	const char *owner;          // "asdrubalfuentes"
	const char *repo;           // "nodeIO" | "nodeIO_master" | "miHMI"
	const char *currentVersion; // "1.2.0"  (X.Y.Z; se tolera "v1.2.0" y "1.2.0-xyz")
	uint32_t    minFreeHeap = 45000;  // no arrancar la descarga por debajo de esto
	uint16_t    httpTimeoutMs = 15000;
};

enum class Phase : uint8_t { Check, UpToDate, Download, Verify, Flash, Done, Error };

// pct: 0..100 (solo en Download). detail: texto corto para log/pantalla.
typedef void (*ProgressCb)(Phase phase, int pct, const char *detail);

struct Result {
	bool  ok        = false;   // true si no hubo error (aunque no hubiera update)
	bool  updated   = false;   // true justo antes de reiniciar (rara vez se ve)
	bool  hasUpdate = false;   // latest > current
	char  latest[24] = {0};
	char  error[64]  = {0};
};

// Compara "X.Y.Z" numéricamente (no lexicográfico). Tolera 'v' inicial y sufijo '-...'.
bool isNewer(const char *latest, const char *current);

// Solo consulta version.txt. No descarga nada.
Result check(const Config &cfg, ProgressCb cb = nullptr);

// Descarga + verifica SHA-256 + flashea + ESP.restart(). No retorna si tiene éxito.
Result apply(const Config &cfg, ProgressCb cb = nullptr);

// check() y, si hay versión nueva, apply(). Conveniencia para el arranque.
Result run(const Config &cfg, ProgressCb cb = nullptr);

}  // namespace ota
