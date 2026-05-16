// ============================================================================
// net_wifi.cpp — non-blocking WiFi for ESP8266
// ============================================================================
#include "net_wifi.h"
#include "secrets.h"
#include "config.h"
#include "state.h"

#include <ESP8266WiFi.h>
#include <time.h>   // configTime, used to kick off NTP after WiFi UP

namespace net_wifi {

// We hold the event handler objects globally so they outlive begin().
static WiFiEventHandler s_onGotIp;
static WiFiEventHandler s_onConnected;
static WiFiEventHandler s_onDisconnected;

static uint32_t s_last_reconnect_attempt = 0;

static void onConnected(const WiFiEventStationModeConnected& evt) {
    Serial.printf("[wifi] associated to %s (ch=%d)\n",
                  evt.ssid.c_str(), evt.channel);
}

static void onGotIp(const WiFiEventStationModeGotIP& evt) {
    Serial.printf("[wifi] got IP: %s  gw=%s  mask=%s\n",
                  evt.ip.toString().c_str(),
                  evt.gw.toString().c_str(),
                  evt.mask.toString().c_str());
    state::g.net_status = state::NetStatus::WIFI_OK;

    // Kick off NTP sync — non-blocking, the time becomes valid in a
    // few seconds.
    //
    // IMPORTANT: configTime(tz_string, ...) is unstable across ESP8266
    // Arduino core versions — older cores treat the TZ string as just
    // an arbitrary token and the resulting time is UTC. Symptom: a daily
    // reboot scheduled at 04:00 fires at 04:00 UTC = 06:00 French summer
    // time. The portable fix is to sync NTP with offset 0 (= UTC) and then
    // apply the timezone via the standard POSIX setenv("TZ", ...)+tzset()
    // mechanism, which works on every libc including newlib-based ESP cores.
    configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);
    setenv("TZ", NTP_TZ, 1);
    tzset();
    Serial.printf("[wifi] NTP sync requested, TZ=%s\n", NTP_TZ);
}

static void onDisconnected(const WiFiEventStationModeDisconnected& evt) {
    // The reason codes are useful for debug. Common ones:
    //   2 (AUTH_EXPIRE), 8 (ASSOC_LEAVE), 200 (BEACON_TIMEOUT),
    //   201 (NO_AP_FOUND), 202 (AUTH_FAIL), 203 (ASSOC_FAIL),
    //   204 (HANDSHAKE_TIMEOUT)
    Serial.printf("[wifi] DISCONNECTED from %s, reason=%d\n",
                  evt.ssid.c_str(), evt.reason);
    state::g.net_status = state::NetStatus::DISCONNECTED;
}

void begin() {
    Serial.printf("[wifi] init, target SSID=%s\n", WIFI_SSID);

    // Pure station mode — never start an AP from this device.
    WiFi.persistent(false);   // don't wear out flash with credential rewrites
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);  // belt + suspenders, on top of our manual logic

    s_onConnected    = WiFi.onStationModeConnected(onConnected);
    s_onGotIp        = WiFi.onStationModeGotIP(onGotIp);
    s_onDisconnected = WiFi.onStationModeDisconnected(onDisconnected);

    // Set a sensible hostname — appears in DHCP leases.
    WiFi.hostname(DEVICE_NAME);

    // Kick off the first connection (non-blocking).
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    s_last_reconnect_attempt = millis();
}

void loop() {
    if (WiFi.status() == WL_CONNECTED) return;

    const uint32_t now = millis();
    if ((now - s_last_reconnect_attempt) < WIFI_RECONNECT_MS) return;

    Serial.println("[wifi] reconnect attempt...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    s_last_reconnect_attempt = now;
}

bool isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

} // namespace net_wifi
