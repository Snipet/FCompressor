// Tools/web/Main.cpp: fcmp_web_check's entry point (see WebCheck.h): `fcmp_web_check <subcommand> [args...]`.
// With no argument, or `list`, it prints the subcommands.
#include "web/WebCheck.h"

#include <cstdio>
#include <cstring>

namespace fcmp::webcheck
{
    namespace
    {
        Command*& head() noexcept
        {
            static Command* h = nullptr;                // function-local: defined before any Registrar runs
            return h;
        }
    }

    Registrar::Registrar(Command& c) noexcept
    {
        c.next = head();
        head() = &c;
    }

    const Command* first() noexcept { return head(); }
}

int main(int argc, char** argv)
{
    using fcmp::webcheck::Command;
    if (argc < 2 || std::strcmp(argv[1], "list") == 0)
    {
        std::printf("fcmp_web_check <subcommand> [args...]; subcommands:\n");
        for (const Command* c = fcmp::webcheck::first(); c != nullptr; c = c->next)
            std::printf("  %s\n", c->name);
        return argc < 2 ? 2 : 0;
    }
    for (const Command* c = fcmp::webcheck::first(); c != nullptr; c = c->next)
        if (std::strcmp(argv[1], c->name) == 0)
            return c->fn(argc - 1, argv + 1);
    std::fprintf(stderr, "fcmp_web_check: unknown subcommand '%s' (try `fcmp_web_check list`)\n", argv[1]);
    return 2;
}
