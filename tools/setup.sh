#!/bin/sh
# Set up a computer to build and flash the firmware (C++ on PlatformIO, firmware/arduino). By default only what
# building, flashing and the host tests need: python3, a host C++ compiler (the unit tests and the simulator),
# PlatformIO Core (official installer into ~/.platformio, or pipx) and the PlatformIO platforms and packages of
# every environment; offers pio on the PATH (~/.profile) and, on Linux, the serial port (dialout group,
# PlatformIO's udev rules). With --dev also the developer checks: clang, clang-tidy, clang-format, cppcheck, LLVM
# (sanitizers, libFuzzer), Node.js + npm, the browser-test packages (firmware/web-test), a Chromium or Chrome.
# Then it runs the host unit tests (pio test -e native).
#
#   tools/setup.sh                ask before installing system packages (they need sudo)
#   tools/setup.sh --yes          install system packages without asking (the sudo command is still printed first)
#   tools/setup.sh --check        install nothing: report what is there and what is missing
#   tools/setup.sh --dev [...]    also the developer tools (static analysis, fuzzing, browser tests)
#
# Linux (apt, dnf or pacman) and macOS (Homebrew). Windows: use WSL 2 (Ubuntu) and run it there, or install
# VS Code and its PlatformIO extension natively (README.md, "Windows").
# Safe to run again: whatever is already there is kept. Never runs sudo without saying so.
set -u
orig_path=$PATH
root=$(cd "$(dirname "$0")/.." && pwd)
fw="$root/firmware/arduino"
mode=ask
dev=0
for arg in "$@"; do
  case "$arg" in
    --yes) mode=yes ;;
    --check) mode=check ;;
    --dev) dev=1 ;;
    *) sed -n '2,17p' "$0"; exit 2 ;;
  esac
done

ok() { printf '  ok       %s\n' "$*"; }
did() { printf '  done     %s\n' "$*"; }
miss() { printf '  MISSING  %s\n' "$*"; missing=1; }
# a developer tool: missing only with --dev
optional() { printf '  optional %s (not installed; tools/setup.sh --dev adds it)\n' "$*"; }
note() { printf '           %s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }
missing=0

# ---------------------------------------------------------------- the package manager
os=$(uname -s)
pm=none
if [ "$os" = Darwin ]; then
  have brew && pm=brew
elif have apt-get; then pm=apt
elif have dnf; then pm=dnf
elif have pacman; then pm=pacman
fi

# run CMD (a shell command), asking first; --check only prints it, --yes prints and runs it
ask_run() {
  if [ "$mode" = check ]; then
    note "to do: $1"
    return 1
  fi
  if [ "$mode" = ask ]; then
    if [ -t 0 ]; then
      printf '  run: %s ? [y/N] ' "$1"
      read -r answer
      case "$answer" in y | Y | yes) ;; *) note "skipped; run it yourself: $1"; return 1 ;; esac
    else
      note "not a terminal, so nothing is changed; run: $1"
      return 1
    fi
  else
    note "running: $1"
  fi
  sh -c "$1"
}

# install PACKAGES...: the system package manager, asking first (sudo on Linux)
install_pkgs() {
  case $pm in
    apt) cmd="sudo apt-get install -y $*" ;;
    dnf) cmd="sudo dnf install -y $*" ;;
    pacman) cmd="sudo pacman -S --needed --noconfirm $*" ;;
    brew) cmd="brew install $*" ;;
    *) note "no supported package manager found: install $* yourself"; return 1 ;;
  esac
  [ "$pm" = apt ] && [ -z "${apt_updated:-}" ] && cmd="sudo apt-get update && $cmd" && apt_updated=1
  ask_run "$cmd"
}

# package names per manager: pkg TOOL -> package (empty: comes with another one)
pkg() {
  case "$pm:$1" in
    dnf:clang-tidy | dnf:clang-format) echo clang-tools-extra ;;
    pacman:clang-tidy | pacman:clang-format) echo clang ;;
    brew:clang | brew:clang-tidy | brew:clang-format | brew:llvm-symbolizer) echo llvm ;;
    apt:llvm-symbolizer | dnf:llvm-symbolizer | pacman:llvm-symbolizer) echo llvm ;;
    brew:node | brew:npm) echo node ;;
    *:node) echo nodejs ;;
    *) echo "$1" ;;
  esac
}

# ---------------------------------------------------------------- python3
step "python3"
if have python3; then
  ok "$(python3 --version 2>&1)"
else
  miss python3
  install_pkgs python3 || { echo "python3 is needed for PlatformIO and the tools; install it and run again"; exit 1; }
fi

# ---------------------------------------------------------------- a host C++ compiler
step "a C++ compiler for this computer (the host unit tests and the simulator)"
if have c++ || have g++ || have clang++; then
  for t in c++ g++ clang++; do have "$t" && { ok "$t: $("$t" --version 2>&1 | head -1)"; break; }; done
elif [ "$os" = Darwin ]; then
  miss "a C++ compiler"
  ask_run "xcode-select --install"
else
  miss "a C++ compiler"
  case $pm in
    apt) install_pkgs build-essential ;;
    dnf) install_pkgs gcc-c++ make ;;
    pacman) install_pkgs base-devel ;;
    *) note "install g++ (or clang++) yourself" ;;
  esac
fi

# ---------------------------------------------------------------- PlatformIO Core
step "PlatformIO Core"
PATH="$HOME/.platformio/penv/bin:$HOME/.local/bin:$PATH"
export PATH
if have pio; then
  ok "$(pio --version 2>&1) ($(command -v pio))"
elif [ "$mode" = check ]; then
  miss "PlatformIO Core (pio)"
  note "to install: the official installer (get-platformio.py) into ~/.platformio, or: pipx install platformio"
else
  installer=$(mktemp -d)/get-platformio.py
  echo "  installing PlatformIO Core into ~/.platformio with the official installer"
  if python3 -c "import urllib.request,sys; urllib.request.urlretrieve('https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py', sys.argv[1])" "$installer" &&
    python3 "$installer"; then
    did "PlatformIO Core: $(pio --version 2>&1)"
  elif have pipx && pipx install platformio; then
    did "PlatformIO Core with pipx: $(pio --version 2>&1)"
  else
    miss "PlatformIO Core: the installer failed; see https://docs.platformio.org/en/latest/core/installation/"
  fi
  rm -rf "$(dirname "$installer")"
fi
if [ -x "$HOME/.platformio/penv/bin/pio" ] && ! (PATH=$orig_path; command -v pio >/dev/null 2>&1); then
  if grep -qsF '.platformio/penv/bin' "$HOME/.profile"; then
    note "pio is on the PATH from ~/.profile: open a new terminal (or log out and in)"
  else
    ask_run "echo 'export PATH=\"\$HOME/.platformio/penv/bin:\$PATH\"' >> \"\$HOME/.profile\"" &&
      did "pio added to the PATH in ~/.profile: open a new terminal (or log out and in)"
  fi
fi

# ---------------------------------------------------------------- the USB serial port (Linux)
if [ "$os" = Linux ]; then
  step "USB serial port access (flashing and the serial console)"
  grp=dialout
  getent group uucp >/dev/null 2>&1 && ! getent group dialout >/dev/null 2>&1 && grp=uucp
  if id -nG | tr ' ' '\n' | grep -qx "$grp"; then
    ok "you are in the $grp group"
  else
    ask_run "sudo usermod -aG $grp $USER" && did "added to $grp: log out and in for it to take effect"
  fi
  rules=/etc/udev/rules.d/99-platformio-udev.rules
  if [ -f "$rules" ]; then
    ok "PlatformIO udev rules ($rules)"
  else
    ask_run "curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules | sudo tee $rules >/dev/null && sudo udevadm control --reload-rules && sudo udevadm trigger" &&
      did "PlatformIO udev rules"
  fi
  grep -qi microsoft /proc/version 2>/dev/null && note "WSL 2: attach the board from Windows with usbipd-win (usbipd attach --wsl --busid <id>)"
fi

# ---------------------------------------------------------------- PlatformIO platforms and packages
step "PlatformIO platforms and packages (every environment in firmware/arduino/platformio.ini)"
if have pio; then
  if [ "$mode" = check ]; then
    note "to install: (cd firmware/arduino && pio pkg install)"
  elif (cd "$fw" && pio pkg install >/dev/null); then
    did "pio pkg install (already installed packages are kept)"
  else
    miss "pio pkg install failed: run it in firmware/arduino to see why"
  fi
fi

# ---------------------------------------------------------------- developer tools (--dev)
if [ "$dev" = 0 ]; then
  step "developer tools (static analysis, fuzzing, browser tests): only with --dev"
  for t in clang clang-tidy clang-format cppcheck node npm; do
    if have "$t"; then ok "$t"; else optional "$t"; fi
  done
else
# ---------------------------------------------------------------- C/C++ tools
step "clang, clang-tidy, clang-format, cppcheck, LLVM (sanitizers, libFuzzer)"
want=""
for t in clang clang-tidy clang-format cppcheck llvm-symbolizer; do
  if have "$t"; then ok "$t: $("$t" --version 2>&1 | grep -m1 -i version)"; else miss "$t"; want="$want $(pkg "$t")"; fi
done
if [ -n "$want" ]; then
  [ "$pm" = brew ] && note "Homebrew's llvm is keg-only: add \$(brew --prefix llvm)/bin to PATH for clang-tidy and clang-format"
  # shellcheck disable=SC2086
  install_pkgs $(printf '%s\n' $want | sort -u) && did "C/C++ tools"
fi
if have clang; then
  t=$(mktemp -d)
  printf 'int LLVMFuzzerTestOneInput(const unsigned char* d, unsigned long n) { (void)d; (void)n; return 0; }\n' >"$t/f.c"
  if clang -fsanitize=fuzzer,address,undefined "$t/f.c" -o "$t/f" >/dev/null 2>&1; then
    ok "clang builds with -fsanitize=fuzzer,address,undefined (libFuzzer and the sanitizers work)"
  else
    miss "clang cannot link -fsanitize=fuzzer,address,undefined (the compiler runtime is missing)"
    major=$(clang --version | sed -n 's/.*version \([0-9][0-9]*\).*/\1/p' | head -1)
    case $pm in
      apt) install_pkgs "libclang-rt-$major-dev" ;;
      dnf | pacman) install_pkgs compiler-rt ;;
      brew) install_pkgs llvm ;;
      *) note "install the clang runtime (compiler-rt) for clang $major" ;;
    esac
  fi
  rm -rf "$t"
fi

# ---------------------------------------------------------------- clang-tidy for tools/check-static.sh
# check-static.sh needs clang-tidy 18 or newer that can read this computer's C library headers; PlatformIO's own
# (15) cannot, and distributions often ship an older one. The pip wheel (clang-tidy 21) goes into PlatformIO's
# Python, where check-static.sh looks first (before PATH).
step "clang-tidy 21 in PlatformIO's Python (tools/check-static.sh)"
penv="$HOME/.platformio/penv/bin"
if [ -x "$penv/clang-tidy" ]; then
  ok "$("$penv/clang-tidy" --version 2>&1 | grep -m1 -i version) ($penv/clang-tidy)"
elif [ ! -x "$penv/pip" ]; then
  miss "PlatformIO's Python ($penv/pip): install PlatformIO Core first"
elif [ "$mode" = check ]; then
  miss "clang-tidy in $penv"
  note "to install: $penv/pip install clang-tidy"
elif "$penv/pip" install --quiet clang-tidy; then
  did "clang-tidy $("$penv/clang-tidy" --version 2>&1 | grep -o 'version [0-9.]*') in $penv"
else
  miss "$penv/pip install clang-tidy failed"
fi

# ---------------------------------------------------------------- Node.js and the browser tests
step "Node.js, npm and the browser-test packages (firmware/web-test)"
want=""
for t in node npm; do
  if have "$t"; then ok "$t $("$t" --version 2>&1)"; else miss "$t"; want="$want $(pkg "$t")"; fi
done
if [ -n "$want" ]; then
  # shellcheck disable=SC2086
  install_pkgs $(printf '%s\n' $want | sort -u) && did "Node.js"
fi
if have npm; then
  if [ -d "$root/firmware/web-test/node_modules/playwright-core" ]; then
    ok "web-test packages (firmware/web-test/node_modules)"
  elif [ "$mode" = check ]; then
    miss "web-test packages"
    note "to install: (cd firmware/web-test && npm install --no-package-lock)"
  elif (cd "$root/firmware/web-test" && npm install --silent --no-package-lock >/dev/null 2>&1); then
    did "npm install in firmware/web-test (no lock file written)"
  else
    miss "npm install in firmware/web-test failed"
  fi
fi

step "a Chromium or Chrome for the browser tests"
browser=""
for b in chromium chromium-browser google-chrome google-chrome-stable; do
  if have "$b"; then browser=$(command -v "$b"); break; fi
done
[ -z "$browser" ] && [ -x "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" ] && browser="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
[ -z "$browser" ] && [ -x "/Applications/Chromium.app/Contents/MacOS/Chromium" ] && browser="/Applications/Chromium.app/Contents/MacOS/Chromium"
if [ -n "$browser" ]; then
  ok "$browser (use CHROMIUM=\"$browser\" for tools/check-host.sh)"
else
  miss "Chromium or Chrome"
  case $pm in
    brew) install_pkgs --cask chromium ;;
    apt) install_pkgs chromium || note "on Ubuntu: sudo snap install chromium, or Google Chrome's .deb" ;;
    *) install_pkgs chromium ;;
  esac
fi

fi

# ---------------------------------------------------------------- the host tests
step "host unit tests: pio test -e native"
tests=skipped
if have pio && [ "$mode" != check ]; then
  log=$(mktemp)
  if (cd "$fw" && pio test -e native >"$log" 2>&1); then tests=passed; else tests=failed; fi
  tail -2 "$log"
  [ "$tests" = failed ] && note "the whole output: $log" || rm -f "$log"
fi

step "summary"
[ "$missing" = 0 ] && echo "  everything needed is there" || echo "  something is missing (MISSING above); run tools/setup.sh again after installing it"
echo "  host unit tests: $tests"
cat <<EOF

Next steps (README.md, "Developer quick start"):
  1. Open it in VS Code: code $root/spectre-proptroller.code-workspace, and install the recommended
     extensions when asked.
  2. Terminal > Run Task: "Native unit tests", "Host simulator", "Build all boards"
     ("Check host (browser tests)" needs tools/setup.sh --dev).
  3. From a shell: cd firmware/arduino && pio run && pio test -e native
     (PlatformIO lives in ~/.platformio/penv/bin; add it to PATH as shown above if pio is not found.)
EOF
[ "$tests" = failed ] && exit 1
exit 0
