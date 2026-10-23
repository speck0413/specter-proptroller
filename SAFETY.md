# Safety and disclaimer

Specter Proptroller is a hobby project shared as-is. Please read this before you build or use it.

- **As-is, no warranty, no certification.** The software, the board design files and the documentation are
  provided as-is, with no warranty of any kind. The board and its enclosure have not been tested or certified to
  any safety standard (no UL, CE, FCC or similar listing).
- **Your own risk.** You build, wire and use it at your own risk. You are responsible for checking that your build
  and your installation are safe for the people and the place around it.
- **Mains on the rev B relays is for qualified people only.** The rev B relays (silk "rev B") may switch up to
  120 V AC 5 A (3 A inductive) each. Only a qualified person, following the local electrical code, should wire mains
  to them: plug the load into a GFCI-protected outlet, put a 5 A fuse in each relay's COM wire, and follow every
  step in [hardware/README.md](hardware/README.md). The recommended way to switch AC is off the board, with a listed
  relay-controlled outlet box ([docs/ac-control.md](docs/ac-control.md)).
- **Rev A relays are low-voltage DC only** (30 V DC at most; the silk says "RELAYS: <=30V DC ONLY"). Never connect
  mains to a rev A board.
- **Everything else is low-voltage DC.** Power the board from a listed 9-24 V DC supply with its own over-current
  protection; the board has no fuses. Every other terminal and every 3.5 mm jack is low-voltage DC only.
- **No liability.** The authors and contributors are not liable for any damage, injury or loss that comes from
  building or using this project, as the [LICENSE](LICENSE) also says.
