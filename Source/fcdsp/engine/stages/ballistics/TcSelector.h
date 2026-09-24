#pragma once

// stage::TcSelector: the Fairchild 670's TIME CONSTANT switch (01 §5.2 ballistics/ catalogue; 01 §10.7 Mu 67
// "TcSelector exposes TC1-4 as SmoothBranching {alpha r1, 1 - alpha} and TC5/TC6 as MultiStage3 branches, solved as the
// max of roots"; D §2.3; E §2.7). M4 (S10).
//
//     TC1-TC4   SmoothBranching: one attack and one release (EngineParams::atkTauMs / relTauMs, the step's times)
//     TC5, TC6  MultiStage3: the programme-dependent network, stage times from EngineParams::m[2..6] (MultiStage3.h)
//
// It is AutoSwitch<SmoothBranching, MultiStage3, TcSelect> (combinators/AutoSwitch.h): the switch position is not part
// of the kernel key (01 §5.5), so TC4 <-> TC5 swaps the ballistics inside one engine, and AutoSwitch hands the running
// GR to the other path (seeded from it: MultiStage3 starts charged) and crossfades the two over 20 ms with the host's
// smootherstep, so a switch while compressing never steps the gain (dsp.zipper's detent edges); the newly selected
// path lands on its targets (a value-initialised Coeffs). With the position constant it is bit-identical to the running
// path alone. The selector reads the TC5 / TC6 step tags (01 §10.7: kTagTc5 / kTagTc6 in EngineParams::tags, the OR of
// the active step tags; no other Mu 67 step carries them).
//
// Feedback: in FB each path forms its own affine maps; SmoothBranching::solveFb carries its sub-ulp steps through
// FbAffine::base (S10), and MultiStage3 copies that pattern for the winning stage (lead revision 3 at the S10 base), so
// a TC4 release (5 s) and TC6's 16 s stage follow their exponentials in the loop. MultiStage3 takes the exact commit
// with r^ (Stage.h HasCommitFbRhat), which AutoSwitch forwards. The time constants' published values are closed-loop
// (ADR-63): Mu67Desc.cpp's physical() converts the attack.

#include "fcdsp/core/Rt.h"
#include "fcdsp/engine/Stage.h"
#include "fcdsp/engine/stages/ballistics/MultiStage3.h"
#include "fcdsp/engine/stages/ballistics/SmoothBranching.h"
#include "fcdsp/engine/stages/combinators/AutoSwitch.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include <cstdint>

namespace fcdsp::stage {

// AutoSwitch's selector: B (MultiStage3) on the TC5 and TC6 positions.
struct TcSelect {
    static bool useB(const EngineParams& p) noexcept FCDSP_NONBLOCKING
    {
        return (p.tags & static_cast<uint32_t>(kTagTc5 | kTagTc6)) != 0;
    }
};

template <class Multi = MultiStage3>
using TcSelectorT = AutoSwitch<SmoothBranching, Multi, TcSelect>;

using TcSelector = TcSelectorT<>;

static_assert(BallisticsPolicy<TcSelector> && HasCommitFbRhat<TcSelector>);

} // namespace fcdsp::stage
