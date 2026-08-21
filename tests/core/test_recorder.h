#ifndef QTERMX_TEST_RECORDER_H
#define QTERMX_TEST_RECORDER_H

// The C++ port of tests/recorder.py: a recorder handler implementing the
// dispatcher protocol. Records every dispatcher call as an Event; the
// parser tests' oracle.

#include <string>
#include <vector>

#include "dispatcher.h"
#include "parser.h"

namespace qtermx::test {

struct Event {
    std::string type;          // "chars", "execute", "csi_dispatch", ...
    std::u32string text;       // chars / osc payload
    int code = -1;             // execute
    std::string intermediates; // csi / esc / charset
    std::string prefix;        // csi
    Params params;             // csi
    std::string final;         // csi / esc / charset

    bool operator==(const Event& other) const
    {
        return type == other.type && text == other.text && code == other.code &&
               intermediates == other.intermediates && prefix == other.prefix &&
               params == other.params && final == other.final;
    }
};

inline Event charsEvent(std::u32string text)
{
    Event e;
    e.type = "chars";
    e.text = std::move(text);
    return e;
}

inline Event execEvent(int code)
{
    Event e;
    e.type = "execute";
    e.code = code;
    return e;
}

inline Event csiEvent(std::string intermediates, std::string prefix, Params params,
                      std::string final)
{
    Event e;
    e.type = "csi_dispatch";
    e.intermediates = std::move(intermediates);
    e.prefix = std::move(prefix);
    e.params = std::move(params);
    e.final = std::move(final);
    return e;
}

inline Event escEvent(std::string intermediates, std::string final)
{
    Event e;
    e.type = "escape_dispatch";
    e.intermediates = std::move(intermediates);
    e.final = std::move(final);
    return e;
}

inline Event charsetEvent(std::string designator, std::string charset)
{
    Event e;
    e.type = "designate_charset";
    e.intermediates = std::move(designator);
    e.final = std::move(charset);
    return e;
}

inline Event oscEvent(std::u32string payload)
{
    Event e;
    e.type = "osc_dispatch";
    e.text = std::move(payload);
    return e;
}

// Human-readable form for failure messages.
inline std::string eventToString(const Event& e)
{
    std::string s = e.type + "(";
    if (e.type == "chars" || e.type == "osc_dispatch") {
        s += "text=" + encodeUtf8(e.text);
    } else if (e.type == "execute") {
        s += "code=" + std::to_string(e.code);
    } else if (e.type == "csi_dispatch") {
        s += "inter=" + e.intermediates + " prefix=" + e.prefix + " final=" + e.final;
        s += " params=[";
        for (const auto& group : e.params.groups) {
            s += "(";
            for (size_t i = 0; i < group.size(); ++i) {
                if (i) {
                    s += ",";
                }
                s += std::to_string(group[i]);
            }
            s += ")";
        }
        s += "]";
    } else {
        s += "inter=" + e.intermediates + " final=" + e.final;
    }
    return s + ")";
}

inline std::string eventsToString(const std::vector<Event>& events)
{
    std::string s = "[";
    for (size_t i = 0; i < events.size(); ++i) {
        if (i) {
            s += ", ";
        }
        s += eventToString(events[i]);
    }
    return s + "]";
}

// Assert actual == expected; on mismatch throw with a readable message.
inline void expectEvents(const std::vector<Event>& actual, std::vector<Event> expected)
{
    if (actual != expected) {
        throw CheckFailed{"expected " + eventsToString(expected) +
                          "\n       got      " + eventsToString(actual)};
    }
}

class Recorder : public Dispatcher {
public:
    std::vector<Event> events;

    void chars(std::u32string text) override { events.push_back(charsEvent(std::move(text))); }
    void execute(int code) override { events.push_back(execEvent(code)); }
    void csiDispatch(std::string intermediates, std::string prefix, Params params,
                     std::string final) override
    {
        events.push_back(csiEvent(std::move(intermediates), std::move(prefix),
                                  std::move(params), std::move(final)));
    }
    void escapeDispatch(std::string intermediates, std::string final) override
    {
        events.push_back(escEvent(std::move(intermediates), std::move(final)));
    }
    void designateCharset(std::string designator, std::string charset) override
    {
        events.push_back(charsetEvent(std::move(designator), std::move(charset)));
    }
    void oscDispatch(std::u32string payload) override
    {
        events.push_back(oscEvent(std::move(payload)));
    }
};

// Feed text to a fresh parser and return the full recorded event sequence.
inline std::vector<Event> feed(std::u32string_view text)
{
    Recorder recorder;
    Parser parser(&recorder);
    parser.feed(text);
    parser.flush();
    return recorder.events;
}

// Feed bytes to a fresh parser and return the full recorded event sequence.
inline std::vector<Event> feedBytes(std::string_view data)
{
    Recorder recorder;
    Parser parser(&recorder);
    parser.feedBytes(data);
    parser.flush();
    return recorder.events;
}

// Parser subclass exposing internals — the pyqtermx analogue of xterm.js's
// TestEscapeSequenceParser (tests/parser/test_parser_states.py Probe).
class Probe : public Parser {
public:
    Probe() : Parser(&m_recorder) {}

    Recorder m_recorder;

    ParserState state() const { return m_state; }
    void setState(ParserState s) { m_state = s; }
    std::vector<std::vector<int64_t>> paramsGroups() const { return m_params.build().groups; }
    std::string intermediates() const { return m_intermediates; }
    std::string prefix() const { return m_prefix; }
    std::u32string osc() const { return m_oscBuffer; }
    std::vector<Event> events() const { return m_recorder.events; }

    // Dirty the collection buffers (xterm.js sets params/collect by hand).
    void dirty()
    {
        m_params.addDigit(2);
        m_params.addParam();
        m_params.addDigit(3); // groups ((2,), (3,))
        m_intermediates = "#";
        m_prefix = "?";
    }

    void setOsc(std::u32string s) { m_oscBuffer = std::move(s); }
};

// Feed `text` to a fresh parser forced into `state` (xterm.js style).
inline Probe feedFrom(ParserState state, std::u32string_view text)
{
    Probe probe;
    probe.setState(state);
    probe.feed(text);
    probe.flush();
    return probe;
}

} // namespace qtermx::test

#endif // QTERMX_TEST_RECORDER_H