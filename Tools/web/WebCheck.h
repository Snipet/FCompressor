// Tools/web/WebCheck.h: fcmp_web_check's subcommands (ADR-93; cmake/FcmpWeb.cmake).
//
// fcmp_web_check is one program built from every Tools/web/*.cpp, natively (against fcdsp as the plugin has it) and for
// the web (wasm32, run under node). Each file adds subcommands and declares the tests that run them:
//
//   // FCMP_WEB_TEST name=web.engine.print timeout=300 on=all args=print,{golden}
//   #include "web/WebCheck.h"
//   FCMP_WEB_COMMAND(print) { ... return 0; }          // int (int argc, char** argv): argv[0] is the subcommand
//
// A subcommand returns 0 for a pass and 1 for a failure, having printed what failed; 2 is a usage error. Output is plain
// lines on stdout: "PASS <name> ..." / "FAIL <name> ..." rows and "NOTE ..." lines, as the probes print, so a log reads
// the same. These are not Harness probes (FunkGui is not part of the web configuration yet): a test's verdict is its
// exit code, and anything compared with a golden is compared here, by the subcommand itself.
#pragma once

namespace fcmp::webcheck
{
    using CommandFn = int (*)(int argc, char** argv);

    struct Command
    {
        const char* name;
        CommandFn   fn;
        Command*    next;
    };

    // Registration at static-initialisation time (Tools/ may have static constructors; fcdsp may not).
    struct Registrar
    {
        explicit Registrar(Command& c) noexcept;
    };

    const Command* first() noexcept;
}

#define FCMP_WEB_COMMAND(ident)                                                                                        \
    static int fcmpWebCommand_##ident(int argc, char** argv);                                                          \
    static fcmp::webcheck::Command fcmpWebCommandRow_##ident { #ident, &fcmpWebCommand_##ident, nullptr };             \
    static const fcmp::webcheck::Registrar fcmpWebCommandReg_##ident { fcmpWebCommandRow_##ident };                    \
    static int fcmpWebCommand_##ident([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
