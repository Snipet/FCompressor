// Source/plugin/factory/mu-mastering.inc: Mu Mastering's factory presets (slot 12: uuids ...00c1-...00cf). An Entry
// list expanded inside FactoryBank.cpp's bank (format and rules there); host plain units; unstated parameters take Mu
// Mastering's defaults. Values (MuMasteringDesc.cpp): THRESHOLD in dBFS on its 21 steps, dial = (-12 - thr) / 0.6
// (dial 10 = -18, 0 = -12, 20 = -24); RATIO COMPRESS ratio(1.5f) / LIMIT 0.75; ATTACK 20 ... 80 ms steps; RECOVERY
// 200 / 400 / 600 / 4000 / 8000 ms; INPUT (`drive`) -10 ... +10 dB; OUTPUT (`makeup`) dB, dial = makeup - 11.5;
// SC HPF OFF 0 / 100; LINK SEP 0 / LINK 1; STEREO 0 / M/S 1.

{ "00000000-0000-4000-8000-0000000000c1", "mu-mastering", "Mastering Mu Glue", "Master",
  "COMPRESS at dial 10 with the medium attack and a 0.6 s recovery, side-chain HPF in: a dB or two of slow, even tube "
  "glue on a mix.",
  { { Pid::thr, -18.0f }, { Pid::ratio, ratio(1.5f) }, { Pid::atk, 50.0f }, { Pid::rel, 600.0f },
    { Pid::schpf, 100.0f }, { Pid::makeup, 1.5f } } },

{ "00000000-0000-4000-8000-0000000000c2", "mu-mastering", "Gentle Master Limit", "Master",
  "LIMIT near the top of the dial with a 25 ms attack: 4:1 on the loudest passages, rising toward 20:1 only if they "
  "push past 12 dB.",
  { { Pid::thr, -12.0f }, { Pid::ratio, 0.75f }, { Pid::atk, 25.0f }, { Pid::rel, 400.0f }, { Pid::schpf, 100.0f },
    { Pid::makeup, 2.0f } } },

{ "00000000-0000-4000-8000-0000000000c3", "mu-mastering", "M/S Center Hold", "Master",
  "COMPRESS in M/S: the center and the sides levelled apart, so a loud chorus stays centred without the width "
  "collapsing.",
  { { Pid::thr, -18.6f }, { Pid::ratio, ratio(1.5f) }, { Pid::rel, 600.0f }, { Pid::stmode, 1.0f },
    { Pid::makeup, 1.0f } } },

{ "00000000-0000-4000-8000-0000000000c4", "mu-mastering", "Warm Bus Push", "Bus",
  "INPUT up 6 dB into COMPRESS, OUTPUT pulled back: more tube colour and a little more hold on a drum or music bus.",
  { { Pid::thr, -18.0f }, { Pid::ratio, ratio(1.5f) }, { Pid::atk, 40.0f }, { Pid::rel, 400.0f },
    { Pid::drive, 6.0f }, { Pid::makeup, -4.0f } } },

{ "00000000-0000-4000-8000-0000000000c5", "mu-mastering", "Vocal Mu", "Vocal",
  "LIMIT at dial 13 with a 30 ms attack and a 0.6 s recovery: a vocal held forward, the loudest lines reined in hard.",
  { { Pid::thr, -19.8f }, { Pid::ratio, 0.75f }, { Pid::atk, 30.0f }, { Pid::rel, 600.0f }, { Pid::makeup, 5.0f } } },

{ "00000000-0000-4000-8000-0000000000c6", "mu-mastering", "Slow Program Leveler", "Master",
  "COMPRESS with the slowest attack and the 4 s recovery: the mix's level rides up and down over whole sections, not "
  "notes.",
  { { Pid::thr, -21.0f }, { Pid::ratio, ratio(1.5f) }, { Pid::atk, 70.0f }, { Pid::rel, 4000.0f },
    { Pid::makeup, 3.0f } } },

{ "00000000-0000-4000-8000-0000000000c7", "mu-mastering", "Parallel Mu Squash", "Parallel",
  "LIMIT at the bottom of the dial, INPUT up, fastest attack and recovery, blended at 50 %: dense tube body under the "
  "dry signal.",
  { { Pid::thr, -24.0f }, { Pid::ratio, 0.75f }, { Pid::atk, 20.0f }, { Pid::rel, 200.0f }, { Pid::drive, 4.0f },
    { Pid::makeup, 10.0f }, { Pid::mix, 0.5f } } },
