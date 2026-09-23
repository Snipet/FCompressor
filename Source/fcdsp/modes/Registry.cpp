// The Mode registry (01 §8.1-8.2; SPRINTS D5). Modes.def is expanded three times, each time with both FCMP_MODE and
// FCMP_RETIRED defined around the #include:
//   1. the kEntry_<Traits> declarations (each Mode's TU defines its entry with FCDSP_DEFINE_MODE);
//   2. the registered slots, as address constants;
//   3. the retired slots.
// The raw-slot map (retired -> successor, unassigned -> clean) is computed from the two tables at compile time. Every
// table is constexpr, so it is constant-initialised: no lazy initialisation, no function-local statics, no static
// constructors (C D12). With no active line in Modes.def the registry is empty and every lookup is safe: modeSlots()
// is empty and resolveSlot returns the null ModeSlot{0, "", nullptr} until slot 0 is active. Every lookup is
// FCDSP_NONBLOCKING (FZ0 errata, R-F0 #1), repeated here from Registry.h.

#include "fcdsp/modes/Registry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string_view>

namespace fcdsp::modes {

// 1. Declarations: fcdsp::modes::kEntry_<Traits>, defined in the Mode's TU.
#define FCMP_MODE(slot, key, Traits) extern const ::fcdsp::ModeEntry kEntry_##Traits;
#define FCMP_RETIRED(slot, key, successor)
#include "fcdsp/modes/Modes.def"
#undef FCMP_RETIRED
#undef FCMP_MODE

namespace {

// 2. Registered slots in Modes.def order (= slot order, asserted below), then the null row.
constexpr ModeSlot kModeRows[] = {
#define FCMP_MODE(slot, key, Traits) ModeSlot{ slot, key, &kEntry_##Traits },
#define FCMP_RETIRED(slot, key, successor)
#include "fcdsp/modes/Modes.def"
#undef FCMP_RETIRED
#undef FCMP_MODE
    ModeSlot{ 0, "", nullptr } };

// 3. Retired slots in Modes.def order, then a terminator row.
constexpr Retired kRetiredRows[] = {
#define FCMP_MODE(slot, key, Traits)
#define FCMP_RETIRED(slot, key, successor) Retired{ slot, key, successor },
#include "fcdsp/modes/Modes.def"
#undef FCMP_RETIRED
#undef FCMP_MODE
    Retired{ 0, "", "" } };

constexpr std::size_t kNumModes   = std::size(kModeRows) - 1;
constexpr std::size_t kNumRetired = std::size(kRetiredRows) - 1;
constexpr std::size_t kNullRow    = kNumModes;              // index of ModeSlot{0, "", nullptr}
constexpr std::string_view kFallbackKey = "clean";          // where an unassigned raw slot resolves (01 §8.2)

static_assert(kNumModes + kNumRetired <= static_cast<std::size_t>(kModeCapacity), "Modes.def: more than 128 slots");

constexpr std::size_t findMode(std::string_view key) noexcept {       // index into kModeRows, or kNullRow
    for (std::size_t i = 0; i < kNumModes; ++i)
        if (kModeRows[i].key == key) return i;
    return kNullRow;
}

constexpr std::size_t findRetired(std::string_view key) noexcept {    // index into kRetiredRows, or kNumRetired
    for (std::size_t i = 0; i < kNumRetired; ++i)
        if (kRetiredRows[i].key == key) return i;
    return kNumRetired;
}

// Modes.def rules the compiler can check (01 §8.1, §8.3); the dsp.registry probe checks the rest at run time.
constexpr bool validKey(std::string_view key) noexcept {               // [a-z0-9-]{1,24}
    if (key.empty() || key.size() > 24) return false;
    for (const char ch : key)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')) return false;
    return true;
}

consteval bool modesDefIsWellFormed() {
    std::array<bool, kModeCapacity> slotUsed{};
    for (std::size_t i = 0; i < kNumModes; ++i) {
        const ModeSlot& m = kModeRows[i];
        if (m.slot >= kModeCapacity || slotUsed[m.slot] || !validKey(m.key) || m.entry == nullptr) return false;
        if (i > 0 && kModeRows[i - 1].slot >= m.slot) return false;           // modeSlots() is in slot order
        if (findMode(m.key) != i || findRetired(m.key) != kNumRetired) return false;   // keys never reused
        slotUsed[m.slot] = true;
    }
    for (std::size_t i = 0; i < kNumRetired; ++i) {
        const Retired& r = kRetiredRows[i];
        if (r.slot >= kModeCapacity || slotUsed[r.slot] || !validKey(r.key)) return false;
        if (findRetired(r.key) != i || findMode(r.successor) == kNullRow) return false;   // registered successor
        slotUsed[r.slot] = true;
    }
    return true;
}
static_assert(modesDefIsWellFormed(),
              "Modes.def: slots < 128 and unique, keys [a-z0-9-]{1,24} and unique, FCMP_MODE lines in ascending slot "
              "order, every retired key's successor registered");

// Raw slot -> kModeRows index: registered -> itself; retired -> its successor; unassigned -> clean, or the null row
// while clean is not registered. O(1), constant-initialised.
constexpr std::size_t kFallbackRow = findMode(kFallbackKey);
constexpr std::array<uint8_t, kModeCapacity> kSlotMap = [] {
    std::array<uint8_t, kModeCapacity> map{};
    map.fill(static_cast<uint8_t>(kFallbackRow));
    for (std::size_t i = 0; i < kNumModes; ++i)
        map[kModeRows[i].slot] = static_cast<uint8_t>(i);
    for (std::size_t i = 0; i < kNumRetired; ++i)
        map[kRetiredRows[i].slot] = static_cast<uint8_t>(findMode(kRetiredRows[i].successor));
    return map;
}();

} // namespace
} // namespace fcdsp::modes

namespace fcdsp {

std::span<const ModeSlot> modeSlots() noexcept FCDSP_NONBLOCKING {
    return { modes::kModeRows, modes::kNumModes };
}

const ModeEntry* bySlot(int slot) noexcept FCDSP_NONBLOCKING {
    if (slot < 0 || slot >= kModeCapacity) return nullptr;
    const ModeSlot& row = modes::kModeRows[modes::kSlotMap[static_cast<std::size_t>(slot)]];
    return row.slot == slot ? row.entry : nullptr;             // a retired or unassigned slot maps to another row
}

const ModeEntry* byKey(std::string_view key) noexcept FCDSP_NONBLOCKING {
    return modes::kModeRows[modes::findMode(key)].entry;       // the null row's entry is nullptr
}

const ModeSlot& resolveSlot(int rawSlot) noexcept FCDSP_NONBLOCKING {
    if (rawSlot < 0 || rawSlot >= kModeCapacity) return modes::kModeRows[modes::kFallbackRow];
    return modes::kModeRows[modes::kSlotMap[static_cast<std::size_t>(rawSlot)]];
}

const ModeSlot* resolveKey(std::string_view key) noexcept FCDSP_NONBLOCKING {
    std::size_t row = modes::findMode(key);
    if (row == modes::kNullRow) {
        const std::size_t r = modes::findRetired(key);
        if (r == modes::kNumRetired) return nullptr;           // unknown key
        row = modes::findMode(modes::kRetiredRows[r].successor);
    }
    return row == modes::kNullRow ? nullptr : &modes::kModeRows[row];
}

int slotOf(const ModeEntry& entry) noexcept FCDSP_NONBLOCKING {
    for (const ModeSlot& m : modeSlots())
        if (m.entry == &entry) return m.slot;
    return -1;
}

std::span<const Retired> retired() noexcept FCDSP_NONBLOCKING {
    return { modes::kRetiredRows, modes::kNumRetired };
}

} // namespace fcdsp
