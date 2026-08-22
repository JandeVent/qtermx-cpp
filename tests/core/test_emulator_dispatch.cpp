// Emulator-level dispatch completeness (port of tests/emulator/test_dispatch.py).
// Every final this phase claims must have a dispatch entry — so a
// half-wired sequence can never silently parse-and-ignore. The escape
// table is asserted the same way; the C0 execute set is covered
// behaviorally by the charset tests (SO/SI shift the active slot).
#include <set>

#include "emulator.h"
#include "harness.h"

using namespace qtermx;

namespace {

// The finals this phase claims, keyed by their family.
const std::set<std::string> kCsiFinals = {
    "h", "l", "r", "m", "A", "B", "C", "D", "E", "F", "H", "f", "G", "d",
    "J", "K", "X", "@", "L", "M", "P", "S", "T", "g", "I", "Z", "s", "u",
};

// The finals this phase claims on the escape path. Intermediate-bearing
// entries — DECALN `ESC # 8` — can't be expressed as bare finals; they
// are pinned by the exactness test instead.
const std::set<std::string> kEscapeFinals = {
    "D", "E", "M", "n", "o", "~", "}", "|", "H", "7", "8",
};

} // namespace

TEST_CASE(dispatch_every_claimed_csi_final_is_dispatched)
{
    // The Python oracle also probes that every table value resolves to a
    // callable handler (test_dispatch.py test_every_dispatch_entry_resolves
    // _to_a_handler) — in C++ that check is compile-time: the map values
    // are member-function pointers initialized with &Emulator::handler, so
    // a missing handler is a compile error, never a runtime null.
    std::set<std::string> dispatched;
    for (const auto& [key, handler] : Emulator::kCsiDispatch) {
        (void)handler;
        dispatched.insert(std::get<2>(key));
    }
    for (const auto& final : kCsiFinals) {
        QTERMX_CHECK(dispatched.find(final) != dispatched.end());
    }
}

TEST_CASE(dispatch_every_claimed_escape_final_is_dispatched)
{
    std::set<std::string> dispatched;
    for (const auto& [key, handler] : Emulator::kEscDispatch) {
        (void)handler;
        dispatched.insert(std::get<1>(key));
    }
    for (const auto& final : kEscapeFinals) {
        QTERMX_CHECK(dispatched.find(final) != dispatched.end());
    }
}

TEST_CASE(dispatch_escape_lookup_is_exact_no_bare_final_fallback)
{
    // Intermediate-bearing escapes dispatch exactly (ESC # 8 DECALN) —
    // they never fall back to the bare final (ESC 8 would restore the
    // cursor state).
    QTERMX_CHECK(Emulator::lookupEsc("#", "8") == &Emulator::decaln);
    QTERMX_CHECK(Emulator::lookupEsc("", "8") == &Emulator::decrc);
    QTERMX_CHECK(Emulator::lookupEsc("", "D") == &Emulator::ind);
}

TEST_CASE(dispatch_csi_final_mapping_uses_bare_final_fallback)
{
    // An entry with intermediates must not shadow the bare-final entry.
    for (const auto& [key, handler] : Emulator::kCsiDispatch) {
        (void)handler;
        const auto& [prefix, intermediates, final] = key;
        if (!intermediates.empty()) {
            // DECRPM (`? $ p`) is an exact-match entry — the bare-final
            // fallback for "p" is nothing (xterm.js registers it by
            // exact key, like the ESC table).
            if (prefix == "?" && intermediates == "$" && final == "p") {
                continue;
            }
            QTERMX_CHECK(Emulator::lookupCsi("", prefix, final) != nullptr);
        }
    }
}

TEST_CASE(dispatch_csi_lookup_applies_bare_final_fallback)
{
    // A sequence whose intermediates match no entry falls back to the
    // bare final (xterm.js rule).
    QTERMX_CHECK(Emulator::lookupCsi("!", "", "m") == &Emulator::sgr);
    QTERMX_CHECK(Emulator::lookupCsi("", "", "m") == &Emulator::sgr);
    QTERMX_CHECK(Emulator::lookupCsi("", "", "Z") == &Emulator::cbt);
    QTERMX_CHECK(Emulator::lookupCsi("", "?", "h") == &Emulator::decset);
    QTERMX_CHECK(Emulator::lookupCsi("", "", "q") == nullptr);
}