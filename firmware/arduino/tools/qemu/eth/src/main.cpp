// The generic firmware (src/main_wifi.cpp), for QEMU with the network on OpenCores Ethernet (PROPTROLLER_QEMU_ETH).
#include <Proptroller.h>

Proptroller prop;

void setup() { prop.begin(); }

void loop() { prop.update(); }
