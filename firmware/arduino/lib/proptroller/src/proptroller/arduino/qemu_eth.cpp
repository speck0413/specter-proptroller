// QEMU only (PROPTROLLER_QEMU_ETH, tools/qemu/eth, QEMU.md "Network"): the network on QEMU's OpenCores Ethernet
// instead of Wi-Fi, which QEMU does not emulate. Everything above the interface (lwIP sockets, the HTTP and
// WebSocket server, discovery, MQTT) is the board's own code. Never built for a board.
#if defined(ARDUINO) && defined(ESP32) && defined(PROPTROLLER_QEMU_ETH)
#include "qemu_eth.h"
#include <Arduino.h>
#include <esp_eth.h>
#include <esp_event.h>
#include <esp_netif.h>

namespace pt {

static volatile bool g_up = false;
static char g_ip[16] = "";

static void onIp(void*, esp_event_base_t, int32_t, void* data) {
  const ip_event_got_ip_t* e = (const ip_event_got_ip_t*)data;
  snprintf(g_ip, sizeof g_ip, IPSTR, IP2STR(&e->ip_info.ip));
  g_up = true;
}

bool qemuEthBegin(uint32_t wait_ms) {
  esp_netif_init();
  esp_event_loop_create_default();  // may exist already (the core's Wi-Fi events): the error is harmless
  esp_netif_config_t ncfg = ESP_NETIF_DEFAULT_ETH();
  esp_netif_t* netif = esp_netif_new(&ncfg);
  eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
  phy_cfg.autonego_timeout_ms = 100;  // QEMU's PHY: as ESP-IDF's examples for open_eth
  esp_eth_mac_t* mac = esp_eth_mac_new_openeth(&mac_cfg);
  esp_eth_phy_t* phy = esp_eth_phy_new_dp83848(&phy_cfg);
  esp_eth_config_t cfg = ETH_DEFAULT_CONFIG(mac, phy);
  esp_eth_handle_t eth = nullptr;
  if (esp_eth_driver_install(&cfg, &eth) != ESP_OK) return false;
  esp_netif_attach(netif, esp_eth_new_netif_glue(eth));
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, onIp, nullptr);
  if (esp_eth_start(eth) != ESP_OK) return false;
  uint32_t t0 = millis();
  while (!g_up && millis() - t0 < wait_ms) delay(10);  // DHCP from QEMU's user network: 10.0.2.15
  return g_up;
}

bool qemuEthUp() { return g_up; }
const char* qemuEthIp() { return g_ip; }

}  // namespace pt
#endif
