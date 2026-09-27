// Source/plugin/factory/diode-54.inc: Diode 54's factory presets (slot 11: uuids ...00b1-...00bf). An Entry list
// expanded inside FactoryBank.cpp's bank (format and rules there); host plain units; unstated parameters take Diode
// 54's defaults. Values (Diode54Desc.cpp): THRESHOLD and LIMIT THRESHOLD in dBFS = dBu - 22 (+4 dBu = -18, 0 dBu =
// -22, +10 dBu = -12; LIMIT OFF = 24); RATIO through ratio(R) (S = 1 - 1/R, the steps' own floats);
// ATTACK 5 (fixed) or 0.1-2 ms (FAST); RECOVERY 100 / 200 / 800 / AUTO 1500 ms; GAIN 0-20 dB in 2 dB steps.

{ "00000000-0000-4000-8000-0000000000b1", "diode-54", "Drum Bus Weight", "Drums",
  "3:1 at 0 dBu with the fixed 5 ms attack and a 0.1 s recovery: the kit gets denser and the hits keep their front.",
  { { Pid::thr, -22.0f }, { Pid::ratio, ratio(3.0f) }, { Pid::rel, 100.0f }, { Pid::makeup, 4.0f } } },

{ "00000000-0000-4000-8000-0000000000b2", "diode-54", "Vocal Diode", "Vocal",
  "2:1 at -4 dBu with a 0.2 s recovery: the diode bridge's soft knee and transformer thickness on a lead vocal.",
  { { Pid::thr, -26.0f }, { Pid::ratio, ratio(2.0f) }, { Pid::rel, 200.0f }, { Pid::makeup, 4.0f } } },

{ "00000000-0000-4000-8000-0000000000b3", "diode-54", "Snare Grab", "Drums",
  "The FAST pot at 0.5 ms, 4:1 and a 0.1 s recovery: the snare's crack is caught and the ring comes up behind it.",
  { { Pid::thr, -20.0f }, { Pid::ratio, ratio(4.0f) }, { Pid::atk, 0.5f }, { Pid::rel, 100.0f },
    { Pid::makeup, 4.0f } } },

{ "00000000-0000-4000-8000-0000000000b4", "diode-54", "Auto Mix Glue", "Bus",
  "1.5:1 at +4 dBu with the AUTO recovery, linked: a mix bus held together, quick on hits and slow on sustained parts.",
  { { Pid::thr, -18.0f }, { Pid::ratio, ratio(1.5f) }, { Pid::rel, 1500.0f }, { Pid::makeup, 2.0f } } },

{ "00000000-0000-4000-8000-0000000000b5", "diode-54", "Bass Comp + Limit", "Bass",
  "4:1 at -2 dBu into the limiter at +10 dBu: the compressor evens the part and the limiter catches the slaps.",
  { { Pid::thr, -24.0f }, { Pid::ratio, ratio(4.0f) }, { Pid::rel, 200.0f }, { Pid::s2thr, -12.0f },
    { Pid::s2rel, 100.0f }, { Pid::makeup, 6.0f } } },

{ "00000000-0000-4000-8000-0000000000b6", "diode-54", "Diode Room Crush", "Parallel",
  "6:1 at -20 dBu with the FAST pot at 0.1 ms, blended at 40 %: a crushed, pumping room under the dry kit.",
  { { Pid::thr, -42.0f }, { Pid::ratio, ratio(6.0f) }, { Pid::atk, 0.1f }, { Pid::rel, 100.0f },
    { Pid::makeup, 20.0f }, { Pid::mix, 0.4f } } },

{ "00000000-0000-4000-8000-0000000000b7", "diode-54", "Master Safety", "Master",
  "1.5:1 at +8 dBu on AUTO with the limiter at +18 dBu on AUTO: a light touch that catches the occasional overshoot.",
  { { Pid::thr, -14.0f }, { Pid::ratio, ratio(1.5f) }, { Pid::rel, 1500.0f }, { Pid::s2thr, -4.0f },
    { Pid::s2rel, 1500.0f } } },
