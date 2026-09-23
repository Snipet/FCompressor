// Value text for the UI and the host (01 §4.6; K1 #15; K2 #25a-b): formatParts/formatValue/formatHost/parseHost.
// Fixed buffers, no allocation, no locale: numbers are rounded with llround and written digit by digit, and parsed
// digit by digit, so a host that sets LC_NUMERIC cannot turn "−18.0" into "−18,0".
//
// The universal rule (01 §3.1 "Unit / host text"; choices where 01 is silent are listed in the S1 F2 handoff):
//   dB        thr, knee, range, drive, makeup, s2thr: 1 decimal ("−18.0" DB); range >= 60 and s2thr >= 24 print "OFF"
//   ratio     S -> R = 1/(1 - S): "4.0:1" (1 decimal below 10), "20:1", "−2.0:1", "∞:1" (S = 1, or |R| >= 1e5)
//   time      3 significant digits, trailing zeros dropped, units per 01 §3.1: atk, s2atk µS below 1 ms, else MS
//             ("250" µS, "10" MS); rel, s2rel MS below 1 s, else S ("200" MS, "1.2" S); hold, look MS ("0.8" MS);
//             0 prints "0" MS
//   HZ        schpf: below 20 prints "OFF", else 3 significant digits
//   DB/OCT    sce: 1 decimal;   %  link, mix: plain x 100, no decimals
//   index     tmode, det, stmode, voice: the integer;   boolean automu: "OFF"/"ON"
//   DisplayMap  a Mode scale (toDisplay, unit or decimals set): toDisplay(plain) with `decimals` (-1 -> 1) and `unit`
//             (nullptr -> the universal unit of kHostParams)
// A step prints its `text` (a trailing unit token splits off into FormattedValue::unit: "0.3 MS" -> "0.3" + "MS"); a
// text that does not fit the 23-byte value prints the label instead. Without a text, a numeric label ("50", ".3",
// "4") formats the step's plain value by the rule above and any other label prints as itself ("PEAK", "OFF").
// Spoken: step.spoken, else the text, else the label (a numeric step speaks its formatted value); numbers speak "minus"
// and the unit in words. Prefix: '~' for a locked or derived spec with kFlagProgram, else '(' locked, '=' derived.
//
// parseHost accepts, case-insensitively and with '-' or U+2212: the formatted text itself (a prefix, "(...)" and "= "
// are stripped), step labels and texts, "OFF"/"ON" where the universal text has them, "∞"/"INF" for ratio, Mode display
// numbers (bare or with the display unit), and universal numbers with the universal units (µS/US/MS/S, DB, HZ/KHZ,
// %, DB/OCT, ":1"). A value for a stepped spec (or a hybrid outside its range) is snapped to the canonical step plain
// (the host writes exact detents, like the UI); any other value is clamped to the host range by legal(). The n/a
// dash, an empty string and anything else are refused.

#include "fcdsp/params/Text.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace fcdsp {

namespace {

constexpr std::string_view kMinus = "−";      // every negative number (02 §4.6)
constexpr std::string_view kEnDash = "–";     // n/a (K1 #15)
constexpr std::string_view kInfinity = "∞";
constexpr std::string_view kMicro = "µ";
constexpr std::string_view kGreekMu = "μ";
constexpr const char* kUnitMicroSeconds = "µS";

// ---- bounded UTF-8 writer ------------------------------------------------------------------------------------------

// Appends into a fixed buffer, always NUL-terminated. A string that does not fit is cut at a UTF-8 boundary and
// nothing is appended after the cut.
class Writer {
public:
    Writer(char* buf, std::size_t cap) noexcept : buf_(buf), cap_(cap) {
        if (cap_ > 0)
            buf_[0] = '\0';
    }
    void put(std::string_view s) noexcept {
        if (full_ || cap_ == 0)
            return;
        const std::size_t avail = cap_ - 1 - n_;
        std::size_t take = s.size() <= avail ? s.size() : avail;
        if (take < s.size()) {
            full_ = true;
            while (take > 0 && (static_cast<unsigned char>(s[take]) & 0xC0u) == 0x80u)
                --take;                                    // never split a multi-byte sequence
        }
        if (take > 0)
            std::memcpy(buf_ + n_, s.data(), take);
        n_ += take;
        buf_[n_] = '\0';
    }
    void put(char c) noexcept { put(std::string_view(&c, 1)); }
    std::size_t size() const noexcept { return n_; }

private:
    char* buf_;
    std::size_t cap_;
    std::size_t n_ = 0;
    bool full_ = false;
};

// ---- numbers ---------------------------------------------------------------------------------------------------------

// |v| rounded to `decimals` places as ASCII digits ("18.0", "0.25"), plus the sign and the rounded magnitude.
struct Decimal {
    char digits[32];
    bool negative;          // v < 0 and the rounded value is not zero (no "−0.0")
    double magnitude;       // the rounded |v|
};

constexpr double kPow10[] = { 1.0, 10.0, 100.0, 1000.0, 1.0e4, 1.0e5, 1.0e6 };
constexpr double kMaxMagnitude = 1.0e12;            // callers never format more (host ranges are far smaller)

Decimal toDecimal(double v, int decimals, bool dropZeros) noexcept {
    Decimal d{};
    const int dp = decimals < 0 ? 0 : (decimals > 6 ? 6 : decimals);
    const double scale = kPow10[dp];
    double a = std::fabs(v);
    if (!(a <= kMaxMagnitude))
        a = kMaxMagnitude;
    const long long m = std::llround(a * scale);
    d.negative = v < 0.0 && m != 0;
    d.magnitude = static_cast<double>(m) / scale;

    // digits of m, most significant first, with the decimal point dp places from the right
    char rev[32];
    int n = 0;
    long long q = m;
    do {
        rev[n++] = static_cast<char>('0' + static_cast<int>(q % 10));
        q /= 10;
    } while (q > 0 || n <= dp);                         // at least one digit before the point
    int w = 0;
    for (int k = n - 1; k >= 0; --k) {
        d.digits[w++] = rev[k];
        if (k == dp && dp > 0)
            d.digits[w++] = '.';
    }
    if (dropZeros && dp > 0) {
        while (d.digits[w - 1] == '0')
            --w;
        if (d.digits[w - 1] == '.')
            --w;
    }
    d.digits[w] = '\0';
    return d;
}

// Three significant digits: 2 decimals below 10, 1 below 100, else 0; decided on the rounded magnitude, so 9.996
// prints "10" and not "10.00".
int sig3Decimals(double a) noexcept { return a < 10.0 ? 2 : (a < 100.0 ? 1 : 0); }

Decimal sig3(double v) noexcept {
    const int dp = sig3Decimals(std::fabs(v));
    const Decimal d = toDecimal(v, dp, true);
    const int dp2 = sig3Decimals(d.magnitude);
    return dp2 == dp ? d : toDecimal(v, dp2, true);
}

// The formatted parts under construction (FormattedValue's value/unit/spoken). With speak = false the spoken string
// is left to the caller (a step with its own Step::spoken).
struct Parts {
    Parts(char* valueBuf, std::size_t valueCap, char* spokenBuf, std::size_t spokenCap) noexcept
        : value(valueBuf, valueCap), spoken(spokenBuf, spokenCap) {}
    Writer value, spoken;
    const char* unit = "";
    bool speak = true;
};

// The unit in words, for the spoken string.
const char* spokenUnit(std::string_view unit) noexcept {
    if (unit == "DB")                      return "decibels";
    if (unit == "DBU")                     return "d B u";
    if (unit == "DB/OCT")                  return "decibels per octave";
    if (unit == "MS")                      return "milliseconds";
    if (unit == "S")                       return "seconds";
    if (unit == kUnitMicroSeconds)         return "microseconds";
    if (unit == "US")                      return "microseconds";
    if (unit == "HZ")                      return "hertz";
    if (unit == "KHZ")                     return "kilohertz";
    if (unit == "%")                       return "percent";
    return nullptr;
}

void putNumber(Parts& p, const Decimal& d) noexcept {
    if (d.negative) {
        p.value.put(kMinus);
        if (p.speak)
            p.spoken.put("minus ");
    }
    p.value.put(d.digits);
    if (p.speak)
        p.spoken.put(d.digits);
}

void putWord(Parts& p, std::string_view word, std::string_view spoken) noexcept {
    p.value.put(word);
    if (p.speak)
        p.spoken.put(spoken);
}

void finishSpokenUnit(Parts& p) noexcept {
    if (p.unit[0] == '\0' || !p.speak)
        return;
    const char* words = spokenUnit(p.unit);
    p.spoken.put(' ');
    p.spoken.put(words != nullptr ? words : p.unit);
}

// ---- the universal rule -----------------------------------------------------------------------------------------------

enum class Cat : uint8_t { db, ratio, time, hz, dbPerOct, percent, index, boolean };

// From kHostParams (the single source of the universal units, 01 §3.1).
Cat categoryOf(Pid pid) noexcept {
    const HostParam& h = kHostParams[idx(pid)];
    if (h.map == Map::ratio3)  return Cat::ratio;
    if (h.map == Map::index)   return Cat::index;
    if (h.map == Map::boolean) return Cat::boolean;
    const std::string_view u = h.unit;
    if (u == "MS")     return Cat::time;
    if (u == "HZ")     return Cat::hz;
    if (u == "DB/OCT") return Cat::dbPerOct;
    if (u == "%")      return Cat::percent;
    return Cat::db;
}

bool hasDisplayScale(const DisplayMap& m) noexcept {
    return m.toDisplay != nullptr || m.unit != nullptr || m.decimals >= 0;
}

// 01 §3.1's time units per parameter: atk and s2atk "µs below 1 ms, else ms"; rel and s2rel "ms below 1 s, else s";
// hold and look "ms". The unit is chosen on the rounded value (999.96 MS prints "1 S").
void formatTime(Parts& p, Pid pid, double ms) noexcept {
    const bool micro = pid == Pid::atk || pid == Pid::s2atk;
    const bool seconds = pid == Pid::rel || pid == Pid::s2rel;
    const double a = std::fabs(ms);
    if (a == 0.0) {
        putWord(p, "0", "0");
        p.unit = "MS";
        return;
    }
    if (micro && a < 1.0) {
        const Decimal d = sig3(ms * 1000.0);
        if (d.magnitude < 1000.0) {
            putNumber(p, d);
            p.unit = kUnitMicroSeconds;
            return;
        }
    }
    if (seconds && a >= 1000.0) {
        putNumber(p, sig3(ms / 1000.0));
        p.unit = "S";
        return;
    }
    const Decimal d = sig3(ms);
    if (seconds && d.magnitude >= 1000.0) {
        putNumber(p, sig3(ms / 1000.0));
        p.unit = "S";
        return;
    }
    putNumber(p, d);
    p.unit = "MS";
}

void formatRatio(Parts& p, double s) noexcept {
    const double r = s == 1.0 ? 0.0 : 1.0 / (1.0 - s);
    if (s == 1.0 || !(std::fabs(r) < 1.0e5)) {
        putWord(p, kInfinity, "infinity");
    } else {
        Decimal d = toDecimal(r, 1, false);
        if (d.magnitude >= 10.0)
            d = toDecimal(r, 0, false);
        putNumber(p, d);
    }
    putWord(p, ":1", " to 1");
}

// A plain value of `pid` under `spec`'s display scale or the universal rule.
void formatPlain(Parts& p, Pid pid, const ParamSpec& spec, float plain) noexcept {
    if (!std::isfinite(plain)) {
        putWord(p, kEnDash, "not available");
        return;
    }
    const HostParam& h = kHostParams[idx(pid)];
    const DisplayMap& m = spec.display;
    if (hasDisplayScale(m)) {
        const float shown = m.toDisplay != nullptr ? m.toDisplay(plain) : plain;
        putNumber(p, toDecimal(static_cast<double>(shown), m.decimals >= 0 ? m.decimals : 1, false));
        p.unit = m.unit != nullptr ? m.unit : h.unit;
        finishSpokenUnit(p);
        return;
    }
    const double v = static_cast<double>(plain);
    switch (categoryOf(pid)) {
        case Cat::db:
            if ((pid == Pid::range && plain >= kRangeOff) || (pid == Pid::s2thr && plain >= kS2Off)) {
                putWord(p, "OFF", "off");
                return;
            }
            putNumber(p, toDecimal(v, 1, false));
            p.unit = "DB";
            break;
        case Cat::ratio:
            formatRatio(p, v);
            return;
        case Cat::time:
            formatTime(p, pid, v);
            break;
        case Cat::hz:
            if (plain < 20.f) {
                putWord(p, "OFF", "off");
                return;
            }
            putNumber(p, sig3(v));
            p.unit = "HZ";
            break;
        case Cat::dbPerOct:
            putNumber(p, toDecimal(v, 1, false));
            p.unit = "DB/OCT";
            break;
        case Cat::percent:
            putNumber(p, toDecimal(v * 100.0, 0, false));
            p.unit = "%";
            break;
        case Cat::index:
            putNumber(p, toDecimal(v, 0, false));
            return;
        case Cat::boolean:
            if (plain >= 0.5f)
                putWord(p, "ON", "on");
            else
                putWord(p, "OFF", "off");
            return;
    }
    finishSpokenUnit(p);
}

// "4", ".3", "1.2", "−20": a label that names the step's number rather than a word.
bool numericLabel(std::string_view s) noexcept {
    if (s.substr(0, kMinus.size()) == kMinus)
        s.remove_prefix(kMinus.size());
    else if (!s.empty() && (s[0] == '-' || s[0] == '+'))
        s.remove_prefix(1);
    if (!s.empty() && s[0] == '.')
        s.remove_prefix(1);
    return !s.empty() && s[0] >= '0' && s[0] <= '9';
}

// The unit tokens a step text may end with ("0.3 MS", "TC6 .3/10/25 S").
bool unitToken(std::string_view s) noexcept {
    return s == "DB" || s == "DBU" || s == "DB/OCT" || s == "MS" || s == "S" || s == kUnitMicroSeconds || s == "HZ"
        || s == "KHZ" || s == "%";
}

constexpr std::size_t kValueBytes = sizeof(FormattedValue::value) - 1;
constexpr std::size_t kUnitBytes = sizeof(FormattedValue::unit) - 1;

void formatStep(Parts& p, Pid pid, const ParamSpec& spec, const Step& st, char* unitBuf) noexcept {
    const std::string_view label = st.label != nullptr ? st.label : "";
    if (st.text != nullptr) {
        std::string_view text = st.text;
        std::string_view unit;
        if (const std::size_t sp = text.rfind(' '); sp != std::string_view::npos && sp > 0
                                                    && unitToken(text.substr(sp + 1))) {
            unit = text.substr(sp + 1);
            text = text.substr(0, sp);
        }
        if (text.size() <= kValueBytes && unit.size() <= kUnitBytes) {
            p.value.put(text);
            std::memcpy(unitBuf, unit.data(), unit.size());
            unitBuf[unit.size()] = '\0';
            p.unit = unitBuf;
        } else {
            p.value.put(label);                           // "ATTACK OFF (NO GAIN REDUCTION, ...)" -> "OFF"
        }
        p.spoken.put(st.spoken != nullptr ? st.spoken : st.text);
        return;
    }
    if (numericLabel(label)) {                            // the step's number, by the display or universal rule
        p.speak = st.spoken == nullptr;
        formatPlain(p, pid, spec, st.plain);
        p.speak = true;
        if (st.spoken != nullptr)
            p.spoken.put(st.spoken);
        return;
    }
    p.value.put(label);
    p.spoken.put(st.spoken != nullptr ? std::string_view(st.spoken) : label);
}

void copyString(char* dst, std::size_t cap, std::string_view s) noexcept {
    Writer w(dst, cap);
    w.put(s);
}

} // namespace

void formatParts(const ParamView& view, Pid pid, FormattedValue& out) noexcept {
    assert(idx(pid) < kNumModeParams);
    out.value[0] = '\0';
    out.unit[0] = '\0';
    out.spoken[0] = '\0';
    out.prefix = 0;
    if (idx(pid) >= kNumModeParams)
        return;

    const std::size_t i = idx(pid);
    const ParamSpec* spec = view.spec[i];
    const ResolvedParam& r = view.p[i];
    if (spec == nullptr || r.state == SlotState::na) {
        copyString(out.value, sizeof out.value, kEnDash);
        copyString(out.spoken, sizeof out.spoken, "not applicable");
        return;
    }

    const bool program = (spec->flags & kFlagProgram) != 0
                      && (r.state == SlotState::locked || r.state == SlotState::derived);
    out.prefix = program                          ? '~'
               : r.state == SlotState::locked     ? '('
               : r.state == SlotState::derived    ? '='
                                                  : '\0';

    char unitBuf[sizeof out.unit] = {};
    Parts p(out.value, sizeof out.value, out.spoken, sizeof out.spoken);
    if (program)
        p.spoken.put("about ");
    if (r.step >= 0 && static_cast<std::size_t>(r.step) < spec->steps.size()) {
        formatStep(p, pid, *spec, spec->steps[static_cast<std::size_t>(r.step)], unitBuf);
    } else {
        formatPlain(p, pid, *spec, r.plain);
    }
    copyString(out.unit, sizeof out.unit, p.unit);
}

int formatValue(const ParamView& view, Pid pid, char* out, int cap) noexcept {
    if (out == nullptr || cap <= 0)
        return 0;
    FormattedValue f;
    formatParts(view, pid, f);
    Writer w(out, static_cast<std::size_t>(cap));
    const bool unit = f.unit[0] != '\0';
    switch (f.prefix) {
        case '~': w.put('~'); break;
        case '(': w.put('('); break;
        case '=': w.put("(= "); break;
        default: break;
    }
    w.put(f.value);
    if (unit) {
        w.put(' ');
        w.put(f.unit);
    }
    if (f.prefix == '(' || f.prefix == '=')
        w.put(')');
    return static_cast<int>(w.size());
}

int formatHost(const ModeEntry& entry, const RawParams& current, Pid pid, float plain, char* out, int cap) noexcept {
    assert(idx(pid) < kNumModeParams);
    if (out == nullptr || cap <= 0)
        return 0;
    if (idx(pid) >= kNumModeParams) {
        out[0] = '\0';
        return 0;
    }
    if (entry.desc == nullptr) {
        Writer w(out, static_cast<std::size_t>(cap));
        w.put(kEnDash);
        return static_cast<int>(w.size());
    }
    RawParams raw = current;
    raw.v[idx(pid)] = plain;
    ParamView view;
    resolveView(*entry.desc, raw, view);
    return formatValue(view, pid, out, cap);
}

namespace {

// ---- parsing -----------------------------------------------------------------------------------------------------------

constexpr std::size_t kParseBytes = 128;

// `in` normalised for matching into buf: ASCII upper case, U+2212 -> '-', µ (U+00B5, U+03BC) -> 'U', ∞ -> "INF",
// leading and trailing blanks dropped. Returns false when it does not fit.
bool normalise(std::string_view in, char (&buf)[kParseBytes], std::string_view& out) noexcept {
    std::size_t n = 0;
    const auto push = [&buf, &n](std::string_view s) noexcept {
        if (n + s.size() >= kParseBytes)
            return false;
        for (const char c : s)
            buf[n++] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
        return true;
    };
    std::size_t i = 0;
    while (i < in.size()) {
        const std::string_view rest = in.substr(i);
        bool ok = true;
        if (rest.substr(0, kMinus.size()) == kMinus) {
            ok = push("-");
            i += kMinus.size();
        } else if (rest.substr(0, kMicro.size()) == kMicro) {
            ok = push("U");
            i += kMicro.size();
        } else if (rest.substr(0, kGreekMu.size()) == kGreekMu) {
            ok = push("U");
            i += kGreekMu.size();
        } else if (rest.substr(0, kInfinity.size()) == kInfinity) {
            ok = push("INF");
            i += kInfinity.size();
        } else {
            ok = push(rest.substr(0, 1));
            ++i;
        }
        if (!ok)
            return false;
    }
    std::string_view s(buf, n);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    out = s;
    return true;
}

std::string_view trimmed(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return s;
}

// A readout pasted back: "(10 MS)", "(= 0.8 MS)", "~10 MS" -> "10 MS", "0.8 MS", "10 MS".
std::string_view stripDecorations(std::string_view s) noexcept {
    if (s.size() >= 2 && s.front() == '(' && s.back() == ')')
        s = trimmed(s.substr(1, s.size() - 2));
    if (!s.empty() && s.front() == '=')
        s = trimmed(s.substr(1));
    if (!s.empty() && s.front() == '~')
        s = trimmed(s.substr(1));
    return s;
}

bool sameText(std::string_view normalisedInput, const char* candidate) noexcept {
    if (candidate == nullptr)
        return false;
    char buf[kParseBytes];
    std::string_view c;
    return normalise(candidate, buf, c) && c == normalisedInput;
}

// A step of `spec` whose label or text is the input.
const Step* matchStep(const ParamSpec& spec, std::string_view s) noexcept {
    for (const Step& st : spec.steps)
        if (sameText(s, st.label) || sameText(s, st.text))
            return &st;
    return nullptr;
}

// [+-]digits[.digits] (or .digits) at the start of s, locale-free. `rest` is what follows, trimmed.
bool parseNumber(std::string_view s, double& v, std::string_view& rest) noexcept {
    std::size_t i = 0;
    bool negative = false;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
        negative = s[i] == '-';
        ++i;
    }
    long long mantissa = 0;
    int digits = 0, fraction = 0;
    bool point = false;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '.' && !point) {
            point = true;
            continue;
        }
        if (c < '0' || c > '9')
            break;
        if (digits < 15) {                                 // later digits are below float resolution
            mantissa = mantissa * 10 + (c - '0');
            ++digits;
            if (point)
                ++fraction;
        } else if (!point) {
            return false;                                  // an integer part this long is no parameter value
        }
    }
    if (digits == 0)
        return false;
    double x = static_cast<double>(mantissa);
    for (int k = 0; k < fraction; ++k)
        x /= 10.0;
    v = negative ? -x : x;
    rest = trimmed(s.substr(i));
    return true;
}

// A universal number with a universal unit -> host plain. false: the unit does not belong to this parameter.
bool universalPlain(Pid pid, double x, std::string_view unit, double& plain) noexcept {
    switch (categoryOf(pid)) {
        case Cat::db:
            if (unit.empty() || unit == "DB") { plain = x; return true; }
            return false;
        case Cat::ratio: {
            if (!(unit.empty() || unit == ":1" || unit == ": 1") || x == 0.0)
                return false;
            plain = 1.0 - 1.0 / x;
            return true;
        }
        case Cat::time:
            if (unit.empty() || unit == "MS") { plain = x; return true; }
            if (unit == "US")                 { plain = x / 1000.0; return true; }
            if (unit == "S")                  { plain = x * 1000.0; return true; }
            return false;
        case Cat::hz:
            if (unit.empty() || unit == "HZ") { plain = x; return true; }
            if (unit == "KHZ")                { plain = x * 1000.0; return true; }
            return false;
        case Cat::dbPerOct:
            if (unit.empty() || unit == "DB/OCT" || unit == "DB") { plain = x; return true; }
            return false;
        case Cat::percent:
            if (unit.empty() || unit == "%") { plain = x / 100.0; return true; }
            return false;
        case Cat::index:
            if (unit.empty()) { plain = std::round(x); return true; }
            return false;
        case Cat::boolean:
            if (unit.empty()) { plain = x >= 0.5 ? 1.0 : 0.0; return true; }
            return false;
    }
    return false;
}

// The words the universal text uses: OFF for range, s2thr, schpf and automu; ON for automu; ∞ for ratio.
bool universalWord(Pid pid, std::string_view s, double& plain) noexcept {
    if (s == "OFF") {
        if (pid == Pid::range)  { plain = kRangeOff; return true; }
        if (pid == Pid::s2thr)  { plain = kS2Off; return true; }
        if (pid == Pid::schpf)  { plain = 0.0; return true; }
        if (pid == Pid::automu) { plain = 0.0; return true; }
        return false;
    }
    if (s == "ON" && pid == Pid::automu) {
        plain = 1.0;
        return true;
    }
    if (categoryOf(pid) == Cat::ratio && (s == "INF" || s == "INF:1" || s == "INFINITY" || s == "INFINITY:1")) {
        plain = 1.0;
        return true;
    }
    return false;
}

} // namespace

bool parseHost(const ModeEntry& entry, const RawParams& current, Pid pid, std::string_view text,
               float& plainOut) noexcept {
    assert(idx(pid) < kNumModeParams);
    if (idx(pid) >= kNumModeParams || entry.desc == nullptr)
        return false;
    const std::size_t i = idx(pid);

    char buf[kParseBytes];
    std::string_view s;
    if (!normalise(text, buf, s) || s.empty())
        return false;
    s = stripDecorations(s);
    if (s.empty() || s == kEnDash)
        return false;

    // The active spec is the one the host text shows: resolve `current` (the value of pid itself never selects it).
    ParamView view;
    resolveView(*entry.desc, current, view);
    const ParamSpec& active = *view.spec[i];
    const ParamEntry& e = entry.desc->params.e[i];

    double plain = 0.0;
    bool ok = false;
    const Step* st = matchStep(active, s);
    if (st == nullptr)
        st = matchStep(e.spec, s);
    for (std::size_t k = 0; st == nullptr && k < e.variants.size(); ++k)
        st = matchStep(e.variants[k].spec, s);
    if (st != nullptr) {
        plain = static_cast<double>(st->plain);
        ok = true;
    }
    if (!ok)
        ok = universalWord(pid, s, plain);
    if (!ok) {
        double x = 0.0;
        std::string_view unit;
        if (!parseNumber(s, x, unit))
            return false;
        const DisplayMap& m = active.display;
        bool asDisplay = false;
        if (hasDisplayScale(m)) {
            char ubuf[kParseBytes];
            std::string_view displayUnit;
            const char* du = m.unit != nullptr ? m.unit : kHostParams[i].unit;
            asDisplay = unit.empty() || (normalise(du, ubuf, displayUnit) && unit == displayUnit);
        }
        if (asDisplay) {
            const float xf = static_cast<float>(x);
            plain = static_cast<double>(m.toPlain != nullptr ? m.toPlain(xf) : xf);
            ok = true;
        } else {
            ok = universalPlain(pid, x, unit, plain);
        }
    }
    if (!ok)
        return false;

    float p = static_cast<float>(plain);
    if (!std::isfinite(p))
        return false;
    if (active.kind == Kind::stepped || (active.kind == Kind::hybrid && (p < active.lo || p > active.hi)))
        p = snap(pid, active, p).plain;                    // exact detents, as the UI writes them (01 §4.5)
    plainOut = legal(pid, p);
    return true;
}

} // namespace fcdsp
