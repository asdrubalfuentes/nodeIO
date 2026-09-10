#include "ota_update.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <mbedtls/sha256.h>

// OTA "GitHub Releases pull". Ver ota_update.h.

namespace {

void say(ota::ProgressCb cb, ota::Phase p, int pct, const char *d) {
	if (cb) cb(p, pct, d);
}

// Rellena `url` con  https://github.com/<owner>/<repo>/releases/latest/download/<asset>
void relUrl(const ota::Config &cfg, const char *asset, char *url, size_t n) {
	snprintf(url, n, "https://github.com/%s/%s/releases/latest/download/%s",
	         cfg.owner, cfg.repo, asset);
}

// GET de texto corto (version.txt / firmware.sha256). "" en error.
String getText(const char *url, uint16_t timeoutMs) {
	WiFiClientSecure sec;
	sec.setInsecure();                       // sin CA fija (igual que LoraSenderAysafi)
	HTTPClient http;
	http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // GitHub 302 -> assets host
	http.setTimeout(timeoutMs);
	http.setReuse(false);
	if (!http.begin(sec, url)) return "";
	int code = http.GET();
	String body = (code == HTTP_CODE_OK) ? http.getString() : String();
	http.end();
	body.trim();
	return body;
}

int parseSemver(const char *s, int v[3]) {
	while (*s == 'v' || *s == 'V' || *s == ' ') s++;
	v[0] = v[1] = v[2] = 0;
	return sscanf(s, "%d.%d.%d", &v[0], &v[1], &v[2]);  // ignora sufijo "-xxx"
}

}  // namespace

namespace ota {

bool isNewer(const char *latest, const char *current) {
	int l[3], c[3];
	if (parseSemver(latest, l) != 3 || parseSemver(current, c) != 3) return false;
	for (int i = 0; i < 3; i++) if (l[i] != c[i]) return l[i] > c[i];
	return false;
}

Result check(const Config &cfg, ProgressCb cb) {
	Result r;
	say(cb, Phase::Check, 0, "consultando version");
	if (WiFi.status() != WL_CONNECTED) {
		snprintf(r.error, sizeof(r.error), "sin WiFi");
		say(cb, Phase::Error, 0, r.error);
		return r;
	}
	char url[160];
	relUrl(cfg, "version.txt", url, sizeof(url));
	String latest = getText(url, cfg.httpTimeoutMs);
	if (!latest.length()) {
		snprintf(r.error, sizeof(r.error), "no se pudo leer version.txt");
		say(cb, Phase::Error, 0, r.error);
		return r;
	}
	r.ok = true;
	strncpy(r.latest, latest.c_str(), sizeof(r.latest) - 1);
	r.hasUpdate = isNewer(latest.c_str(), cfg.currentVersion);
	say(cb, r.hasUpdate ? Phase::Check : Phase::UpToDate, 0,
	    r.hasUpdate ? r.latest : "al dia");
	return r;
}

Result apply(const Config &cfg, ProgressCb cb) {
	Result r = check(cfg, cb);
	if (!r.ok || !r.hasUpdate) return r;

	if (ESP.getFreeHeap() < cfg.minFreeHeap) {
		snprintf(r.error, sizeof(r.error), "heap bajo (%u)", (unsigned)ESP.getFreeHeap());
		r.ok = false; say(cb, Phase::Error, 0, r.error); return r;
	}

	// 1) SHA-256 esperado
	char url[160];
	relUrl(cfg, "firmware.sha256", url, sizeof(url));
	String expected = getText(url, cfg.httpTimeoutMs);
	expected.toLowerCase();
	if (expected.length() != 64) {
		snprintf(r.error, sizeof(r.error), "sha256 no disponible");
		r.ok = false; say(cb, Phase::Error, 0, r.error); return r;
	}

	// 2) binario
	relUrl(cfg, "firmware.bin", url, sizeof(url));
	WiFiClientSecure sec; sec.setInsecure();
	HTTPClient http;
	http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
	http.setTimeout(cfg.httpTimeoutMs);
	http.setReuse(false);
	if (!http.begin(sec, url) || http.GET() != HTTP_CODE_OK) {
		snprintf(r.error, sizeof(r.error), "descarga firmware.bin fallo");
		r.ok = false; http.end(); say(cb, Phase::Error, 0, r.error); return r;
	}
	int len = http.getSize();                       // -1 si no hay Content-Length
	if (!Update.begin(len > 0 ? (size_t)len : UPDATE_SIZE_UNKNOWN)) {
		snprintf(r.error, sizeof(r.error), "Update.begin: %s", Update.errorString());
		r.ok = false; http.end(); say(cb, Phase::Error, 0, r.error); return r;
	}
	say(cb, Phase::Download, 0, r.latest);

	mbedtls_sha256_context sha;
	mbedtls_sha256_init(&sha);
	mbedtls_sha256_starts(&sha, 0);

	WiFiClient *stream = http.getStreamPtr();
	uint8_t buf[1024];
	size_t written = 0;
	int lastPct = -1;
	uint32_t idleSince = millis();
	bool fail = false;

	while (true) {
		size_t avail = stream->available();
		if (avail) {
			int n = stream->readBytes(buf, avail < sizeof(buf) ? avail : sizeof(buf));
			if (n <= 0) { fail = true; break; }
			if ((int)Update.write(buf, n) != n) { fail = true; break; }
			mbedtls_sha256_update(&sha, buf, n);
			written += n;
			idleSince = millis();
			if (len > 0) {
				int pct = (int)((written * 100ULL) / (size_t)len);
				if (pct != lastPct) { lastPct = pct; say(cb, Phase::Download, pct, nullptr); }
			}
			if (len > 0 && written >= (size_t)len) break;
		} else {
			if (!stream->connected()) break;
			if (millis() - idleSince > cfg.httpTimeoutMs) { fail = true; break; }
			delay(1);
		}
	}
	http.end();

	uint8_t digest[32]; char hex[65];
	mbedtls_sha256_finish(&sha, digest);
	mbedtls_sha256_free(&sha);
	for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", digest[i]);
	hex[64] = 0;

	if (fail || (len > 0 && written != (size_t)len)) {
		Update.abort();
		snprintf(r.error, sizeof(r.error), "descarga incompleta (%u)", (unsigned)written);
		r.ok = false; say(cb, Phase::Error, 0, r.error); return r;
	}
	say(cb, Phase::Verify, 100, nullptr);
	if (expected != String(hex)) {
		Update.abort();
		snprintf(r.error, sizeof(r.error), "sha256 no coincide");
		r.ok = false; say(cb, Phase::Error, 0, r.error); return r;
	}
	if (!Update.end(true) || !Update.isFinished()) {
		snprintf(r.error, sizeof(r.error), "Update.end: %s", Update.errorString());
		r.ok = false; say(cb, Phase::Error, 0, r.error); return r;
	}

	r.updated = true;
	say(cb, Phase::Done, 100, "reiniciando");
	delay(400);
	ESP.restart();
	return r;   // inalcanzable
}

Result run(const Config &cfg, ProgressCb cb) {
	Result r = check(cfg, cb);
	if (r.ok && r.hasUpdate) return apply(cfg, cb);
	return r;
}

}  // namespace ota
