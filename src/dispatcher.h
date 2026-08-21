#ifndef QTERMX_DISPATCHER_H
#define QTERMX_DISPATCHER_H

// The dispatcher protocol — the seam between parser and terminal state
// (port of pyqtermx dispatcher.py). The parser owns parsing; a dispatcher
// owns semantics. The screen (later) and the test recorder (now) both
// implement this protocol, so the parser is testable without any terminal
// state existing.

#include <cstdint>
#include <string>

#include "params.h"

namespace qtermx {

class Dispatcher {
public:
    virtual ~Dispatcher() = default;

    // A run of printable characters (code points).
    virtual void chars(std::u32string text) = 0;

    // A C0 control character, identified by its code point.
    virtual void execute(int code) = 0;

    // A complete control sequence. `intermediates` are the intermediate
    // bytes (0x20–0x2F), `prefix` is the private marker (`?`, `>`, `=` or
    // `<`) when present, `params` the typed parameters, and `final` the
    // final byte.
    virtual void csiDispatch(std::string intermediates, std::string prefix,
                             Params params, std::string final) = 0;

    // A complete escape sequence: intermediate bytes and final byte.
    virtual void escapeDispatch(std::string intermediates, std::string final) = 0;

    // A charset designation (ESC ( / ) / * / + ...). `designator` is the
    // intermediate identifying the slot (G0–G3) and `charset` the final
    // byte naming the charset.
    virtual void designateCharset(std::string designator, std::string charset) = 0;

    // A complete OSC string, payload delivered intact (code points).
    virtual void oscDispatch(std::u32string payload) = 0;
};

} // namespace qtermx

#endif // QTERMX_DISPATCHER_H