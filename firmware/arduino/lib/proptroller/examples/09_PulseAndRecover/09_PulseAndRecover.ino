// 09 Pulse and recover (three runs per three minutes)
//
// What it does: a big button pulses a solenoid valve on OUT1 for 1 second, then OUT2 runs for 5.5 seconds to
// recover (refill, reset, rewind). It may run 3 times in 3 minutes (counted from the first run), then it rests.
// To start, the button has to be released since the last run and held for a moment (0.1 s) while the prop is
// ready, so a quick tap, a bounce or a dropped button never starts it. Held through the recovery, it starts once
// more when the recovery is done. TRIG2 runs the recovery alone (to fill or reset the prop by hand). A start
// from the phone while it is busy waits for it, once "Remote trigger" is on.
//
// Wiring: the button on TRIG1, a "recover" button on TRIG2, the valve on OUT1, the recovery load on OUT2
// (OUT1-OUT4 give the board's supply voltage: 12 V parts need a 12 V supply). CC1
// and CC2 pulse at every run (to chain another controller or a sound box) and the trigger sound plays.
//
// This is the board's built-in effect, described in firmware/STATE-MACHINE.md.

#include <ProptrollerAdvanced.h>

Proptroller prop;

void setup() {
  prop.begin();
  prop.advanced().withPulseAndRecover(PulseAndRecover()
                               .pulseFor(seconds(1))
                               .recoverFor(seconds(5.5))
                               .runsBeforeRest(3)
                               .restFor(minutes(3)));
  prop.advanced().nameInput(TRIG2, "REFILL", "Refill button");  // the app shows REFILL (TRIG2)
  prop.advanced().nameOutput(OUT1, "VALVE", "Valve");
}

void loop() {
  prop.update();
}
