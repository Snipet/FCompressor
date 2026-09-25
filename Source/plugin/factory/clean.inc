// Source/plugin/factory/clean.inc: Clean's factory presets (slot 0: uuids ...0001-...000f). An Entry list expanded
// inside FactoryBank.cpp's bank (format and rules there); host plain units; unstated parameters take Clean's defaults.
// Clean is the textbook compressor: every control continuous except TIME MODE, DETECT, STEREO, VOICE and AUTO.

{ "00000000-0000-4000-8000-000000000001", "clean", "Gentle Glue", "Bus",
  "2:1 RMS glue for a mix or group bus: slow attack, programme-dependent AUTO release, soft knee. Aim for 2-3 dB.",
  { { Pid::thr, -20.0f }, { Pid::ratio, ratio(2.0f) }, { Pid::knee, 12.0f }, { Pid::atk, 30.0f },
    { Pid::rel, 300.0f }, { Pid::tmode, 1.0f }, { Pid::det, 1.0f }, { Pid::makeup, 2.0f } } },

{ "00000000-0000-4000-8000-000000000002", "clean", "Vocal Leveler", "Vocal",
  "3:1 on peak and RMS together, a quick attack and a medium release: an even vocal that still breathes.",
  { { Pid::thr, -26.0f }, { Pid::ratio, ratio(3.0f) }, { Pid::knee, 8.0f }, { Pid::atk, 5.0f },
    { Pid::rel, 120.0f }, { Pid::det, 2.0f }, { Pid::schpf, 80.0f }, { Pid::makeup, 4.0f } } },

{ "00000000-0000-4000-8000-000000000003", "clean", "Drum Punch", "Drums",
  "4:1 with a 25 ms attack that lets the hit through, a short hold and a fast release: punch, not squash.",
  { { Pid::thr, -20.0f }, { Pid::ratio, ratio(4.0f) }, { Pid::knee, 4.0f }, { Pid::atk, 25.0f },
    { Pid::rel, 80.0f }, { Pid::hold, 10.0f }, { Pid::schpf, 60.0f }, { Pid::makeup, 3.0f } } },

{ "00000000-0000-4000-8000-000000000004", "clean", "Parallel Smash", "Parallel",
  "10:1 hard and fast through the TUBE voice, blended at 50 %: density and sustain under the dry transients.",
  { { Pid::thr, -40.0f }, { Pid::ratio, ratio(10.0f) }, { Pid::knee, 0.0f }, { Pid::atk, 0.5f },
    { Pid::rel, 60.0f }, { Pid::voice, 1.0f }, { Pid::drive, 6.0f }, { Pid::makeup, 10.0f }, { Pid::mix, 0.5f } } },

{ "00000000-0000-4000-8000-000000000005", "clean", "Broadband De-Ess", "Vocal",
  "The side chain hears mostly sibilance (high-pass at 500 Hz and a +6 dB/oct tilt), so esses duck the voice briefly.",
  { { Pid::thr, -32.0f }, { Pid::ratio, ratio(4.0f) }, { Pid::knee, 3.0f }, { Pid::atk, 0.5f },
    { Pid::rel, 40.0f }, { Pid::schpf, 500.0f }, { Pid::sce, 6.0f } } },
