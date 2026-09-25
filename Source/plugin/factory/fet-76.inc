// Source/plugin/factory/fet-76.inc: FET 76's factory presets (slot 2: uuids ...0021-...002f). An Entry list expanded
// inside FactoryBank.cpp's bank (format and rules there); host plain units; unstated parameters take FET 76's defaults.
// thr is the effective input threshold (INPUT dial = 24 + (-18 - thr) / 0.9375: -27 dBFS reads about 33.6); RATIO
// 4 / 8 / 12 / 20 / ALL = 0.75 / 0.875 / 0.91667 / 0.95 / 1.0; ATTACK 0.02-0.8 ms and RELEASE 50-1100 ms (the
// hardware's knobs run the other way); OUTPUT is makeup in dB; VOICE LN = 0, A = 1, F = 2.

{ "00000000-0000-4000-8000-000000000021", "fet-76", "Vocal Grab", "Vocal",
  "4:1 with a medium attack and a quick release: the vocal sits forward and the consonants stay intact.",
  { { Pid::thr, -27.0f }, { Pid::ratio, 0.75f }, { Pid::atk, 0.4f }, { Pid::rel, 150.0f },
    { Pid::makeup, 5.0f } } },

{ "00000000-0000-4000-8000-000000000022", "fet-76", "Bass Hold", "Bass",
  "8:1 with a slow attack to keep the pluck and a medium release, so every note sustains at the same level.",
  { { Pid::thr, -24.0f }, { Pid::ratio, 0.875f }, { Pid::atk, 0.6f }, { Pid::rel, 250.0f },
    { Pid::makeup, 4.0f } } },

{ "00000000-0000-4000-8000-000000000023", "fet-76", "Snare Crack", "Drums",
  "The slowest attack lets the stick through, the fastest release brings the body up between hits.",
  { { Pid::thr, -28.0f }, { Pid::ratio, 0.75f }, { Pid::atk, 0.8f }, { Pid::rel, 50.0f },
    { Pid::makeup, 5.0f } } },

{ "00000000-0000-4000-8000-000000000024", "fet-76", "All Buttons Room", "Drums",
  "All buttons in: the aggressive, distorted plateau for room mics; ALL lags the attack, so transients still spit.",
  { { Pid::thr, -34.0f }, { Pid::ratio, 1.0f }, { Pid::atk, 0.1f }, { Pid::rel, 60.0f }, { Pid::voice, 2.0f },
    { Pid::makeup, 8.0f } } },
