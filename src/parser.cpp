#include "parser.h"

#include <algorithm>
#include <array>

namespace qtermx {

// ============================================================================
// Character classification constants (mirror parser.pyx)
// ============================================================================

namespace {

constexpr int kAsciiPrintableLo = 0x20;
constexpr int kAsciiPrintableHi = 0x7E;
constexpr int kUnicodeMaxCodePoint = 0x10FFFF;
constexpr int kUnicodePrintableLo = 0xA0;

constexpr int kEscCharacter = 0x1B;
constexpr int kDelCharacter = 0x7F;
constexpr int kCanCharacter = 0x18;
constexpr int kSubCharacter = 0x1A;
constexpr int kBelCharacter = 0x07;

constexpr int kCsiFinalLo = 0x40;
constexpr int kCsiFinalHi = 0x7E;
constexpr int kIntermediateLo = 0x20;
constexpr int kIntermediateHi = 0x2F;
constexpr int kCsiParameterLo = 0x30;
constexpr int kCsiParameterHi = 0x3F;
constexpr int kCsiParameterDataHi = 0x3B;
constexpr int kCsiDigitLo = 0x30;
constexpr int kCsiDigitHi = 0x39;
constexpr int kCsiSubparameterSeparator = 0x3A;
constexpr int kCsiParameterSeparator = 0x3B;
constexpr int kCsiPrivatePrefixLo = 0x3C;
constexpr int kCsiPrivatePrefixHi = 0x3F;
constexpr int kCsiPrefinalLo = 0x20;
constexpr int kCsiPrefinalHi = 0x3F;
constexpr int kEscFinalLo = 0x30;
constexpr int kEscFinalHi = 0x7E;
constexpr int kCharsetDesignatorLo = 0x28;
constexpr int kCharsetDesignatorHi = 0x2B;

// C1 controls.
constexpr int kC1Dcs = 0x90;
constexpr int kC1Sos = 0x98;
constexpr int kC1Can = 0x99;
constexpr int kC1Sub = 0x9A;
constexpr int kC1Csi = 0x9B;
constexpr int kC1St = 0x9C;
constexpr int kC1Osc = 0x9D;
constexpr int kC1Pm = 0x9E;
constexpr int kC1Apc = 0x9F;

constexpr int kStateCount = 15;
constexpr int kSlotCount = 0xA0;

struct Rule {
    int lo;
    int hi;
    Action action;
    ParserState next;
};

struct Transition {
    Action action;
    ParserState next;
};

// Build the transition lookup table from the same rule list as
// parser.pyx's _build_lookup(): globals set the baseline for all states,
// per-state rules override them (later rules win), and rules reaching
// >= 0xA0 set the state's unicode default.
struct Lookup {
    std::array<std::array<Transition, kSlotCount>, kStateCount> table;
    std::array<Transition, kStateCount> unicodeDefaults;
};

constexpr Transition kDefaultTransition{Action::IGNORE, ParserState::GROUND};

constexpr Rule kGlobalRules[] = {
    {kCanCharacter, kCanCharacter, Action::EXECUTE, ParserState::GROUND},
    {kSubCharacter, kSubCharacter, Action::EXECUTE, ParserState::GROUND},
    {kC1Can, kC1Can, Action::EXECUTE, ParserState::GROUND},
    {kC1Sub, kC1Sub, Action::EXECUTE, ParserState::GROUND},
    {kEscCharacter, kEscCharacter, Action::CLEAR, ParserState::ESCAPE},
    {kC1St, kC1St, Action::IGNORE, ParserState::GROUND},
    {kC1Csi, kC1Csi, Action::CLEAR, ParserState::CSI_ENTRY},
    {kC1Osc, kC1Osc, Action::OSC_START, ParserState::OSC_STRING},
    {kC1Dcs, kC1Dcs, Action::CLEAR, ParserState::DCS_ENTRY},
    {kC1Sos, kC1Sos, Action::IGNORE, ParserState::SOS_PM_STRING},
    {kC1Pm, kC1Pm, Action::IGNORE, ParserState::SOS_PM_STRING},
    {kC1Apc, kC1Apc, Action::CLEAR, ParserState::APC_ENTRY},
    {0x80, 0x8F, Action::EXECUTE, ParserState::GROUND},
    {0x91, 0x97, Action::EXECUTE, ParserState::GROUND},
};

// Per-state rules, in the same order as parser.pyx (later wins).
constexpr Rule kGroundRules[] = {
    {kAsciiPrintableLo, kAsciiPrintableHi, Action::PRINT, ParserState::GROUND},
    {kUnicodePrintableLo, kUnicodeMaxCodePoint, Action::PRINT, ParserState::GROUND},
    {kEscCharacter, kEscCharacter, Action::CLEAR, ParserState::ESCAPE},
    {kDelCharacter, kDelCharacter, Action::IGNORE, ParserState::GROUND},
    {0x00, 0x17, Action::EXECUTE, ParserState::GROUND},
    {0x19, 0x19, Action::EXECUTE, ParserState::GROUND},
    {0x1C, 0x1F, Action::EXECUTE, ParserState::GROUND},
};

constexpr Rule kEscapeRules[] = {
    {kIntermediateLo, kIntermediateHi, Action::COLLECT, ParserState::ESCAPE_INTERMEDIATE},
    {0x30, 0x4F, Action::ESC_DISPATCH, ParserState::GROUND},
    {0x51, 0x57, Action::ESC_DISPATCH, ParserState::GROUND},
    {0x59, 0x5A, Action::ESC_DISPATCH, ParserState::GROUND},
    {0x5C, 0x5C, Action::ESC_DISPATCH, ParserState::GROUND},
    {0x60, 0x7E, Action::ESC_DISPATCH, ParserState::GROUND},
    {0x5B, 0x5B, Action::CLEAR, ParserState::CSI_ENTRY},
    {0x5D, 0x5D, Action::OSC_START, ParserState::OSC_STRING},
    {0x50, 0x50, Action::CLEAR, ParserState::DCS_ENTRY},
    {0x58, 0x58, Action::IGNORE, ParserState::SOS_PM_STRING},
    {0x5E, 0x5E, Action::IGNORE, ParserState::SOS_PM_STRING},
    {0x5F, 0x5F, Action::CLEAR, ParserState::APC_ENTRY},
    {kDelCharacter, kDelCharacter, Action::IGNORE, ParserState::ESCAPE},
    {0x00, 0x17, Action::EXECUTE, ParserState::ESCAPE},
    {0x19, 0x19, Action::EXECUTE, ParserState::ESCAPE},
    {0x1C, 0x1F, Action::EXECUTE, ParserState::ESCAPE},
    // Charset designators override the generic intermediate range rule,
    // so they must come last (later wins).
    {0x28, 0x28, Action::COLLECT, ParserState::CHARSET},
    {0x29, 0x29, Action::COLLECT, ParserState::CHARSET},
    {0x2A, 0x2A, Action::COLLECT, ParserState::CHARSET},
    {0x2B, 0x2B, Action::COLLECT, ParserState::CHARSET},
};

constexpr Rule kEscapeIntermediateRules[] = {
    {kIntermediateLo, kIntermediateHi, Action::COLLECT, ParserState::ESCAPE_INTERMEDIATE},
    {kEscFinalLo, kEscFinalHi, Action::ESC_DISPATCH, ParserState::GROUND},
    {kDelCharacter, kDelCharacter, Action::IGNORE, ParserState::ESCAPE_INTERMEDIATE},
    {0x00, 0x17, Action::EXECUTE, ParserState::ESCAPE_INTERMEDIATE},
    {0x19, 0x19, Action::EXECUTE, ParserState::ESCAPE_INTERMEDIATE},
    {0x1C, 0x1F, Action::EXECUTE, ParserState::ESCAPE_INTERMEDIATE},
};

constexpr Rule kCharsetRules[] = {
    {kIntermediateLo, kIntermediateHi, Action::COLLECT, ParserState::CHARSET},
    {kEscFinalLo, kEscFinalHi, Action::CHARSET_DISPATCH, ParserState::GROUND},
    {kDelCharacter, kDelCharacter, Action::IGNORE, ParserState::CHARSET},
    {0x00, 0x17, Action::EXECUTE, ParserState::CHARSET},
    {0x19, 0x19, Action::EXECUTE, ParserState::CHARSET},
    {0x1C, 0x1F, Action::EXECUTE, ParserState::CHARSET},
};

constexpr Rule kCsiEntryRules[] = {
    {kCsiFinalLo, kCsiFinalHi, Action::CSI_DISPATCH, ParserState::GROUND},
    {kCsiParameterLo, kCsiParameterDataHi, Action::PARAM, ParserState::CSI_PARAM},
    {kCsiPrivatePrefixLo, kCsiPrivatePrefixHi, Action::COLLECT, ParserState::CSI_PARAM},
    {kIntermediateLo, kIntermediateHi, Action::COLLECT, ParserState::CSI_INTERMEDIATE},
    {kDelCharacter, kDelCharacter, Action::IGNORE, ParserState::CSI_ENTRY},
    {0x00, 0x17, Action::EXECUTE, ParserState::CSI_ENTRY},
    {0x19, 0x19, Action::EXECUTE, ParserState::CSI_ENTRY},
    {0x1C, 0x1F, Action::EXECUTE, ParserState::CSI_ENTRY},
};

constexpr Rule kCsiParamRules[] = {
    {kCsiFinalLo, kCsiFinalHi, Action::CSI_DISPATCH, ParserState::GROUND},
    {kCsiParameterLo, kCsiParameterDataHi, Action::PARAM, ParserState::CSI_PARAM},
    {kCsiPrivatePrefixLo, kCsiPrivatePrefixHi, Action::IGNORE, ParserState::CSI_IGNORE},
    {kIntermediateLo, kIntermediateHi, Action::COLLECT, ParserState::CSI_INTERMEDIATE},
    {kDelCharacter, kDelCharacter, Action::IGNORE, ParserState::CSI_PARAM},
    {0x00, 0x17, Action::EXECUTE, ParserState::CSI_PARAM},
    {0x19, 0x19, Action::EXECUTE, ParserState::CSI_PARAM},
    {0x1C, 0x1F, Action::EXECUTE, ParserState::CSI_PARAM},
};

constexpr Rule kCsiIntermediateRules[] = {
    {kIntermediateLo, kIntermediateHi, Action::COLLECT, ParserState::CSI_INTERMEDIATE},
    {kCsiParameterLo, kCsiParameterHi, Action::IGNORE, ParserState::CSI_IGNORE},
    {kCsiFinalLo, kCsiFinalHi, Action::CSI_DISPATCH, ParserState::GROUND},
};

constexpr Rule kCsiIgnoreRules[] = {
    {kCsiPrefinalLo, kCsiPrefinalHi, Action::IGNORE, ParserState::CSI_IGNORE},
    {kCsiFinalLo, kCsiFinalHi, Action::IGNORE, ParserState::GROUND},
    {kDelCharacter, kDelCharacter, Action::IGNORE, ParserState::CSI_IGNORE},
};

// OSC: BEL terminates (OSC_END), but C0 range includes it. BEL must come
// last so the per-state rule wins over the generic C0 ignore.
constexpr Rule kOscStringRules[] = {
    {kC1St, kC1St, Action::OSC_END, ParserState::GROUND},
    {kEscCharacter, kEscCharacter, Action::OSC_END, ParserState::ESCAPE},
    {kCanCharacter, kCanCharacter, Action::OSC_ABORT, ParserState::GROUND},
    {kSubCharacter, kSubCharacter, Action::OSC_ABORT, ParserState::GROUND},
    {kAsciiPrintableLo, kAsciiPrintableHi, Action::OSC_PUT, ParserState::OSC_STRING},
    {kDelCharacter, kDelCharacter, Action::OSC_PUT, ParserState::OSC_STRING},
    {kUnicodePrintableLo, kUnicodeMaxCodePoint, Action::OSC_PUT, ParserState::OSC_STRING},
    {0x00, 0x06, Action::IGNORE, ParserState::OSC_STRING},
    {0x08, 0x17, Action::IGNORE, ParserState::OSC_STRING},
    {0x19, 0x19, Action::IGNORE, ParserState::OSC_STRING},
    {0x1C, 0x1F, Action::IGNORE, ParserState::OSC_STRING},
    {kBelCharacter, kBelCharacter, Action::OSC_END, ParserState::GROUND},
};

constexpr Rule kDcsEntryRules[] = {
    {kIntermediateLo, kIntermediateHi, Action::COLLECT, ParserState::DCS_INTERMEDIATE},
    {kCsiParameterLo, kCsiParameterHi, Action::IGNORE, ParserState::DCS_PARAM},
    {kCsiFinalLo, kCsiFinalHi, Action::IGNORE, ParserState::DCS_IGNORE},
    {kDelCharacter, kDelCharacter, Action::IGNORE, ParserState::DCS_ENTRY},
    {0x00, 0x17, Action::IGNORE, ParserState::DCS_ENTRY},
    {0x19, 0x19, Action::IGNORE, ParserState::DCS_ENTRY},
    {0x1C, 0x1F, Action::IGNORE, ParserState::DCS_ENTRY},
};

constexpr Rule kDcsParamRules[] = {
    {kIntermediateLo, kIntermediateHi, Action::COLLECT, ParserState::DCS_INTERMEDIATE},
    {kCsiParameterLo, kCsiParameterHi, Action::IGNORE, ParserState::DCS_PARAM},
    {kCsiFinalLo, kCsiFinalHi, Action::IGNORE, ParserState::DCS_IGNORE},
    {kDelCharacter, kDelCharacter, Action::IGNORE, ParserState::DCS_PARAM},
    {0x00, 0x17, Action::IGNORE, ParserState::DCS_PARAM},
    {0x19, 0x19, Action::IGNORE, ParserState::DCS_PARAM},
    {0x1C, 0x1F, Action::IGNORE, ParserState::DCS_PARAM},
};

constexpr Rule kDcsIntermediateRules[] = {
    {kIntermediateLo, kIntermediateHi, Action::COLLECT, ParserState::DCS_INTERMEDIATE},
    {kCsiParameterLo, kCsiParameterHi, Action::IGNORE, ParserState::DCS_IGNORE},
    {kCsiFinalLo, kCsiFinalHi, Action::IGNORE, ParserState::DCS_IGNORE},
    {kDelCharacter, kDelCharacter, Action::IGNORE, ParserState::DCS_INTERMEDIATE},
    {0x00, 0x17, Action::IGNORE, ParserState::DCS_INTERMEDIATE},
    {0x19, 0x19, Action::IGNORE, ParserState::DCS_INTERMEDIATE},
    {0x1C, 0x1F, Action::IGNORE, ParserState::DCS_INTERMEDIATE},
};

constexpr Rule kDcsIgnoreRules[] = {
    {kAsciiPrintableLo, kAsciiPrintableHi, Action::IGNORE, ParserState::DCS_IGNORE},
    {kUnicodePrintableLo, kUnicodeMaxCodePoint, Action::IGNORE, ParserState::DCS_IGNORE},
    {kDelCharacter, kDelCharacter, Action::IGNORE, ParserState::DCS_IGNORE},
    {0x00, 0x17, Action::IGNORE, ParserState::DCS_IGNORE},
    {0x19, 0x19, Action::IGNORE, ParserState::DCS_IGNORE},
    {0x1C, 0x1F, Action::IGNORE, ParserState::DCS_IGNORE},
};

constexpr Rule kSosPmStringRules[] = {
    {kAsciiPrintableLo, kAsciiPrintableHi, Action::IGNORE, ParserState::SOS_PM_STRING},
    {kUnicodePrintableLo, kUnicodeMaxCodePoint, Action::IGNORE, ParserState::SOS_PM_STRING},
    {kC1St, kC1St, Action::IGNORE, ParserState::GROUND},
    {kDelCharacter, kDelCharacter, Action::IGNORE, ParserState::SOS_PM_STRING},
    {0x00, 0x17, Action::IGNORE, ParserState::SOS_PM_STRING},
    {0x19, 0x19, Action::IGNORE, ParserState::SOS_PM_STRING},
    {0x1C, 0x1F, Action::IGNORE, ParserState::SOS_PM_STRING},
};

constexpr Rule kApcEntryRules[] = {
    {kEscCharacter, kEscCharacter, Action::IGNORE, ParserState::GROUND},
    {kC1St, kC1St, Action::IGNORE, ParserState::GROUND},
    {kCanCharacter, kCanCharacter, Action::IGNORE, ParserState::GROUND},
    {kSubCharacter, kSubCharacter, Action::IGNORE, ParserState::GROUND},
    {kAsciiPrintableLo, kAsciiPrintableHi, Action::IGNORE, ParserState::APC_ENTRY},
    {kUnicodePrintableLo, kUnicodeMaxCodePoint, Action::IGNORE, ParserState::APC_ENTRY},
    {kDelCharacter, kDelCharacter, Action::IGNORE, ParserState::APC_ENTRY},
    {0x00, 0x17, Action::IGNORE, ParserState::APC_ENTRY},
    {0x19, 0x19, Action::IGNORE, ParserState::APC_ENTRY},
    {0x1C, 0x1F, Action::IGNORE, ParserState::APC_ENTRY},
};

template <size_t N>
void applyRules(std::array<Transition, kSlotCount>& row, Transition& unicodeDefault,
                const Rule (&rules)[N])
{
    for (const Rule& rule : rules) {
        const int lo = std::max(rule.lo, 0);
        const int hi = std::min(rule.hi, kSlotCount - 1);
        for (int cp = lo; cp <= hi; ++cp) {
            row[cp] = {rule.action, rule.next};
        }
        if (rule.hi >= kSlotCount) {
            unicodeDefault = {rule.action, rule.next};
        }
    }
}

Lookup buildLookup()
{
    Lookup lookup;
    for (int state = 0; state < kStateCount; ++state) {
        lookup.table[state].fill(kDefaultTransition);
        lookup.unicodeDefaults[state] = kDefaultTransition;
        // Pass 1: globals set the baseline for all states.
        applyRules(lookup.table[state], lookup.unicodeDefaults[state], kGlobalRules);
    }
    // Pass 2: per-state rules override globals (per-state wins).
    applyRules(lookup.table[static_cast<int>(ParserState::GROUND)],
               lookup.unicodeDefaults[static_cast<int>(ParserState::GROUND)], kGroundRules);
    applyRules(lookup.table[static_cast<int>(ParserState::ESCAPE)],
               lookup.unicodeDefaults[static_cast<int>(ParserState::ESCAPE)], kEscapeRules);
    applyRules(lookup.table[static_cast<int>(ParserState::ESCAPE_INTERMEDIATE)],
               lookup.unicodeDefaults[static_cast<int>(ParserState::ESCAPE_INTERMEDIATE)],
               kEscapeIntermediateRules);
    applyRules(lookup.table[static_cast<int>(ParserState::CHARSET)],
               lookup.unicodeDefaults[static_cast<int>(ParserState::CHARSET)], kCharsetRules);
    applyRules(lookup.table[static_cast<int>(ParserState::CSI_ENTRY)],
               lookup.unicodeDefaults[static_cast<int>(ParserState::CSI_ENTRY)], kCsiEntryRules);
    applyRules(lookup.table[static_cast<int>(ParserState::CSI_PARAM)],
               lookup.unicodeDefaults[static_cast<int>(ParserState::CSI_PARAM)], kCsiParamRules);
    applyRules(lookup.table[static_cast<int>(ParserState::CSI_INTERMEDIATE)],
               lookup.unicodeDefaults[static_cast<int>(ParserState::CSI_INTERMEDIATE)],
               kCsiIntermediateRules);
    applyRules(lookup.table[static_cast<int>(ParserState::CSI_IGNORE)],
               lookup.unicodeDefaults[static_cast<int>(ParserState::CSI_IGNORE)], kCsiIgnoreRules);
    applyRules(lookup.table[static_cast<int>(ParserState::OSC_STRING)],
               lookup.unicodeDefaults[static_cast<int>(ParserState::OSC_STRING)], kOscStringRules);
    applyRules(lookup.table[static_cast<int>(ParserState::DCS_ENTRY)],
               lookup.unicodeDefaults[static_cast<int>(ParserState::DCS_ENTRY)], kDcsEntryRules);
    applyRules(lookup.table[static_cast<int>(ParserState::DCS_PARAM)],
               lookup.unicodeDefaults[static_cast<int>(ParserState::DCS_PARAM)], kDcsParamRules);
    applyRules(lookup.table[static_cast<int>(ParserState::DCS_INTERMEDIATE)],
               lookup.unicodeDefaults[static_cast<int>(ParserState::DCS_INTERMEDIATE)],
               kDcsIntermediateRules);
    applyRules(lookup.table[static_cast<int>(ParserState::DCS_IGNORE)],
               lookup.unicodeDefaults[static_cast<int>(ParserState::DCS_IGNORE)], kDcsIgnoreRules);
    applyRules(lookup.table[static_cast<int>(ParserState::SOS_PM_STRING)],
               lookup.unicodeDefaults[static_cast<int>(ParserState::SOS_PM_STRING)],
               kSosPmStringRules);
    applyRules(lookup.table[static_cast<int>(ParserState::APC_ENTRY)],
               lookup.unicodeDefaults[static_cast<int>(ParserState::APC_ENTRY)], kApcEntryRules);
    return lookup;
}

const Lookup& lookup()
{
    static const Lookup kLookup = buildLookup();
    return kLookup;
}

} // namespace

// ============================================================================
// The Parser
// ============================================================================

void Parser::feed(std::u32string_view text)
{
    const Lookup& lk = lookup();
    for (const char32_t cp : text) {
        // Fast path: GROUND + ASCII printable — no table lookup needed.
        if (m_state == ParserState::GROUND && cp >= 32 && cp <= 126) {
            m_printBuffer.push_back(cp);
            continue;
        }
        const Transition t = cp < kSlotCount ? lk.table[static_cast<int>(m_state)][cp]
                                             : lk.unicodeDefaults[static_cast<int>(m_state)];
        apply(t.action, cp);
        m_state = t.next;
    }
}

void Parser::feedBytes(std::string_view data)
{
    feed(m_decoder.decode(data));
}

void Parser::flush()
{
    flushPrint();
}

void Parser::reset()
{
    m_state = ParserState::GROUND;
    m_printBuffer.clear();
    m_params.reset();
    m_intermediates.clear();
    m_prefix.clear();
    m_oscBuffer.clear();
    m_decoder.reset();
}

void Parser::apply(Action action, char32_t cp)
{
    switch (action) {
    case Action::PRINT:
        m_printBuffer.push_back(cp);
        break;
    case Action::EXECUTE:
        flushPrint();
        m_dispatcher->execute(static_cast<int>(cp));
        break;
    case Action::PARAM:
        if (cp >= kCsiDigitLo && cp <= kCsiDigitHi) {
            m_params.addDigit(static_cast<int>(cp) - 48);
        } else if (cp == kCsiSubparameterSeparator) {
            m_params.addSubparam();
        } else if (cp == kCsiParameterSeparator) {
            m_params.addParam();
        }
        break;
    case Action::COLLECT:
        if (cp >= kCsiPrivatePrefixLo && cp <= kCsiPrivatePrefixHi) {
            m_prefix = std::string(1, static_cast<char>(cp));
        } else {
            m_intermediates += static_cast<char>(cp);
        }
        break;
    case Action::CSI_DISPATCH:
        m_dispatcher->csiDispatch(m_intermediates, m_prefix, m_params.build(),
                                  std::string(1, static_cast<char>(cp)));
        m_params.reset();
        m_intermediates.clear();
        m_prefix.clear();
        break;
    case Action::ESC_DISPATCH:
        m_dispatcher->escapeDispatch(m_intermediates, std::string(1, static_cast<char>(cp)));
        m_intermediates.clear();
        break;
    case Action::CHARSET_DISPATCH:
        m_dispatcher->designateCharset(m_intermediates, std::string(1, static_cast<char>(cp)));
        m_intermediates.clear();
        break;
    case Action::OSC_START:
        flushPrint();
        m_oscBuffer.clear();
        break;
    case Action::OSC_PUT:
        m_oscBuffer.push_back(cp);
        break;
    case Action::OSC_END:
        m_dispatcher->oscDispatch(m_oscBuffer);
        m_oscBuffer.clear();
        break;
    case Action::OSC_ABORT:
        m_oscBuffer.clear();
        break;
    case Action::CLEAR:
        flushPrint();
        m_params.reset();
        m_intermediates.clear();
        m_prefix.clear();
        break;
    case Action::IGNORE:
        flushPrint();
        break;
    }
}

void Parser::flushPrint()
{
    if (!m_printBuffer.empty()) {
        m_dispatcher->chars(m_printBuffer);
        m_printBuffer.clear();
    }
}

} // namespace qtermx