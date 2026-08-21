#ifndef QTERMX_UTF8_DECODER_H
#define QTERMX_UTF8_DECODER_H

// Incremental UTF-8 decoder, upstream of the parser (ADR-0001 port).
// Qt-free: the core must not depend on Qt. Semantics match CPython's
// codecs.getincrementaldecoder("utf-8")(errors="replace"):
//   - incomplete sequences at the end of input are held (no output);
//   - invalid sequences emit U+FFFD and resync at the offending byte;
//   - continuation-byte range checks happen as bytes arrive (overlongs,
//     surrogates and > U+10FFFF are rejected the moment a continuation
//     byte violates the lead's constraints — matching CPython's
//     per-byte error detection, not a deferred completion check).

#include <cstdint>
#include <string>

namespace qtermx {

class Utf8Decoder {
public:
    // Decode `bytes` incrementally; returns the code points produced.
    std::u32string decode(std::string_view bytes);

    void reset();

private:
    // Number of continuation bytes still expected for the pending lead.
    int m_remaining = 0;
    // The pending lead byte (for first-continuation range checks).
    uint8_t m_lead = 0;
    // The code point being accumulated.
    char32_t m_codepoint = 0;
};

// Encode code points as UTF-8 (used by Screen::render and the fixture
// runner to produce byte-exact text).
std::string encodeUtf8(std::u32string_view text);

} // namespace qtermx

#endif // QTERMX_UTF8_DECODER_H