// The controller lock's rule for the work a request does without it (Controller::Unlocked, device.h).
//
// A task that gives the lock back to do long work on its own data (a "window": parsing a request body, checking
// a script, writing the flash) lets only the step run meanwhile. Every other task that takes the lock during a
// window gives it back at once and waits (about 1 ms at a time) until the window ends, before it does anything.
// So no task but the step ever sees the controller half way through a request, and nothing but the step runs
// while a window reads the settings, the profile or the stored script. The step never waits (takeStep); the
// controller refuses changes made from the step while a window is open (Controller::busy text), and stopping
// from the step (the pause input, STOP from a task) only touches the show engine.
//
// Raw is the platform's recursive mutex: take(), give(), releaseAll() (every level this task holds, 0 when it
// holds none), mine() (this task holds it), self() (this task, as an Id), pause() (let other tasks run ~1 ms).
// Header only, so the host tests run the same rule with std::thread (test/test_threads).
#pragma once

namespace pt {

template <class Raw>
class WindowLock {
 public:
  // An ordinary take (requests, the network, the sketch, loop()): waits out another task's window.
  void take() {
    bool outer = !raw_.mine();
    raw_.take();
    if (!outer) return;
    while (open_ && owner_ != raw_.self()) {
      raw_.give();
      raw_.pause();
      raw_.take();
    }
  }
  // The step: runs during a window (that is what the window is for).
  void takeStep() { raw_.take(); }
  void give() { raw_.give(); }
  // Controller::Unlocked: give every level back for a window (0, and nothing given back, when this task does not
  // hold the lock or another window is open).
  int releaseForWindow() {
    if (!raw_.mine() || open_) return 0;
    open_ = true;
    owner_ = raw_.self();
    int n = raw_.releaseAll();
    if (n == 0) open_ = false;
    return n;
  }
  void retakeAfterWindow(int n) {
    for (int i = 0; i < n; i++) raw_.take();
    open_ = false;
  }
  Raw& raw() { return raw_; }

 private:
  Raw raw_;
  // both only written and read while the raw lock is held
  bool open_ = false;
  typename Raw::Id owner_{};
};

}  // namespace pt
