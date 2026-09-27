// Source/plugin/factory/opto-3a.inc: Opto 3A's factory presets (slot 10: uuids ...00a1-...00af). An Entry list
// expanded inside FactoryBank.cpp's bank (format and rules there); host plain units; unstated parameters take Opto 3A's
// defaults. Values (Opto3ADesc.cpp, Opto 2A's dials): PEAK RED. is the threshold in dBFS, dial = (20 - thr) / 0.7
// (PR 40 = -8, PR 50 = -15, PR 70 = -29); GAIN is the makeup in dB, dial = (gain + 10) / 0.4 (GAIN 40 = +6);
// COMP 0.6667 / LIMIT 0.75; HF SENS plain 0-6 = dial 0-10; LINK DUAL 0 / LINK 1.

{ "00000000-0000-4000-8000-0000000000a1", "opto-3a", "Vocal Catch", "Vocal",
  "COMP at PEAK RED. 45: the fast T4 attack catches consonants the 2A lets through, and the quicker tail keeps the "
  "vocal moving.",
  { { Pid::thr, -11.5f }, { Pid::makeup, 8.0f } } },

{ "00000000-0000-4000-8000-0000000000a2", "opto-3a", "Bright Vocal Tamer", "Vocal",
  "HF SENS at 10: the cell hears the top end 10 dB louder than the lows, so harsh, bright passages pull the gain down "
  "and the body is left alone.",
  { { Pid::thr, -8.0f }, { Pid::sce, 6.0f }, { Pid::makeup, 7.0f } } },

{ "00000000-0000-4000-8000-0000000000a3", "opto-3a", "Snappy Guitar", "Instrument",
  "LIMIT at PEAK RED. 50: electric or acoustic guitar held even, pick attack softened by the 1.5 ms attack.",
  { { Pid::thr, -15.0f }, { Pid::ratio, 0.75f }, { Pid::makeup, 9.0f } } },

{ "00000000-0000-4000-8000-0000000000a4", "opto-3a", "Overhead Smooth", "Drums",
  "COMP with HF SENS at 6: overheads and cymbals smoothed without the kick pumping the whole kit.",
  { { Pid::thr, -9.4f }, { Pid::sce, 3.6f }, { Pid::makeup, 6.0f } } },

{ "00000000-0000-4000-8000-0000000000a5", "opto-3a", "Bass Grip", "Bass",
  "LIMIT at PEAK RED. 48: DI bass held steady note to note; long notes build the memory and settle slowly.",
  { { Pid::thr, -13.6f }, { Pid::ratio, 0.75f }, { Pid::makeup, 8.0f } } },

{ "00000000-0000-4000-8000-0000000000a6", "opto-3a", "Opto Squeeze", "Parallel",
  "LIMIT pushed hard at PEAK RED. 70 and blended at 35 %: dense, even body under the untouched signal.",
  { { Pid::thr, -29.0f }, { Pid::ratio, 0.75f }, { Pid::makeup, 18.0f }, { Pid::mix, 0.35f } } },

{ "00000000-0000-4000-8000-0000000000a7", "opto-3a", "Gentle Mix Leveler", "Bus",
  "COMP at PEAK RED. 30, linked: a dB or two of slow, program-dependent leveling on a mix.",
  { { Pid::thr, -1.0f }, { Pid::makeup, 2.0f } } },
