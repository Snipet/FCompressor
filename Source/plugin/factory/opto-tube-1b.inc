// Source/plugin/factory/opto-tube-1b.inc: Opto Tube 1B's factory presets (slot 13: uuids ...00d1-...00df). An Entry
// list expanded inside FactoryBank.cpp's bank (format and rules there); host plain units; unstated parameters take Opto
// Tube 1B's defaults. Values (OptoTube1BDesc.cpp): THRESHOLD in dBFS, dial = thr + 18 (+20 … -40; OFF = 24); RATIO in
// S, 2:1 0.5 … 10:1 0.9 (continuous); A/R SELECT MANUAL 0 / FIXED 1 / FIX-MAN 2; ATTACK 0.5-300 ms (FIX/MAN: DELAY);
// RELEASE 50-10000 ms; GAIN 0-30 dB; SC LOW CUT Hz (< 20 = OFF); MIX 0-1.

{ "00000000-0000-4000-8000-0000000000d1", "opto-tube-1b", "Tube Vocal Leveler", "Vocal",
  "3:1 on MANUAL with a 20 ms attack and a 0.3 s release: the smooth, forward opto-tube vocal. Set THRESHOLD for "
  "4-6 dB of gain reduction.",
  { { Pid::thr, -28.0f }, { Pid::ratio, 0.66667f }, { Pid::atk, 20.0f }, { Pid::rel, 300.0f },
    { Pid::makeup, 6.0f } } },

{ "00000000-0000-4000-8000-0000000000d2", "opto-tube-1b", "Fix/Man Vocal", "Vocal",
  "4:1 on FIX/MAN: consonants and short peaks are caught and let go fast; lines held longer than 60 ms settle into a "
  "1.2 s release.",
  { { Pid::thr, -26.0f }, { Pid::ratio, 0.75f }, { Pid::tmode, 2.0f }, { Pid::atk, 60.0f }, { Pid::rel, 1200.0f },
    { Pid::makeup, 5.0f } } },

{ "00000000-0000-4000-8000-0000000000d3", "opto-tube-1b", "Bass Opto Tube", "Bass",
  "5:1 on MANUAL with a 30 ms attack and a 0.4 s release: a round, even bass that keeps its front.",
  { { Pid::thr, -26.0f }, { Pid::ratio, 0.8f }, { Pid::atk, 30.0f }, { Pid::rel, 400.0f }, { Pid::makeup, 6.0f } } },

{ "00000000-0000-4000-8000-0000000000d4", "opto-tube-1b", "Acoustic Fixed", "Instrument",
  "2.5:1 on FIXED (1 ms / 50 ms): quick, transparent levelling for acoustic guitar strums.",
  { { Pid::thr, -24.0f }, { Pid::ratio, 0.6f }, { Pid::tmode, 1.0f }, { Pid::makeup, 4.0f } } },

{ "00000000-0000-4000-8000-0000000000d5", "opto-tube-1b", "Drum Bus Opto Tube", "Drums",
  "3:1 on MANUAL with a 50 ms attack, a 0.2 s release and the SC low cut at 80 Hz: the kit glued without the kick "
  "pumping it.",
  { { Pid::thr, -22.0f }, { Pid::ratio, 0.66667f }, { Pid::atk, 50.0f }, { Pid::rel, 200.0f }, { Pid::schpf, 80.0f },
    { Pid::makeup, 3.0f } } },

{ "00000000-0000-4000-8000-0000000000d6", "opto-tube-1b", "Parallel Tube Opto", "Parallel",
  "10:1 on FIXED, THRESHOLD at -22 on the dial, blended at 40 %: dense tube-opto body under the dry signal.",
  { { Pid::thr, -40.0f }, { Pid::ratio, 0.9f }, { Pid::tmode, 1.0f }, { Pid::makeup, 14.0f }, { Pid::mix, 0.4f } } },

{ "00000000-0000-4000-8000-0000000000d7", "opto-tube-1b", "Piano Smooth", "Instrument",
  "2:1 on FIX/MAN with a 100 ms DELAY and a 2 s release: hammer transients recover fast, sustained chords are held "
  "gently.",
  { { Pid::thr, -24.0f }, { Pid::ratio, 0.5f }, { Pid::tmode, 2.0f }, { Pid::atk, 100.0f }, { Pid::rel, 2000.0f },
    { Pid::makeup, 3.0f } } },
