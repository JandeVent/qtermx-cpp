// Selection — normalized mouse selection coordinates (selection.h).
class Selection
{
%TypeHeaderCode
#include "selection.h"
%End

public:
    Selection();

    int row1;
    int col1;
    int row2;
    int col2;
    bool rectangular;

    bool operator==(const Selection &other) const;
};