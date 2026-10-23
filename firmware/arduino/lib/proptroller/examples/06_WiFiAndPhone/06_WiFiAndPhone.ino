// 06 Wi-Fi and phone control
//
// What it does: nothing in code at all. Every show is made on a phone.
//
// 1. Upload this sketch to an ESP32 DevKit or a Pico W / Pico 2 W board.
// 2. On the phone, join the Wi-Fi network "Proptroller-XXXX" (password: password).
//    That is a simple default: change it under Settings -> Wi-Fi -> Setup hotspot password.
// 3. Open http://192.168.4.1/ : the Live tab shows every input and output, the Shows tab edits shows on a
//    timeline, Record effect turns what you tap into an effect script, Settings has the Wi-Fi.
// 4. Settings -> Wi-Fi: enter your own network (type it, or pick one the board sees) and restart. The board is
//    then at http://proptroller.local/ (http://proptroller-XXXX.local/ when another board already has that
//    name: Settings -> Wi-Fi shows which), or find it with the desktop app. If it cannot join, the setup
//    network comes back.
// 5. The board cannot measure or limit current: keep every load within its terminal's rating
//    (hardware/README.md: 5 A per OUT on/off, 2 A when dimmed at 12 V, 10 A for the whole board).
//
// Shows made on the phone are saved on the board and survive a power cut. You can mix both: shows from
// prop.when(...) in code appear in the phone's list too.

#include <Proptroller.h>

Proptroller prop;

void setup() {
  prop.begin();
}

void loop() {
  prop.update();
}
