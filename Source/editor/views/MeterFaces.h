// Source/editor/views/MeterFaces.h — the GR meter's face per Mode (ADR-76, v1.1): the band's HISTORY · VU meter
// (GrVuMeter, ADR-72) wears a face in the spirit of the hardware class each Mode models. The user asked that each
// Mode's analog GR meter look like the equivalent hardware. The faces are evocations of each class's meter — its plate,
// print, scale, needle and legend — drawn from general knowledge of those meters, with no maker's name or logo on
// any of them:
//   Clean       the panel's own face (ADR-72), in theme inks and the Mode colour: modern, no hardware
//   FET 76      a FET limiter's ivory VU: black print, a red zone above 0, a black needle, on a black panel
//   Opto 2A     a tube opto leveler's big backlit VU: warm cream face lit by its lamp, a silver-grey panel
//   Mu 67       a vari-mu tube compressor's black meter in a bright ring bezel, reading DB COMPRESSION up from 0
//   Diode 609   a diode-bridge compressor's dark grey-blue meter, cream print, GAIN REDUCTION up from 0 on the left
//   Bus G       a console bus compressor's black meter, white print, COMPRESSION 20 … 0 with the needle resting right
//   Bus 25      a modern console bus compressor's cream meter, black print, GAIN REDUCTION 20 … 0 resting right
//   Brickwall   a digital limiter's LED ladder: amber segments with a red top, fast (no needle)
//   Octo        the hybrid VCA's red gain-reduction LEDs: the ladder in red throughout (v1.2)
// Every face keeps the ADR-72 needle (mass-spring, 99 % in 300 ms, 1.25 % overshoot, the display clock), except the
// LED ladder, which shows the GR at once and falls at kLedFallDbPerS. The hardware plates keep their own fixed colours
// in both themes, as hardware does; a small lamp in the Mode colour (ADR-75) sits in each plate's corner. Everything
// is drawn inside the band's HISTORY plot rectangle.
//
// Scale positions. A face maps a reading (dB; −GR, so 0 = no gain reduction, and the VU faces also read +1 … +3) to a
// position x on its scale, 0 at the left end and 1 at the right, and the needle's angle from vertical is
// −E + 2E·x (E = layout::vu::kEndDeg). The needle is integrated in x (its own mass acts on the dial), so on a VU face
// (x linear in the linear gain, the classic GR-on-a-VU) and on a linear-dB face alike, a step takes 300 ms to 99 %.
//   vuGain     x = (g(r − hi) − g(lo − hi)) / (1 − g(lo − hi)), g(v) = 10^(v / 20), the ends lo = −20 and hi = +3
//   grLinear   x = GR / 20 (rest left) or 1 − GR / 20 (rest right), GR = −r in [0, 20]
//   led        the "position" is the GR in dB itself (0 … kLedMaxDb), one segment per dB
#pragma once

#include <funkgui/core/Col.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <string_view>

namespace fcmp::ui
{
    enum class FacePlate : unsigned char { panel, light, backlit, dark, ring, led };
    enum class FaceLaw : unsigned char { vuGain, grLinear, led };

    struct FaceMark
    {
        float       readingDb;                                    // −GR (VU faces: −20 … +3)
        const char* label;                                        // printed; nullptr: an unlabelled tick
    };

    struct MeterFace
    {
        std::string_view key;                                     // the Mode; "" = the panel face (Clean, unlisted)
        FacePlate plate = FacePlate::panel;
        FaceLaw   law = FaceLaw::vuGain;
        bool      restRight = true;                               // grLinear: 0 dB GR on the right end
        float     lowDb = -20.0f, highDb = 3.0f;                  // the readings at the scale's ends (vuGain)
        std::span<const FaceMark> marks;                          // labelled and unlabelled ticks, labels in order
        float     zoneFromDb = 1.0f;                              // vuGain: a red zone from here to highDb (> hi: none)
        funkgui::Col bezel, ring, face, print, zone, needle, glow, dim;   // dim: an unlit LED segment
        const char* legend = "";                                  // large, above the pivot
        const char* legend2 = "";                                 // small, under the legend
    };

    namespace meterface
    {
        inline constexpr float kLedMaxDb = 24.0f;                 // the ladder: 24 segments of 1 dB
        inline constexpr int   kLedSegments = 24;
        inline constexpr float kLedRedFromDb = 12.0f;             // segments from here up are red
        inline constexpr double kLedFallDbPerS = 20.0;            // the ladder falls this fast (attack is instant)

        // A VU face prints its numbers without signs, as the hardware does: 20 … 1 0 in the print ink, 1 2 3 above 0
        // in the red zone's ink (GrVuMeter::drawPlate).
        inline constexpr std::array<FaceMark, 16> kVuMarks { {
            { -20.0f, "20" }, { -10.0f, "10" }, { -7.0f, "7" }, { -5.0f, "5" }, { -3.0f, "3" }, { -2.0f, "2" },
            { -1.0f, "1" },   { 0.0f, "0" },    { 1.0f, "1" },  { 2.0f, "2" },  { 3.0f, "3" },
            { -15.0f, nullptr }, { -9.0f, nullptr }, { -8.0f, nullptr }, { -6.0f, nullptr }, { -4.0f, nullptr },
        } };
        inline constexpr std::array<FaceMark, 11> kGr4Marks { {    // 0 4 8 12 16 20, minor every 2
            { 0.0f, "0" }, { -4.0f, "4" }, { -8.0f, "8" }, { -12.0f, "12" }, { -16.0f, "16" }, { -20.0f, "20" },
            { -2.0f, nullptr }, { -6.0f, nullptr }, { -10.0f, nullptr }, { -14.0f, nullptr }, { -18.0f, nullptr },
        } };
        inline constexpr std::array<FaceMark, 11> kGr2Marks { {    // 0 2 4 … 20, all labelled
            { 0.0f, "0" }, { -2.0f, "2" }, { -4.0f, "4" }, { -6.0f, "6" }, { -8.0f, "8" }, { -10.0f, "10" },
            { -12.0f, "12" }, { -14.0f, "14" }, { -16.0f, "16" }, { -18.0f, "18" }, { -20.0f, "20" },
        } };
        inline constexpr std::array<FaceMark, 21> kGr5Marks { {    // 0 5 10 15 20, a ruler of 1 dB ticks
            { 0.0f, "0" }, { -5.0f, "5" }, { -10.0f, "10" }, { -15.0f, "15" }, { -20.0f, "20" },
            { -1.0f, nullptr }, { -2.0f, nullptr }, { -3.0f, nullptr }, { -4.0f, nullptr }, { -6.0f, nullptr },
            { -7.0f, nullptr }, { -8.0f, nullptr }, { -9.0f, nullptr }, { -11.0f, nullptr }, { -12.0f, nullptr },
            { -13.0f, nullptr }, { -14.0f, nullptr }, { -16.0f, nullptr }, { -17.0f, nullptr }, { -18.0f, nullptr },
            { -19.0f, nullptr },
        } };
        inline constexpr std::array<FaceMark, 9> kLedMarks { {     // under the ladder: GR 0 3 6 … 24
            { 0.0f, "0" }, { -3.0f, "3" }, { -6.0f, "6" }, { -9.0f, "9" }, { -12.0f, "12" }, { -15.0f, "15" },
            { -18.0f, "18" }, { -21.0f, "21" }, { -24.0f, "24" },
        } };

        constexpr funkgui::Col rgb(unsigned v) noexcept
        {
            return { static_cast<unsigned char>(v >> 16), static_cast<unsigned char>(v >> 8),
                     static_cast<unsigned char>(v) };
        }

        inline constexpr MeterFace kPanel {};                     // the panel face: GrVuMeter draws it from layout::vu

        inline constexpr std::array<MeterFace, 8> kFaces { {
            { "fet-76", FacePlate::light, FaceLaw::vuGain, true, -20.0f, 3.0f, kVuMarks, 0.0f,
              rgb(0x141414), rgb(0x141414), rgb(0xF2EEE3), rgb(0x1B1B1B), rgb(0xC62F24), rgb(0x111111), rgb(0xFFFFFF),
              rgb(0x000000), "VU", "GAIN REDUCTION" },
            { "opto-2a", FacePlate::backlit, FaceLaw::vuGain, true, -20.0f, 3.0f, kVuMarks, 0.0f,
              rgb(0x7E8186), rgb(0x5E6166), rgb(0xE6D39C), rgb(0x2A2418), rgb(0xB8321F), rgb(0x1E1A14), rgb(0xFFE9A8),
              rgb(0x000000), "VU", "GAIN REDUCTION" },
            { "mu-67", FacePlate::ring, FaceLaw::grLinear, false, -20.0f, 0.0f, kGr4Marks, 99.0f,
              rgb(0x2B2D31), rgb(0xC8CBD0), rgb(0x111214), rgb(0xF0EDE6), rgb(0x000000), rgb(0xEDE8DA), rgb(0x000000),
              rgb(0x000000), "DB", "COMPRESSION" },
            { "diode-609", FacePlate::dark, FaceLaw::grLinear, false, -20.0f, 0.0f, kGr5Marks, 99.0f,
              rgb(0x5C6B78), rgb(0x5C6B78), rgb(0x1F2328), rgb(0xE7DDC2), rgb(0x000000), rgb(0xF0E6CC), rgb(0x000000),
              rgb(0x000000), "GAIN REDUCTION", "DB" },
            { "bus-g", FacePlate::dark, FaceLaw::grLinear, true, -20.0f, 0.0f, kGr4Marks, 99.0f,
              rgb(0x2A2C30), rgb(0x2A2C30), rgb(0x141619), rgb(0xE9E6DE), rgb(0x000000), rgb(0xF2F0EA), rgb(0x000000),
              rgb(0x000000), "COMPRESSION", "DB" },
            { "bus-25", FacePlate::light, FaceLaw::grLinear, true, -20.0f, 0.0f, kGr2Marks, 99.0f,
              rgb(0x2F3A45), rgb(0x2F3A45), rgb(0xECE4CE), rgb(0x1E1E1E), rgb(0x000000), rgb(0x1A1A1A), rgb(0xFFFFFF),
              rgb(0x000000), "GAIN REDUCTION", "DB" },
            { "brickwall", FacePlate::led, FaceLaw::led, true, -24.0f, 0.0f, kLedMarks, 99.0f,
              rgb(0x2B2B2E), rgb(0x2B2B2E), rgb(0x0E0E10), rgb(0xC8C2B0), rgb(0xFF3B30), rgb(0xFFB000), rgb(0x000000),
              rgb(0x2E2410), "ATTENUATION", "DB" },
            { "octo", FacePlate::led, FaceLaw::led, true, -24.0f, 0.0f, kLedMarks, 99.0f,
              rgb(0x1C1C1E), rgb(0x1C1C1E), rgb(0x0B0B0C), rgb(0xC9C4B8), rgb(0xFF3B30), rgb(0xFF3B30), rgb(0x000000),
              rgb(0x2E1210), "GAIN REDUCTION", "DB" },
        } };
    }

    // The face of Mode `key`: its own, or the panel face (Clean and any Mode the table does not name).
    constexpr const MeterFace& faceFor(std::string_view key) noexcept
    {
        for (const MeterFace& f : meterface::kFaces)
            if (f.key == key)
                return f;
        return meterface::kPanel;
    }

    // The scale position of a reading on face f (see above); the LED face's position is the GR in dB.
    inline double facePos(const MeterFace& f, double readingDb) noexcept
    {
        switch (f.law)
        {
            case FaceLaw::vuGain:
            {
                const auto g = [](double v) { return std::pow(10.0, v / 20.0); };
                const double lo = g(static_cast<double>(f.lowDb) - static_cast<double>(f.highDb));
                return (g(readingDb - static_cast<double>(f.highDb)) - lo) / (1.0 - lo);
            }
            case FaceLaw::grLinear:
            {
                const double gr = -readingDb / 20.0;
                return f.restRight ? 1.0 - gr : gr;
            }
            case FaceLaw::led:
                return -readingDb;
        }
        return 0.0;
    }

    // The reading at scale position x on face f (the inverse of facePos; −inf below the VU law's zero).
    inline double faceReading(const MeterFace& f, double x) noexcept
    {
        switch (f.law)
        {
            case FaceLaw::vuGain:
            {
                const double lo = std::pow(10.0, (static_cast<double>(f.lowDb) - static_cast<double>(f.highDb)) / 20.0);
                const double g = lo + x * (1.0 - lo);
                return g > 0.0 ? 20.0 * std::log10(g) + static_cast<double>(f.highDb)
                               : -std::numeric_limits<double>::infinity();
            }
            case FaceLaw::grLinear:
                return -20.0 * (f.restRight ? 1.0 - x : x);
            case FaceLaw::led:
                return -x;
        }
        return 0.0;
    }
}
