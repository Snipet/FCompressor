// FCMP_PROBE layer=dsp name=print scope=mode timeout=60
//
// dsp.print.<key> (F7, S6; 03 §3.4 "dsp.print", C §5.12; 01 §0 and K2 #10): the Mode's fingerprint through
// fcdsp::EngineHost at the plugin's default setup (STD, no lookahead, 48 kHz, blocks of 512): fixed program material
// rendered at the Mode's defaults and at three parameter sets, each output channel hashed (Harness hashFloats: FNV-1a
// over the float bit patterns). Golden rows only (exact): an exact refactor-neutrality proof. Once the Mode is listed
// in tests/fixtures/modes-ever.tsv, a change that moves print.default.* requires ModeDescriptor::revision++ and an
// entry in docs/modes/<key>.md (01 §0, §9.1).
//
// The program (4 s, stereo: a logarithmic sweep at -12 dBFS plus noise bursts) and the four parameter sets (default,
// lo, hi, mid) are Tools/probes/common/PrintProgram.h's, shared with the web engine check (ADR-93), which renders the
// same material through the C ABI and compares with these golden rows.
// Rows: print.<set>.<l|r>.hash (golden, exact); NOTE lines give each render's output RMS.
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "PrintProgram.h"

#include "fcdsp/engine/EngineHost.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace fcdsp;
    using funkgui::test::Probe;
    namespace printprog = fcmp::probe::printprog;

    using printprog::kFs;
    using printprog::Program;
    constexpr int kBlock = 512;

    std::pair<std::vector<float>, std::vector<float>> render(const ModeEntry& en, const RawParams& raw,
                                                             const Program& in)
    {
        HostConfig cfg;
        cfg.fs = kFs;
        cfg.maxBlock = kBlock;
        cfg.quality = Quality::std;
        cfg.budget = LookaheadBudget::off;
        BlockParams bp;
        bp.slot = static_cast<std::uint8_t>(slotOf(en));
        bp.eng = fcmp::probe::resolveRaw(en, raw).eng;
        auto host = std::make_unique<EngineHost>();
        host->configure(cfg, bp);
        const std::size_t n = in.l.size();
        std::vector<float> l(n), r(n);
        for (std::size_t off = 0; off < n; off += static_cast<std::size_t>(kBlock))
        {
            const std::size_t len = std::min<std::size_t>(static_cast<std::size_t>(kBlock), n - off);
            const float* ins[2] = { in.l.data() + off, in.r.data() + off };
            float* outs[2] = { l.data() + off, r.data() + off };
            ProcessIo io;
            io.in = ins;
            io.numIn = 2;
            io.out = outs;
            io.numOut = 2;
            io.n = static_cast<int>(len);
            host->process(io, bp);
        }
        return { l, r };
    }

    double rmsDb(const std::vector<float>& x)
    {
        double e = 0.0;
        for (const float v : x)
            e += static_cast<double>(v) * static_cast<double>(v);
        return 10.0 * std::log10(std::max(e / static_cast<double>(x.size()), 1e-30));
    }
} // namespace

FCMP_PROBE(dsp, print)
{
    const ModeEntry& en = fcmp::probe::modeEntry(C.key);
    const Program in = printprog::program();
    const auto sets = printprog::sets(en);
    for (const auto& [name, raw] : sets)
    {
        const auto [l, r] = render(en, raw, in);
        const std::string k = std::string("print.") + name;
        std::printf("NOTE     %s: output RMS %.4f / %.4f dBFS (L/R)\n", k.c_str(), rmsDb(l), rmsDb(r));
        P.hash(k + ".l.hash", funkgui::test::hashFloats(std::span<const float>(l)));
        P.hash(k + ".r.hash", funkgui::test::hashFloats(std::span<const float>(r)));
    }
    return P.finish();
}
