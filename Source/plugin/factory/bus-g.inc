// Source/plugin/factory/bus-g.inc: Bus G's factory presets (slot 1: uuids ...0011-...001f). An Entry list expanded
// inside FactoryBank.cpp's bank (format and rules there); host plain units; unstated parameters take Bus G's defaults.
// Steps (BusGDesc.cpp): RATIO 2 / 4 / 10 = 0.50 / 0.75 / 0.90; ATTACK 0.1 0.3 1 3 10 30 ms; RELEASE 100 300 600 1200 ms
// and AUTO = 2400; VOICE VCA = 0, CLEAN = 1 (DRIVE n/a). THRESHOLD is the dial's dBFS (-33 ... -3; dial 0 = -18).

{ "00000000-0000-4000-8000-000000000011", "bus-g", "Mix Bus Glue", "Bus",
  "The classic console bus setting: 2:1, 30 ms attack, AUTO release. Aim for 2-4 dB of gain reduction.",
  { { Pid::thr, -16.0f }, { Pid::ratio, 0.50f }, { Pid::atk, 30.0f }, { Pid::rel, 2400.0f },
    { Pid::makeup, 2.0f } } },

{ "00000000-0000-4000-8000-000000000012", "bus-g", "Drum Bus Snap", "Drums",
  "4:1 with a fast release that breathes with the groove; the side-chain high-pass keeps the kick from pumping it.",
  { { Pid::thr, -22.0f }, { Pid::ratio, 0.75f }, { Pid::atk, 10.0f }, { Pid::rel, 100.0f }, { Pid::schpf, 60.0f },
    { Pid::makeup, 4.0f } } },

{ "00000000-0000-4000-8000-000000000013", "bus-g", "Master Polish", "Master",
  "Light 2:1 for a finished mix: 10 ms attack, 0.6 s release, the low end kept out of the side chain.",
  { { Pid::thr, -15.0f }, { Pid::ratio, 0.50f }, { Pid::atk, 10.0f }, { Pid::rel, 600.0f }, { Pid::schpf, 100.0f },
    { Pid::makeup, 1.0f } } },

{ "00000000-0000-4000-8000-000000000014", "bus-g", "Crushed Parallel", "Parallel",
  "10:1 at the fastest attack and release, pushed into the VCA colour and blended at 40 %: density and room tone.",
  { { Pid::thr, -32.0f }, { Pid::ratio, 0.90f }, { Pid::atk, 0.1f }, { Pid::rel, 100.0f }, { Pid::drive, 6.0f },
    { Pid::makeup, 12.0f }, { Pid::mix, 0.4f } } },
