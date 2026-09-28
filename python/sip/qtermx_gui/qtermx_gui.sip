// qtermx_gui — PyQt6 bindings for qtermx-cpp.
//
// A single extension module: the Qt GUI layer (TerminalWidget,
// TerminalRenderer, InputEncoder) plus the minimal core surface the
// widget API exposes — Session, Pty, Screen, and the frozen value types
// snapshots carry (Cell, Row, Cursor, Selection, Snapshot).
//
// The parser, emulator and params types are internal to Session and are
// deliberately not bound.

%Module(name=qtermx_gui, keyword_arguments="All")

%Import QtCore/QtCoremod.sip
%Import QtGui/QtGuimod.sip
%Import QtWidgets/QtWidgetsmod.sip

// Code injected into the generated module's header. The `using`
// directives make the unqualified names SIP emits resolve to their
// qtermx namespaces. The headers must come first — a using-directive
// needs the namespace to exist.
%ModuleHeaderCode
#include "input_encoder.h"
#include "renderer.h"
#include "screen.h"
#include "selection.h"
#include "terminal_widget.h"
using namespace qtermx;
using namespace qtermx::gui;
%End

// std::u32string <-> str - must be defined before the classes using it.
%Include conversions.sip

// Frozen value types (snapshot payloads).
%Include Cell.sip
%Include Row.sip
%Include Cursor.sip
%Include Selection.sip
%Include Snapshot.sip

// Model / session plumbing the widget API exposes.
%Include Pty.sip
%Include Screen.sip
%Include Session.sip

// Qt GUI layer.
%Include InputEncoder.sip
%Include Renderer.sip
%Include TerminalWidget.sip

// Free functions from screen.h / selection.h.
int rgb(int r, int g, int b);
bool isRgb(int color);
Selection point(int row, int col);
Selection extend(int anchorRow, int anchorCol, int row, int col, bool rectangular = false);