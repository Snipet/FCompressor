// Source/plugin/factory/bus-25.inc: Bus 25's factory presets (slot 6: uuids ...0061-...006f). An Entry list expanded
// inside FactoryBank.cpp's bank (format and rules there); host plain units; unstated parameters take Bus 25's defaults.
// Steps (Bus25Desc.cpp): TYPE NEW = 0 (feed-forward), OLD = 1 (feedback); RATIO 1.5 2 3 4 6 10 inf; KNEE HARD = 0,
// MED = 6, SOFT = 12; ATTACK 0.03 0.1 0.3 1 3 10 30 ms; TIME MODE FIXED = 0 (RELEASE 50 100 200 500 1000 2000 ms), VAR =
// 1 (RELEASE continuous 50 ... 3000 ms); THRUST NORM = 0, MED = 1.5, LOUD = 3.01 (sce); LINK IND 50 ... 100 %.
// THRESHOLD is dBFS (-42 ... -2; dBu = dBFS + 22).

{ "00000000-0000-4000-8000-000000000061", "bus-25", "Mix Bus 2:1", "Bus",
  "2:1 soft knee with THRUST MED so the low end does not pump: an open, punchy mix bus.",
  { { Pid::thr, -18.0f }, { Pid::ratio, ratio(2.0f) }, { Pid::knee, 12.0f }, { Pid::atk, 30.0f },
    { Pid::rel, 200.0f }, { Pid::sce, 1.5f }, { Pid::makeup, 2.0f } } },

{ "00000000-0000-4000-8000-000000000062", "bus-25", "Drum Bus Thrust", "Drums",
  "OLD (feedback) type, hard knee, THRUST LOUD: aggressive drum-bus punch that ignores the kick's low end.",
  { { Pid::voice, 1.0f }, { Pid::thr, -24.0f }, { Pid::ratio, ratio(4.0f) }, { Pid::knee, 0.0f },
    { Pid::atk, 10.0f }, { Pid::rel, 100.0f }, { Pid::sce, 3.01f }, { Pid::makeup, 4.0f } } },

{ "00000000-0000-4000-8000-000000000063", "bus-25", "Parallel Punch", "Parallel",
  "10:1 hard knee with a fast attack and release at 50 % mix: New York parallel drums in one insert.",
  { { Pid::thr, -34.0f }, { Pid::ratio, 0.9f }, { Pid::knee, 0.0f }, { Pid::atk, 0.3f }, { Pid::rel, 50.0f },
    { Pid::makeup, 10.0f }, { Pid::mix, 0.5f } } },

{ "00000000-0000-4000-8000-000000000064", "bus-25", "Vocal Silk", "Vocal",
  "OLD type with a soft knee and the VARIABLE release at 350 ms: smooth, forgiving vocal levelling.",
  { { Pid::voice, 1.0f }, { Pid::thr, -28.0f }, { Pid::ratio, ratio(3.0f) }, { Pid::knee, 12.0f },
    { Pid::atk, 3.0f }, { Pid::tmode, 1.0f }, { Pid::rel, 350.0f }, { Pid::makeup, 5.0f } } },
