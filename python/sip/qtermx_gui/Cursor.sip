// Cursor — the printable position plus rendition (screen.h).
class Cursor
{
%TypeHeaderCode
#include "screen.h"
%End

public:
    Cursor();

    int x;
    int y;
    bool pending_wrap;
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
};