#include <WiFi.h>
#include <Arduino.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <DNSServer.h>

#include "wireless.h"
static IPAddress apIP(192, 168, 4, 1);
#define WIFI_AP_NAME "STAR_TRACKER"

void initWifiAP() {
  WiFi.begin();
  delay(500); // If not used, somethimes following command fails
  WiFi.mode( WIFI_AP );
  uint8_t protocol = (WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_AP, protocol));
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  uint8_t channel = 1;
  if(channel < 1 || channel > 13) {
    channel = 1;
  }
  log_i("Starting wifi %s on channel %i in mode %s", WIFI_AP_NAME, channel, protocol ? "bgn" : "b");
  WiFi.softAP(WIFI_AP_NAME, NULL, channel);
}

