// Cell — one glyph plus its graphic rendition (screen.h).
class Cell
{
%TypeHeaderCode
#include "screen.h"
%End

public:
    Cell();

    std::u32string data;
    int fg;
    int bg;
    bool bold;
    bool underline;
    bool reverse;
    bool blink;
    bool dim;
    bool italic;
    bool hidden;
    bool strike;
    bool overline;

    static Cell blank();
    bool operator==(const Cell &other) const;
};