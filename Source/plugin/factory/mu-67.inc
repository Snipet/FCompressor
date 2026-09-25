// Source/plugin/factory/mu-67.inc: Mu 67's factory presets (slot 4: uuids ...0041-...004f). An Entry list expanded
// inside FactoryBank.cpp's bank (format and rules there); host plain units; unstated parameters take Mu 67's defaults.
// thr is AC THRESH in dBFS (-40 ... +24); knee is DC THRESH in dB (0 ... 40; dial = knee / 4: low = harder, up to 20:1;
// high = gentler, down to 10:1); TIME is rel 300 / 800 / 2000 / 5000 ms, TC5 = 10000, TC6 = 25000; INPUT is drive,
// -20 ... 0 dB in 1 dB steps (colour only); SC HPF 0 / 50 / 100 / 200 / 350 Hz; stmode L/R = 0, LAT/VERT = 1; link
// IND = 0, LINK = 1.

{ "00000000-0000-4000-8000-000000000041", "mu-67", "Mastering Glue", "Master",
  "Soft, progressive tube glue for a stereo mix: 1-2 dB of reduction binds it without dulling it.",
  { { Pid::thr, -21.0f }, { Pid::knee, 28.0f }, { Pid::rel, 800.0f }, { Pid::schpf, 100.0f }, { Pid::drive, -6.0f },
    { Pid::makeup, 1.5f } } },

{ "00000000-0000-4000-8000-000000000042", "mu-67", "Vocal Warmth", "Vocal",
  "Fast TC1 with the input fully up: tube colour that grows with the gain reduction.",
  { { Pid::thr, -26.0f }, { Pid::knee, 16.0f }, { Pid::rel, 300.0f }, { Pid::drive, 0.0f }, { Pid::makeup, 3.0f } } },

{ "00000000-0000-4000-8000-000000000043", "mu-67", "Slow Leveler", "Bus",
  "TC5 releases in 2 s after peaks and in 10 s after dense passages: invisible levelling for a group or a long mix.",
  { { Pid::thr, -24.0f }, { Pid::knee, 20.0f }, { Pid::rel, 10000.0f }, { Pid::drive, -4.0f },
    { Pid::makeup, 2.0f } } },

{ "00000000-0000-4000-8000-000000000044", "mu-67", "Lat/Vert Master", "Master",
  "Mid and side compressed independently (LAT/VERT, unlinked): tames a wide, peaky side without touching the centre.",
  { { Pid::thr, -20.0f }, { Pid::knee, 30.0f }, { Pid::rel, 2000.0f }, { Pid::stmode, 1.0f }, { Pid::link, 0.0f },
    { Pid::schpf, 50.0f }, { Pid::drive, -8.0f }, { Pid::makeup, 1.0f } } },
