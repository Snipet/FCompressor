// Source/plugin/factory/opto-2a.inc: Opto 2A's factory presets (slot 3: uuids ...0031-...003f). An Entry list
// expanded inside FactoryBank.cpp's bank (format and rules there); host plain units; unstated parameters take Opto
// 2A's defaults. thr is PEAK REDUCTION (dial = (20 - thr) / 0.7: -18.5 dBFS reads 55); RATIO COMPRESS = 0.6667, LIMIT =
// 0.90; EMPHASIS is sce 0...6 (dial = sce x 10/6); GAIN is makeup in dB (dial = (makeup + 10) / 0.4). The T4 cell sets
// attack and release itself.

{ "00000000-0000-4000-8000-000000000031", "opto-2a", "Smooth Vocal", "Vocal",
  "COMPRESS at peak reduction 55: slow, programme-dependent levelling that never grabs. The classic lead vocal.",
  { { Pid::thr, -18.5f }, { Pid::ratio, 0.6667f }, { Pid::makeup, 9.0f } } },

{ "00000000-0000-4000-8000-000000000032", "opto-2a", "Bass Levelling", "Bass",
  "Even, sustained bass: the cell's slow memory holds long notes level without audible pumping.",
  { { Pid::thr, -15.0f }, { Pid::ratio, 0.6667f }, { Pid::makeup, 8.0f } } },

{ "00000000-0000-4000-8000-000000000033", "opto-2a", "Gentle Bus", "Bus",
  "A light touch on a group or a mix; half EMPHASIS keeps the low end from driving the cell.",
  { { Pid::thr, -6.0f }, { Pid::ratio, 0.6667f }, { Pid::sce, 3.0f }, { Pid::makeup, 3.0f } } },

{ "00000000-0000-4000-8000-000000000034", "opto-2a", "Limit Lead", "Instrument",
  "LIMIT for a lead guitar or synth: firmer control of the peaks with the same smooth release.",
  { { Pid::thr, -22.0f }, { Pid::ratio, 0.90f }, { Pid::makeup, 10.0f } } },
