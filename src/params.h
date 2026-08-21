#ifndef QTERMX_PARAMS_H
#define QTERMX_PARAMS_H

// Typed CSI parameters, shaped like xterm.js's Params (port of pyqtermx
// params.py). Parsing only, never interpretation: values are stored as
// integers (empty parameters and sub-parameters are recorded as 0 and -1
// respectively, per xterm.js's zero-default-mode convention); which value
// a handler treats as "default" is a dispatch-time decision.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace qtermx {

// Digit accumulation caps out here (xterm.js Constants.MAX_VALUE).
inline constexpr uint32_t kMaxValue = 0xFFFFFFFF;
// Maximum storable parameters and sub-parameters (xterm.js defaults).
inline constexpr int kMaxParams = 32;
inline constexpr int kMaxSubparams = 32;

// Immutable CSI parameters: one group per `;`-separated parameter.
// Each group is (main_value, sub1, sub2, ...) — sub-parameters follow
// the main value; an empty sub-parameter is stored as -1. Parsing only:
// no defaults are applied here.
//
// Values are int64_t: the digit-accumulation cap is 0xFFFFFFFF (xterm.js
// Constants.MAX_VALUE), which does not fit a signed 32-bit int — the
// Python oracle stores the full 32-bit value, so the C++ port must too.
struct Params {
    std::vector<std::vector<int64_t>> groups;

    // Non-explicit so tests can write Params{{31}} / Params{{1}, {2}}.
    Params() = default;
    Params(std::initializer_list<std::initializer_list<int64_t>> g)
    {
        groups.reserve(g.size());
        for (const auto& group : g) {
            groups.emplace_back(group);
        }
    }

    int count() const { return static_cast<int>(groups.size()); }

    // Main value at `index`, or 0 when absent (zero-default-mode).
    int64_t get(int index) const
    {
        if (index >= 0 && index < static_cast<int>(groups.size())) {
            return groups[index][0];
        }
        return 0;
    }

    // Sub-parameters of the parameter at `index`, or empty when none.
    std::vector<int64_t> subparams(int index) const
    {
        if (index >= 0 && index < static_cast<int>(groups.size())) {
            return std::vector<int64_t>(groups[index].begin() + 1, groups[index].end());
        }
        return {};
    }

    bool operator==(const Params& other) const { return groups == other.groups; }
};

// Mutable accumulation target for the parser; build() freezes it.
class ParamsBuilder {
public:
    ParamsBuilder() { reset(); }

    void addDigit(int digit)
    {
        if (m_reject) {
            return;
        }
        auto& group = m_groups.back();
        // Accumulate in int64 and clamp at the 0xFFFFFFFF cap (xterm.js
        // Constants.MAX_VALUE) — the full 32-bit value, like the Python
        // oracle (arbitrary-precision ints).
        const int64_t v = group.back() * 10 + digit;
        const int64_t clamped = std::min<int64_t>(v, kMaxValue);
        if (m_digitIsSub) {
            int64_t& current = group.back();
            current = current == -1 ? digit : clamped;
        } else {
            group[0] = clamped;
        }
    }

    // `;`: start a new parameter group.
    void addParam()
    {
        m_digitIsSub = false;
        if (static_cast<int>(m_groups.size()) >= kMaxParams) {
            m_reject = true;
            return;
        }
        m_groups.push_back({0});
    }

    // `:`: start a new sub-parameter on the current group.
    void addSubparam()
    {
        m_digitIsSub = true;
        if (m_reject || m_subCount >= kMaxSubparams) {
            m_reject = true;
            return;
        }
        ++m_subCount;
        m_groups.back().push_back(-1);
    }

    Params build() const
    {
        Params p;
        p.groups = m_groups;
        return p;
    }

    void reset()
    {
        m_groups = {{0}}; // zero-default-mode (xterm.js)
        m_digitIsSub = false;
        m_reject = false;
        m_subCount = 0;
    }

private:
    std::vector<std::vector<int64_t>> m_groups;
    bool m_digitIsSub = false;
    bool m_reject = false;
    int m_subCount = 0;
};

} // namespace qtermx

#endif // QTERMX_PARAMS_H