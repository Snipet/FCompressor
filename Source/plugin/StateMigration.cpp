// Source/plugin/StateMigration.cpp: the stateVersion migrations table (01 §9.1 load step 2; P2, S8). See State.h.
//
// kMigrations[i] rewrites a stateVersion i+1 <PARAMS> tree as a stateVersion i+2 one; loadState runs entries
// v-1 .. kStateVersion-2 of a version-v session in order, before it reads a value. The table has exactly
// fcdsp::kStateVersion - 1 entries: none at v1.
//
// Adding a version (01 §9.1; the only reasons are a stored plain value whose meaning changes, or a renamed or removed
// parameter; an added parameter is never one: absent means default):
//   1. bump fcdsp::kStateVersion (Source/fcdsp/modes/ModeDescriptor.h; a lead-approved revision, it is frozen);
//   2. append ONE function here that edits only what changed, e.g. v1 -> v2 renaming "sce" to "scemph":
//          void v1ToV2(juce::ValueTree& params)
//          {
//              for (juce::ValueTree p : params)
//                  if (p.hasType("PARAM") && p.getProperty("id").toString() == "sce")
//                      p.setProperty("id", "scemph", nullptr);
//          }
//      and list it in kMigrations;
//   3. add a released v1 fixture for it to tests/fixtures/state/ (write-once, from the shipped build; proc.fixtures).
// A migration never touches the stateVersion/modeId/modeRev attributes (loadState reads them before it migrates) and
// never throws: a tree it does not recognise is left as it is.
#include "plugin/State.h"

#include "fcdsp/modes/ModeDescriptor.h"

#include <array>
#include <cstddef>

namespace fcmp
{
    namespace
    {
        constexpr std::size_t kNumMigrations = static_cast<std::size_t>(fcdsp::kStateVersion) - 1u;
        static_assert(fcdsp::kStateVersion >= 1, "stateVersion starts at 1 (01 §9.1)");

        constexpr std::array<StateMigration, kNumMigrations> kMigrations{};

        consteval bool noNullMigration()
        {
            for (const StateMigration m : kMigrations)
                if (m == nullptr)
                    return false;
            return true;
        }
        static_assert(noNullMigration(), "State: every stateVersion step needs its migration (kMigrations)");
    } // namespace

    std::span<const StateMigration> stateMigrations() noexcept
    {
        return { kMigrations.data(), kMigrations.size() };
    }
} // namespace fcmp
