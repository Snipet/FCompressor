// Source/plugin/factory/octo.inc: Octo's factory presets (slot 8: uuids ...0081-...008f). An Entry list expanded inside
// FactoryBank.cpp's bank (format and rules there); host plain units; unstated parameters take Octo's defaults.
// Steps (OctoDesc.cpp): RATIO 1 2 3 4 6 10 (OPTO) 20 NUKE = S 0 / 0.5 / 0.66667 / 0.75 / 0.83333 / 0.9 / 0.95 / 1.0;
// AUDIO CLEAN 0, HP 1, DIST 2 = 2, DIST 2 + HP 3, DIST 3 = 4, DIST 3 + HP 5; SC HPF OFF 0 / HP 90; BAND EMPH OFF 0 /
// BE 1; LINK DUAL 0 / LINK 1. INPUT is the threshold's dBFS: dial 5 = -18, 4 dB per dial unit (dial 7 = -26).
// ATTACK 0.05-30 ms; RELEASE 50-3500 ms (10:1 OPTO: up to 20000 ms).

{ "00000000-0000-4000-8000-000000000081", "octo", "Vocal Octo", "Vocal",
  "3:1 with a medium attack and release and DIST 2 for a forward, warm vocal. INPUT sets 4-8 dB of gain reduction.",
  { { Pid::thr, -26.0f }, { Pid::ratio, 0.66667f }, { Pid::atk, 1.5f }, { Pid::rel, 400.0f }, { Pid::voice, 2.0f },
    { Pid::makeup, 5.0f } } },

{ "00000000-0000-4000-8000-000000000082", "octo", "Opto Vocal Smooth", "Vocal",
  "10:1 OPTO: a quick first release and a long tail after sustained notes, clean audio. Even, unobtrusive leveling.",
  { { Pid::thr, -24.0f }, { Pid::ratio, 0.9f }, { Pid::atk, 5.0f }, { Pid::rel, 3000.0f }, { Pid::makeup, 5.0f } } },

{ "00000000-0000-4000-8000-000000000083", "octo", "Band De-Ess", "Vocal",
  "6:1 with the 6 kHz band emphasis in the detector and fast times: sibilance pulls the gain down, the rest passes.",
  { { Pid::thr, -22.0f }, { Pid::ratio, 0.83333f }, { Pid::atk, 0.2f }, { Pid::rel, 80.0f }, { Pid::sce, 1.0f } } },

{ "00000000-0000-4000-8000-000000000084", "octo", "Room Nuke", "Drums",
  "NUKE at the fastest attack and a short release with DIST 3: the drum room mic, flattened and blown up.",
  { { Pid::thr, -34.0f }, { Pid::ratio, 1.0f }, { Pid::atk, 0.05f }, { Pid::rel, 120.0f }, { Pid::voice, 4.0f },
    { Pid::makeup, 12.0f } } },

{ "00000000-0000-4000-8000-000000000085", "octo", "Kick & Snare Punch", "Drums",
  "6:1 with a slow attack that lets the stick through and a fast release; DIST 2 and the detector HP keep the low end "
  "from pumping it.",
  { { Pid::thr, -26.0f }, { Pid::ratio, 0.83333f }, { Pid::atk, 12.0f }, { Pid::rel, 90.0f }, { Pid::voice, 2.0f },
    { Pid::schpf, 90.0f }, { Pid::makeup, 5.0f } } },

{ "00000000-0000-4000-8000-000000000086", "octo", "Opto Bass", "Bass",
  "10:1 OPTO with a 5 ms attack and DIST 2: a steady, growly bass that holds its level through long notes.",
  { { Pid::thr, -26.0f }, { Pid::ratio, 0.9f }, { Pid::atk, 5.0f }, { Pid::rel, 1500.0f }, { Pid::voice, 2.0f },
    { Pid::makeup, 6.0f } } },

{ "00000000-0000-4000-8000-000000000087", "octo", "Parallel Crush", "Parallel",
  "NUKE into DIST 3, the fastest times, blended at 40 %: density and grit under the untouched signal.",
  { { Pid::thr, -38.0f }, { Pid::ratio, 1.0f }, { Pid::atk, 0.05f }, { Pid::rel, 60.0f }, { Pid::voice, 4.0f },
    { Pid::makeup, 14.0f }, { Pid::mix, 0.4f } } },
