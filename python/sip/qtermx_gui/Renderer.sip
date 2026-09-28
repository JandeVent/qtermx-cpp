// TerminalRenderer — snapshot -> pixels (renderer.h, Qt layer).

class TerminalRenderer
{
%TypeHeaderCode
#include "renderer.h"
%End

public:
    TerminalRenderer(const QFont &font, bool antialias = true);

    // -- Metrics ---------------------------------------------------------
    double cellW() const;
    double cellH() const;
    QRectF cellRect(int viewportRow, int col) const;

    // -- Theming ---------------------------------------------------------
    void setFont(const QFont &font);
    void setPalette(const QColor &fg, const QColor &bg);
    const QFont &font() const;
    const QColor &defaultFgColor() const;
    const QColor &defaultBgColor() const;

    // -- Painting --------------------------------------------------------
    int staticCacheSize() const;

    // TODO: `cursorVisible` (std::optional<bool>) needs SIP >= 6.7
    // (native std::optional support); wrap with %MethodCode otherwise.
    void render(QImage &image, const Snapshot &snapshot,
                const std::vector<Row> *rows = nullptr,
                const std::vector<int> *rowIndices = nullptr,
                const Selection *selection = nullptr,
                std::optional<bool> cursorVisible,
                const char *cursorStyle = "block");

    void paint(QPainter &painter, const Snapshot &snapshot, int viewportLines,
               const std::vector<Row> *rows = nullptr,
               const std::vector<int> *rowIndices = nullptr,
               const Selection *selection = nullptr,
               std::optional<bool> cursorVisible,
               const char *cursorStyle = "block");
};