// Source/plugin/factory/console-e.inc: Console E's factory presets (slot 9: uuids ...0091-...009f). An Entry list
// expanded inside FactoryBank.cpp's bank (format and rules there); host plain units; unstated parameters take Console
// E's defaults. Values (ConsoleEDesc.cpp): THRESHOLD in dBFS, dial 0 = -18 (dial -10 = -28); RATIO continuous in S
// (2:1 0.5, 3:1 0.66667, 4:1 0.75, 6:1 0.83333, inf 1.0); ATTACK FAST 1 / AUTO 10; RELEASE 100-4000 ms; REL CURVE
// LOG 0 / LIN 1; DETECT RMS 0 / PEAK 1; KNEE HARD 0 / OVEREASY 10; SC HPF Hz (< 20 = OFF); LINK DUAL 0 / LINK 1.
// AUTO makeup keeps a 0 VU signal at its level; MAKEUP is a trim after it.

{ "00000000-0000-4000-8000-000000000091", "console-e", "Channel Vocal", "Vocal",
  "3:1 OverEasy on the RMS detector with the auto attack: the channel strip's even, forward vocal. Set THRESHOLD for "
  "4-6 dB of gain reduction.",
  { { Pid::thr, -26.0f }, { Pid::ratio, 0.66667f }, { Pid::rel, 300.0f } } },

{ "00000000-0000-4000-8000-000000000092", "console-e", "Snare Snap", "Drums",
  "4:1 hard knee on the peak detector with the auto attack, which lets the first few milliseconds of the hit through "
  "before it clamps. A short release brings the body back up.",
  { { Pid::thr, -22.0f }, { Pid::ratio, 0.75f }, { Pid::knee, 0.0f }, { Pid::det, 1.0f }, { Pid::rel, 150.0f } } },

{ "00000000-0000-4000-8000-000000000093", "console-e", "Kick Tight", "Drums",
  "Fast 1 ms attack, 4:1 hard knee and a 0.1 s release on the peak detector: holds the kick's level steady and "
  "shortens its tail.",
  { { Pid::thr, -20.0f }, { Pid::ratio, 0.75f }, { Pid::knee, 0.0f }, { Pid::atk, 1.0f }, { Pid::det, 1.0f },
    { Pid::rel, 100.0f } } },

{ "00000000-0000-4000-8000-000000000094", "console-e", "Bass Even", "Bass",
  "4:1 OverEasy on the RMS detector with a 0.6 s release: evens out a bass part's note-to-note level without "
  "flattening the attack.",
  { { Pid::thr, -26.0f }, { Pid::ratio, 0.75f }, { Pid::rel, 600.0f } } },

{ "00000000-0000-4000-8000-000000000095", "console-e", "Acoustic Gentle", "Instrument",
  "2:1 OverEasy with the auto attack and a slow release: acoustic guitar or piano, a few dB of leveling that stays "
  "out of the way.",
  { { Pid::thr, -28.0f }, { Pid::ratio, 0.5f }, { Pid::rel, 800.0f } } },

{ "00000000-0000-4000-8000-000000000096", "console-e", "Room Pump", "Drums",
  "Infinite ratio, fast attack and the linear release on the peak detector, with the side-chain HPF at 100 Hz: the "
  "room mic pulls down on every hit and climbs back at a steady rate.",
  { { Pid::thr, -34.0f }, { Pid::ratio, 1.0f }, { Pid::knee, 0.0f }, { Pid::atk, 1.0f }, { Pid::det, 1.0f },
    { Pid::rel, 400.0f }, { Pid::tmode, 1.0f }, { Pid::schpf, 100.0f } } },

{ "00000000-0000-4000-8000-000000000097", "console-e", "Channel Limit", "Limiter",
  "Infinite ratio, hard knee and the fast attack on the peak detector, high on the dial: a channel-strip limiter "
  "that catches the loudest peaks only.",
  { { Pid::thr, -10.0f }, { Pid::ratio, 1.0f }, { Pid::knee, 0.0f }, { Pid::atk, 1.0f }, { Pid::det, 1.0f },
    { Pid::rel, 100.0f } } },
