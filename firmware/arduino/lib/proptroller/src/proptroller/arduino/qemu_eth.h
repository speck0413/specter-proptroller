// QEMU only: the network on QEMU's OpenCores Ethernet (qemu_eth.cpp).
#pragma once
#include <stdint.h>

namespace pt {
bool qemuEthBegin(uint32_t wait_ms);  // brings the interface up and waits for DHCP; true when it has an address
bool qemuEthUp();
const char* qemuEthIp();
}  // namespace pt
