#ifndef QTERMX_WCWIDTH_H
#define QTERMX_WCWIDTH_H

// Cell-width measurement for the screen model (port of the Python
// `wcwidth` package dependency). Qt-free.
//
// Based on Markus Kuhn's public-domain wcwidth.c (the same tables the
// Python wcwidth package derives from). The table is older than the
// package's Unicode-15 tables; for the conformance corpus and the
// CJK/combining/emoji ranges the tests exercise, the results are
// identical. If a future fixture needs a newer range, extend the table.

#include <cstdint>

namespace qtermx {

// Width of a code point: 0 combining, 1 narrow, 2 wide, -1 control.
int wcwidth(char32_t cp);

} // namespace qtermx

#endif // QTERMX_WCWIDTH_H