// Tools/probes/common/ProbeMain.cpp: main() of every probe executable (fcmp_probe_dsp, fcmp_probe_plugin; 03 §2.9,
// §3.2.2). Frozen at FZ0 with ProbeRegistry.h.
//
//   <exe> <layer>.<name> [--mode <key>] --golden-root <dir> --arch arm64|x86_64 [--bless-to <dir>] [--results <dir>]
//         [--only <glob>] [--quick] [--verbose]          run one probe (the harness reads every flag itself)
//   <exe> --list                                         the registered subcommands, one per line
//   <exe> --help
//
// It dispatches the subcommand over the FCMP_PROBE registration list, passes --mode to the probe as Ctx::key, creates
// the sandbox directories named by FCMP_PREFS_DIR / FCMP_PRESETS_DB (the probe's own; never the user's real
// ~/Library/Application Support/FCompressor), and runs the body under funkgui::test::ScopedFtz. An exception escaping
// the body is a harness error (exit 4). finish() is called whether or not the body called it, and the process exit
// code is always finish()'s (0 pass, 1 spec_fail, 2 golden_drift, 3 golden_missing, 4 harness_error; 03 §3.2.4). An
// unknown subcommand, a duplicate registration or a missing subcommand exits 4 without a RESULT line.
// JUCE-free: fcmp_probe_plugin's probes set up JUCE themselves.
#include "ProbeRegistry.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fcmp::probe
{
    namespace
    {
        constinit const Registration* gHead = nullptr;      // constant-initialised before any Registration runs
    }

    Registration::Registration(const char* layerDotName, ProbeFn f) noexcept
        : name(layerDotName), fn(f), next(gHead)
    {
        gHead = this;
    }

    const Registration* registrations() noexcept { return gHead; }
} // namespace fcmp::probe

namespace
{
    using fcmp::probe::Registration;

    constexpr int kExitHarnessError = 4;

    std::vector<const Registration*> sortedRegistrations()
    {
        std::vector<const Registration*> v;
        for (const Registration* r = fcmp::probe::registrations(); r != nullptr; r = r->next)
            v.push_back(r);
        std::sort(v.begin(), v.end(),
                  [](const Registration* a, const Registration* b) { return std::strcmp(a->name, b->name) < 0; });
        return v;
    }

    const char* baseName(const char* path)
    {
        const char* slash = std::strrchr(path, '/');
        return slash != nullptr ? slash + 1 : path;
    }

    void printUsage(std::FILE* f, const char* exe)
    {
        std::fprintf(f,
                     "usage: %s <layer>.<name> [--mode <key>] --golden-root <dir> --arch arm64|x86_64\n"
                     "       %*s [--bless-to <dir>] [--results <dir>] [--only <glob>] [--quick] [--verbose]\n"
                     "       %s --list | --help\n",
                     exe, static_cast<int>(std::strlen(exe)), "", exe);
    }

    void printList(std::FILE* f, const std::vector<const Registration*>& probes)
    {
        for (const Registration* r : probes)
            std::fprintf(f, "%s\n", r->name);
    }

    // The probe's own sandbox (03 §2.9: "created by the probe itself").
    std::string makeSandbox()
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (const char* dir = std::getenv("FCMP_PREFS_DIR"); dir != nullptr && *dir != '\0')
        {
            fs::create_directories(dir, ec);
            if (ec)
                return std::string("cannot create FCMP_PREFS_DIR ") + dir + ": " + ec.message();
        }
        if (const char* db = std::getenv("FCMP_PRESETS_DB"); db != nullptr && *db != '\0')
        {
            const fs::path parent = fs::path(db).parent_path();
            if (!parent.empty())
                fs::create_directories(parent, ec);
            if (ec)
                return "cannot create the directory of FCMP_PRESETS_DB " + std::string(db) + ": " + ec.message();
        }
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    const char* exe = baseName(argc > 0 && argv[0] != nullptr ? argv[0] : "fcmp_probe");
    const std::vector<const Registration*> probes = sortedRegistrations();

    for (std::size_t i = 1; i < probes.size(); ++i)
        if (std::strcmp(probes[i - 1]->name, probes[i]->name) == 0)
        {
            std::fprintf(stderr, "HARNESS ERROR  %s: probe %s is registered twice\n", exe, probes[i]->name);
            return kExitHarnessError;
        }

    if (argc < 2 || argv[1] == nullptr)
    {
        printUsage(stderr, exe);
        return kExitHarnessError;
    }
    const std::string_view command = argv[1];
    if (command == "--list")
    {
        printList(stdout, probes);
        return 0;
    }
    if (command == "--help" || command == "-h")
    {
        printUsage(stdout, exe);
        std::printf("probes:\n");
        printList(stdout, probes);
        return 0;
    }

    const auto it = std::find_if(probes.begin(), probes.end(),
                                 [&](const Registration* r) { return command == r->name; });
    if (it == probes.end())
    {
        std::fprintf(stderr, "HARNESS ERROR  %s: unknown probe '%.*s'; registered:\n", exe,
                     static_cast<int>(command.size()), command.data());
        printList(stderr, probes);
        return kExitHarnessError;
    }
    const Registration& probe = **it;

    fcmp::probe::Ctx ctx;
    for (int i = 2; i + 1 < argc; ++i)
        if (argv[i] != nullptr && std::string_view(argv[i]) == "--mode" && argv[i + 1] != nullptr)
        {
            ctx.key = argv[i + 1];
            break;
        }

    funkgui::test::Probe P(probe.name, ctx.key, argc, argv);
    if (const std::string err = makeSandbox(); !err.empty())
        P.harnessError(err);
    (void) fcmp::probe::rt::available();             // resolve the interposer (if any) before a probe arms it

    bool returned = false;
    int rc = 0;
    {
        const funkgui::test::ScopedFtz ftz;
        try
        {
            rc = probe.fn(P, ctx);
            returned = true;
        }
        catch (const std::exception& e)
        {
            P.harnessError(std::string("uncaught exception: ") + e.what());
        }
        catch (...)
        {
            P.harnessError("uncaught exception of unknown type");
        }
    }
    const int code = P.finish();                     // idempotent: the body's own call already decided the status
    if (returned && rc != code)
        std::printf("NOTE     %s returned %d; the exit code is finish()'s: %d\n", probe.name, rc, code);
    return code;
}
