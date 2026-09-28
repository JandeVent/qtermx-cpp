// Row — one grid row: cells plus the wrapped marker (screen.h).
class Row
{
%TypeHeaderCode
#include "screen.h"
%End

public:
    Row();

    // TODO: if the installed SIP does not map STL containers natively,
    // add a %MappedType std::vector<Cell> (or accessor methods).
    std::vector<Cell> cells;
    bool wrapped;
};