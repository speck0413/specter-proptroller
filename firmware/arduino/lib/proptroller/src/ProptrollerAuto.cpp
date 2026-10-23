// The controller without prop.begin() / prop.update(): the first call that needs it starts the generic
// controller, and the network and the USB serial port are then served in the background, so loop() is the
// sketch's own (a delay(5000) there stops nothing).
//
//   ESP32       a FreeRTOS task on core 1, below the step task's priority, runs what prop.update() runs. It is not
//               watched: a stuck network must never reset a running show (the watchdog watches the step).
//   RP2040      the step already runs on core 1; the network must stay on core 0, and arduino-pico has no hook
//               after loop(), so core 0 is served only in prop.update() and in delay() (arduino-pico's delay is
//               weak). loop() must call one of them: otherwise the phone page and the serial port stall.
//               Triggers, outputs and shows run on core 1 and never wait.
//   host        a thread steps the simulated board every 10 ms (tests hold proptrollerHostMutex()).
//
// A sketch that calls prop.begin() itself keeps the old way: prop.update() in loop() does the work.
#include "ProptrollerAdvanced.h"

#if defined(ARDUINO) && defined(ESP32)
#include <Arduino.h>
#elif defined(ARDUINO) && defined(ARDUINO_ARCH_RP2040) && !defined(__FREERTOS)
#include <Arduino.h>
#include <pico/stdlib.h>
#define PT_AUTO_DELAY 1
#elif !defined(ARDUINO)
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#define PT_AUTO_HOST 1
#endif

static Proptroller* g_auto = nullptr;  // the controller started on first use

#if defined(ARDUINO) && defined(ESP32)
static const uint32_t SERVICE_TASK_STACK = 12 * 1024;  // as the loop task it replaces (tools/stack_check.py)
static TaskHandle_t g_service = nullptr;
static void serviceTask(void* arg) {
  Proptroller& p = *(Proptroller*)arg;
  for (;;) {
    p.update();
    vTaskDelay(1);
  }
}
#endif

#if PT_AUTO_HOST
static std::recursive_mutex g_host_mutex;
static std::thread g_host_thread;
static std::atomic<bool> g_host_stop{false};
static thread_local bool t_service = false;
std::recursive_mutex& proptrollerHostMutex() { return g_host_mutex; }
#endif

#if PT_AUTO_DELAY
static bool g_in_delay_service = false;
// delay() on core 0 serves the network while it waits; anywhere else it only waits
extern "C" void delay(unsigned long ms) {
  if (g_auto && get_core_num() == 0 && !g_in_delay_service) {
    unsigned long start = millis();
    while (millis() - start < ms) {
      g_in_delay_service = true;
      g_auto->update();
      g_in_delay_service = false;
      sleep_ms(1);
    }
    return;
  }
  if (ms) sleep_ms(ms);
}
#endif

void ProptrollerAuto::start(Proptroller& p) {
  if (p.started_ || g_auto) return;
  g_auto = &p;
  p.begin();
#if defined(ARDUINO) && defined(ESP32)
  xTaskCreatePinnedToCore(serviceTask, "proptroller-io", SERVICE_TASK_STACK, &p, 1, &g_service, 1);
#elif PT_AUTO_HOST
  g_host_stop = false;
  g_host_thread = std::thread([&p] {
    t_service = true;
    while (!g_host_stop) {
      {
        std::lock_guard<std::recursive_mutex> g(g_host_mutex);
        p.update();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });
#endif
}

bool ProptrollerAuto::active(const Proptroller& p) { return g_auto == &p; }

bool ProptrollerAuto::skipUpdate(const Proptroller& p) {
#if defined(ARDUINO) && defined(ESP32)
  return g_auto == &p && xTaskGetCurrentTaskHandle() != g_service;
#elif PT_AUTO_HOST
  return g_auto == &p && !t_service;
#else
  (void)p;
  return false;  // RP2040: prop.update() and delay() serve core 0
#endif
}

void ProptrollerAuto::stop(Proptroller& p) {
  if (g_auto != &p) return;
#if PT_AUTO_HOST
  g_host_stop = true;
  if (g_host_thread.joinable()) g_host_thread.join();
#endif
  g_auto = nullptr;
}
