// Source/plugin/factory/diode-609.inc: Diode 609's factory presets (slot 5: uuids ...0051-...005f). An Entry list
// expanded inside FactoryBank.cpp's bank (format and rules there); host plain units; unstated parameters take Diode
// 609's defaults. Steps (Diode609Desc.cpp): THRESHOLD -42 ... -12 dBFS in 2 dB (dBu = dBFS + 22); RATIO 1.5 / 2 / 3 / 4
// / 6; ATTACK FAST = 3, SLOW = 6 (+ the side-chain high-pass); RELEASE 100 400 800 1500 ms, A1 = 2000, A2 = 5000; GAIN
// 0 ... 20 dB in 2 dB; LIMIT threshold -18 ... -7 dBFS in 1 dB (+4 ... +15 dBu), OUT = 24; LIMIT attack FAST = 2,
// SLOW = 4; LIMIT release 50 100 200 800 ms, A1 = 2000, A2 = 5000.

{ "00000000-0000-4000-8000-000000000051", "diode-609", "Mix Bus Thickener", "Bus",
  "2:1 with the SLOW attack and AUTO 1 release: the weight of the diode bridge on a mix bus, limiter out.",
  { { Pid::thr, -20.0f }, { Pid::ratio, ratio(2.0f) }, { Pid::atk, 6.0f }, { Pid::rel, 2000.0f },
    { Pid::makeup, 2.0f } } },

{ "00000000-0000-4000-8000-000000000052", "diode-609", "Vocal Comp + Limit", "Vocal",
  "3:1 compressor into the limiter at +10 dBu: a steady vocal level with the stray peaks caught.",
  { { Pid::thr, -28.0f }, { Pid::ratio, ratio(3.0f) }, { Pid::atk, 3.0f }, { Pid::rel, 400.0f },
    { Pid::makeup, 6.0f }, { Pid::s2thr, -12.0f }, { Pid::s2atk, 2.0f }, { Pid::s2rel, 100.0f } } },

{ "00000000-0000-4000-8000-000000000053", "diode-609", "Drum Crush", "Drums",
  "6:1 with the fast attack and release, blended at 60 %: thick, dense drums that keep their transients.",
  { { Pid::thr, -36.0f }, { Pid::ratio, ratio(6.0f) }, { Pid::atk, 3.0f }, { Pid::rel, 100.0f },
    { Pid::makeup, 10.0f }, { Pid::mix, 0.6f } } },

{ "00000000-0000-4000-8000-000000000054", "diode-609", "Bass Weight", "Bass",
  "4:1 SLOW attack for body, and the limiter at +8 dBu with AUTO release holding the peaks of a bass DI.",
  { { Pid::thr, -26.0f }, { Pid::ratio, ratio(4.0f) }, { Pid::atk, 6.0f }, { Pid::rel, 800.0f },
    { Pid::makeup, 4.0f }, { Pid::s2thr, -14.0f }, { Pid::s2atk, 4.0f }, { Pid::s2rel, 2000.0f } } },
