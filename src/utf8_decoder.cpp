#include "utf8_decoder.h"

namespace qtermx {

namespace {

constexpr char32_t kReplacement = 0xFFFD;

// First-continuation range constraints per lead byte (CPython's UTF-8
// state machine): 0xE0 needs 0xA0–0xBF (no overlongs), 0xED needs
// 0x80–0x9F (no surrogates), 0xF0 needs 0x90–0xBF (no overlongs), 0xF4
// needs 0x80–0x8F (no > U+10FFFF). All other leads accept 0x80–0xBF.
bool continuationOk(uint8_t lead, uint8_t b)
{
    if (lead == 0xE0) {
        return b >= 0xA0;
    }
    if (lead == 0xED) {
        return b <= 0x9F;
    }
    if (lead == 0xF0) {
        return b >= 0x90;
    }
    if (lead == 0xF4) {
        return b <= 0x8F;
    }
    return true;
}

} // namespace

std::u32string Utf8Decoder::decode(std::string_view bytes)
{
    std::u32string out;
    size_t i = 0;
    while (i < bytes.size()) {
        const uint8_t b = static_cast<uint8_t>(bytes[i]);
        if (m_remaining > 0) {
            // The lead's range constraint applies only to the FIRST
            // continuation byte (m_remaining == 2 for 3-byte leads,
            // == 3 for 4-byte); later continuations just need 0x80–0xBF.
            if ((b & 0xC0) != 0x80 ||
                (m_remaining >= 2 && !continuationOk(m_lead, b))) {
                // Invalid continuation: emit U+FFFD for the pending
                // sequence and reprocess this byte fresh (CPython
                // resyncs at the offending byte).
                out.push_back(kReplacement);
                m_remaining = 0;
                continue;
            }
            m_codepoint = (m_codepoint << 6) | (b & 0x3F);
            --m_remaining;
            if (m_remaining == 0) {
                out.push_back(m_codepoint);
            }
            ++i;
            continue;
        }
        if (b < 0x80) {
            out.push_back(b);
            ++i;
        } else if (b >= 0xC2 && b <= 0xDF) {
            m_lead = b;
            m_codepoint = b & 0x1F;
            m_remaining = 1;
            ++i;
        } else if (b >= 0xE0 && b <= 0xEF) {
            m_lead = b;
            m_codepoint = b & 0x0F;
            m_remaining = 2;
            ++i;
        } else if (b >= 0xF0 && b <= 0xF4) {
            m_lead = b;
            m_codepoint = b & 0x07;
            m_remaining = 3;
            ++i;
        } else {
            // Lone continuation byte or invalid lead: replacement char.
            out.push_back(kReplacement);
            ++i;
        }
    }
    return out;
}

void Utf8Decoder::reset()
{
    m_remaining = 0;
    m_lead = 0;
    m_codepoint = 0;
}

std::string encodeUtf8(std::u32string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char32_t cp : text) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

} // namespace qtermx