# FCompressor releases

Signed with `Developer ID Application: Sean Funk (Y29FLXW57M)`, notarised and stapled, arm64 only (Q6). Built with
`Scripts/release.sh`; each release's `MANIFEST.txt` records the source, dependency pins, flags and SHA256SUMS.

## v1.1.0 — 2026-09-26

User requests after v1 (the UI only; the DSP and the saved state are unchanged from v1.0.0):

- **Linked controls in a box (ADR-74).** A control that follows another parameter and is not edited on its own sits
  in a bordered box. Examples: Bus G's KNEE "= RATIO", Mu 67's RATIO and ATTACK, Brickwall's ATTACK "= LOOKAHEAD".
- **A colour per Mode (ADR-75).** Each Mode's live gain-reduction picture is drawn in its own colour, in GRAPHITE and
  PAPER: the GR readout and bar, the needle, the GR meter, the history trace and the operating dot. The rule under
  the Mode name and a swatch in the Mode browser carry the same colour. FET 76 is amber, Opto 2A lime, Mu 67 green,
  Clean cyan, Bus G azure, Bus 25 periwinkle, Diode 609 violet and Brickwall magenta.
- **Hardware-style GR meter faces (ADR-76).** The HISTORY · VU meter wears a face in the spirit of each Mode's hardware
  class: ivory VU, backlit VU, a ring-bezel black meter, a grey-blue ruler meter, black and cream console meters, and
  an LED ladder for Brickwall. Clean keeps the panel meter.

## v1.0.0 — 2026-09-26

The first release: eight Modes (Clean, Bus G, FET 76, Opto 2A, Mu 67, Diode 609, Bus 25, Brickwall) on one shared
panel.

- The GPU editor (FunkGui): the Characteristics screen, the Mode browser, presets (34 factory, plus save, save-over,
  rename, delete, import and export), UI zoom and the GR VU meter.
- GRAPHITE and a high-contrast PAPER theme.
- Record: `docs/sprints/s13.md` Outcome.
