// Source/plugin/factory/brickwall.inc: Brickwall's factory presets (slot 7: uuids ...0071-...007f). An Entry list
// expanded inside FactoryBank.cpp's bank (format and rules there); host plain units; unstated parameters take
// Brickwall's defaults. thr is the drive into the limiter (-30 ... 0 dBFS; the threshold lands on the ceiling); CEILING
// is makeup (-12 ... 0 dB); TIME MODE MAN = 0, AUTO = 1; DETECT PEAK = 0, TRUE PEAK = 1; VOICE CLEAN = 0, LOUD = 1;
// STEREO ST = 0, MID = 2, SIDE = 3. LOOKAHEAD needs the lookahead budget (5 MS or 20 MS), a setup the preset leaves
// alone: with the budget OFF it runs at 0.

{ "00000000-0000-4000-8000-000000000071", "brickwall", "Master -1 dBTP", "Master",
  "True-peak mastering limiter: ceiling -1 dBTP, AUTO release, 5 ms lookahead (set the budget to 5 MS or 20 MS).",
  { { Pid::thr, -4.0f }, { Pid::knee, 1.0f }, { Pid::rel, 60.0f }, { Pid::tmode, 1.0f }, { Pid::look, 5.0f },
    { Pid::det, 1.0f }, { Pid::makeup, -1.0f } } },

{ "00000000-0000-4000-8000-000000000072", "brickwall", "Loud Master", "Master",
  "The LOUD voice soft-clips before the limiter for density: 9 dB of drive into a -1 dBTP ceiling.",
  { { Pid::thr, -9.0f }, { Pid::knee, 2.0f }, { Pid::rel, 100.0f }, { Pid::tmode, 1.0f }, { Pid::look, 5.0f },
    { Pid::det, 1.0f }, { Pid::voice, 1.0f }, { Pid::makeup, -1.0f } } },

{ "00000000-0000-4000-8000-000000000073", "brickwall", "Transparent Safety", "Limiter",
  "A safety net that touches only the overs: 2 ms lookahead, a slow release, ceiling -0.3 dBFS.",
  { { Pid::thr, -1.0f }, { Pid::knee, 0.0f }, { Pid::rel, 150.0f }, { Pid::tmode, 0.0f }, { Pid::look, 2.0f },
    { Pid::det, 0.0f }, { Pid::makeup, -0.3f } } },

{ "00000000-0000-4000-8000-000000000074", "brickwall", "Drum Clamp", "Drums",
  "A fast clamp for drum groups: shaves the transient peaks by a few dB to win headroom, ceiling -3 dBFS.",
  { { Pid::thr, -6.0f }, { Pid::knee, 0.0f }, { Pid::rel, 10.0f }, { Pid::tmode, 0.0f }, { Pid::look, 1.0f },
    { Pid::det, 0.0f }, { Pid::makeup, -3.0f } } },
