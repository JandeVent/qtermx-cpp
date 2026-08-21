#ifndef QTERMX_PARSER_H
#define QTERMX_PARSER_H

// Stream parser for ECMA-48 / VT terminal input: code points in,
// dispatcher calls out (port of pyqtermx parser.pyx).
//
//     bytes → UTF-8 decoder → code points → state machine → dispatcher calls
//
// The 15-state VT500 state machine must never desync: input can be split
// mid-sequence arbitrarily (never line-based parsing).

#include <string>

#include "dispatcher.h"
#include "params.h"
#include "utf8_decoder.h"

namespace qtermx {

enum class ParserState {
    GROUND = 0,
    ESCAPE = 1,
    ESCAPE_INTERMEDIATE = 2,
    CSI_ENTRY = 3,
    CSI_PARAM = 4,
    CSI_INTERMEDIATE = 5,
    CSI_IGNORE = 6,
    OSC_STRING = 7,
    CHARSET = 8,
    DCS_ENTRY = 9,
    DCS_PARAM = 10,
    DCS_INTERMEDIATE = 11,
    DCS_IGNORE = 12,
    SOS_PM_STRING = 13,
    APC_ENTRY = 14,
};

enum class Action {
    PRINT = 0,
    EXECUTE = 1,
    CLEAR = 2,
    IGNORE = 3,
    COLLECT = 4,
    PARAM = 5,
    CSI_DISPATCH = 6,
    ESC_DISPATCH = 7,
    CHARSET_DISPATCH = 8,
    OSC_START = 9,
    OSC_PUT = 10,
    OSC_END = 11,
    OSC_ABORT = 12,
};

class Parser {
public:
    explicit Parser(Dispatcher* dispatcher) : m_dispatcher(dispatcher) {}

    // Parse a run of code points, dispatching as it goes.
    void feed(std::u32string_view text);

    // Incrementally UTF-8 decode bytes, then parse them (ADR-0001).
    void feedBytes(std::string_view data);

    // Dispatch any pending printable run (the write boundary).
    void flush();

    // Return to GROUND and drop all collected state.
    void reset();

protected:
    // Internals exposed to the test Probe (port of the Python Probe
    // subclass in tests/parser/test_parser_states.py).
    Dispatcher* m_dispatcher;
    ParserState m_state = ParserState::GROUND;
    std::u32string m_printBuffer;
    ParamsBuilder m_params;
    std::string m_intermediates;
    std::string m_prefix;
    std::u32string m_oscBuffer;
    Utf8Decoder m_decoder;

private:
    void apply(Action action, char32_t cp);
    void flushPrint();
};

} // namespace qtermx

#endif // QTERMX_PARSER_H