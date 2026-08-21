#ifndef QTERMX_EMULATOR_H
#define QTERMX_EMULATOR_H

// The emulator — turns parse events into screen operations (port of
// pyqtermx emulator.py). Implements the dispatcher protocol, the seam
// the parser already defines. The screen is the dumb model; the
// emulator decides what each event means.

#include <string>

#include "dispatcher.h"
#include "params.h"

namespace qtermx {

class Screen;

class Emulator : public Dispatcher {
public:
    explicit Emulator(Screen& screen) : m_screen(screen) {}

    void chars(std::u32string text) override;
    void execute(int code) override;
    void csiDispatch(std::string intermediates, std::string prefix, Params params,
                     std::string final) override;
    void escapeDispatch(std::string intermediates, std::string final) override;
    void designateCharset(std::string designator, std::string charset) override;
    void oscDispatch(std::u32string payload) override;

private:
    // CSI handlers (params).
    void sm(const Params& params);
    void rm(const Params& params);
    void decset(const Params& params);
    void decrst(const Params& params);
    void sgr(const Params& params);
    void decstbm(const Params& params);
    void cuu(const Params& params);
    void cud(const Params& params);
    void cuf(const Params& params);
    void cub(const Params& params);
    void cnl(const Params& params);
    void cpl(const Params& params);
    void cup(const Params& params);
    void cha(const Params& params);
    void vpa(const Params& params);
    void ed(const Params& params);
    void el(const Params& params);
    void ech(const Params& params);
    void ich(const Params& params);
    void il(const Params& params);
    void dl(const Params& params);
    void dch(const Params& params);
    void su(const Params& params);
    void sd(const Params& params);
    void tbc(const Params& params);
    void cht(const Params& params);
    void cbt(const Params& params);
    void save(const Params& params);
    void restore(const Params& params);

    // ESC handlers (no params).
    void ind();
    void nel();
    void ri();
    void ls2();
    void ls3();
    void ls1r();
    void ls2r();
    void ls3r();
    void hts();
    void decsc();
    void decrc();
    void decaln();

    // SGR extended colors: returns the number of *additional* params
    // consumed (0 when nothing matched).
    int sgrExtended(const Params& params, int i, void (Screen::*setColor)(int));

    Screen& m_screen;
};

} // namespace qtermx

#endif // QTERMX_EMULATOR_H