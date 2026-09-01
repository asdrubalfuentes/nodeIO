#pragma once
#include <Arduino.h>

// Captive configuration portal (SoftAP + DNS + web form).
// Fields map 1:1 onto NodeConfig; "Guardar" persists and reboots.
extern bool portalActive;

void   portalStart();   // bring up SoftAP, DNS and web server (uses cfg.apSsid/apPass)
void   portalStop();    // tear everything down
void   portalLoop();    // call from loop() while portalActive
String portalIP();      // SoftAP IP as text (for the OLED)
