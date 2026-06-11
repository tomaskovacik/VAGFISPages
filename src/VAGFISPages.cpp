#include "VAGFISPages.h"

static const uint8_t LINE_H = 8;
static inline uint8_t ly(uint8_t line) { return line * LINE_H; }

// ---- Construction ----

VAGFISPages::VAGFISPages(VAGFISWriter& writer, PCINTButtons& btns)
    : _w(writer), _btns(btns),
      _numPages(0), _curPage(0), _rawPage(1), _lastPage(0xFF),
      _state(FIS_PAGE), _menuCursor(0), _subCursor(0), _subItem(0),
      _redraw(false), _keepaliveMs(0)
{
    _navCooldownMs[BTN_UP] = _navCooldownMs[BTN_DN] = _navCooldownMs[BTN_RST] = 0;
}

// ---- Public API ----

void VAGFISPages::begin() {
    _btns.begin();
#ifdef VAGFIS_DEBUG
    Serial.println(F("DBG VAGFISPages::begin() ok"));
#endif
}

bool VAGFISPages::addPage(const FISPage& page) {
    if (_numPages >= VAGFIS_MAX_PAGES) return false;
    _pages[_numPages++] = page;
    return true;
}

void VAGFISPages::update() {
    // poll() detects long presses while held; clicks are fully ISR-latched.
    _btns.poll();

    if (_numPages == 0) {
#ifdef VAGFIS_DEBUG
        static bool once = false;
        if (!once) { once = true; Serial.println(F("DBG update: no pages registered")); }
#endif
        return;
    }

    bool wasCustom = isCustom();
    switch (_state) {
        case FIS_PAGE:      _pageButtons();      break;
        case FIS_CONFIG:    _configButtons();    break;
        case FIS_SUBCONFIG: _subConfigButtons(); break;
    }

    if (wasCustom && !isCustom()) {
        _w.reset();
#ifdef VAGFIS_DEBUG
        Serial.println(F("DBG released display to cluster"));
#endif
    }

    if (!isCustom()) {
#ifdef VAGFIS_DEBUG
        static uint32_t _dbgLastNotCustom = 0;
        uint32_t _dbgNow = millis();
        if (_dbgNow - _dbgLastNotCustom >= 2000) {
            _dbgLastNotCustom = _dbgNow;
            Serial.print(F("DBG not custom  rawPage=")); Serial.println(_rawPage);
        }
#endif
        return;
    }

    uint32_t now = millis();
    bool keepaliveDue = (now - _keepaliveMs) >= KEEPALIVE_INTERVAL;

    if (_state == FIS_PAGE) {
        if (_curPage != _lastPage) {
            _lastPage = _curPage;
            _redraw   = true;
#ifdef VAGFIS_DEBUG
            Serial.print(F("DBG enter page  curPage=")); Serial.print(_curPage);
            Serial.print(F("  rawPage=")); Serial.println(_rawPage);
#endif
            _clearScreen();
            if (_pages[_curPage].onEnter) _pages[_curPage].onEnter();
        } else if (_redraw) {
            _redraw = false;
#ifdef VAGFIS_DEBUG
            Serial.print(F("DBG render page curPage=")); Serial.println(_curPage);
#endif
            if (_pages[_curPage].onRender) _pages[_curPage].onRender();
        } else if (keepaliveDue) {
            _keepaliveMs = now;
#ifdef VAGFIS_DEBUG
            Serial.print(F("DBG keepalive   rawPage=")); Serial.println(_rawPage);
#endif
            _w.sendKeepAliveMsg();
        }
    } else {
        if (_redraw) {
            _redraw = false;
            if (_state == FIS_CONFIG) _drawConfig(); else _drawSub();
        } else if (keepaliveDue) {
            _keepaliveMs = now;
            _w.sendKeepAliveMsg();
        }
    }
}

// ---- PAGE state ----

void VAGFISPages::_pageButtons() {
    // Read and clear all latches up-front so no event bleeds into a later state.
    uint32_t now     = millis();
    bool clickUp     = _btns.wasClicked(BTN_UP);
    bool clickDn     = _btns.wasClicked(BTN_DN);
    bool longUp      = _btns.wasLongPressed(BTN_UP);
    bool longDn      = _btns.wasLongPressed(BTN_DN);
    bool longRst     = _btns.wasLongPressed(BTN_RST);
    (void)_btns.wasClicked(BTN_RST); // consume, not used in page state
    (void)longDn;                    // consume, nothing to drill out from in page state

    if (clickUp && (now - _navCooldownMs[BTN_UP]) >= NAV_COOLDOWN) {
        _navCooldownMs[BTN_UP] = now;
#ifdef VAGFIS_DEBUG
        Serial.print(F("DBG page UP click  dur=")); Serial.print(_btns.lastDurMs(BTN_UP));
        Serial.print(F("ms rawPage=")); Serial.println(_rawPage);
#endif
        uint8_t maxPage = VAGFIS_CLUSTER_PAGES + _numPages;
        _rawPage = (_rawPage < maxPage) ? _rawPage + 1 : 1;
        _syncCurPage();
    }
    if (clickDn && (now - _navCooldownMs[BTN_DN]) >= NAV_COOLDOWN) {
        _navCooldownMs[BTN_DN] = now;
#ifdef VAGFIS_DEBUG
        Serial.print(F("DBG page DN click  dur=")); Serial.print(_btns.lastDurMs(BTN_DN));
        Serial.print(F("ms rawPage=")); Serial.println(_rawPage);
#endif
        uint8_t maxPage = VAGFIS_CLUSTER_PAGES + _numPages;
        _rawPage = (_rawPage > 1) ? _rawPage - 1 : maxPage;
        _syncCurPage();
    }
    if (longUp && isCustom()) {
#ifdef VAGFIS_DEBUG
        Serial.print(F("DBG page UP long   dur=")); Serial.print(_btns.lastDurMs(BTN_UP));
        Serial.println(F("ms -> enter config"));
#endif
        _enterConfig();
    }
    if (longRst && isCustom()) {
#ifdef VAGFIS_DEBUG
        Serial.print(F("DBG page RST long  dur=")); Serial.print(_btns.lastDurMs(BTN_RST));
        Serial.println(F("ms -> onReset"));
#endif
        _btns.flush();
        if (_pages[_curPage].onReset) _pages[_curPage].onReset();
        _lastPage = 0xFF;
    }
}

void VAGFISPages::_syncCurPage() {
    if (_rawPage > VAGFIS_CLUSTER_PAGES) {
        _curPage = _rawPage - VAGFIS_CLUSTER_PAGES - 1;
    } else {
        _lastPage = 0xFF; // returned to cluster pages
    }
#ifdef VAGFIS_DEBUG
    Serial.print(F("DBG syncCurPage rawPage=")); Serial.print(_rawPage);
    Serial.print(F("  curPage=")); Serial.println(_curPage);
#endif
}

// ---- CONFIG state ----

void VAGFISPages::_enterConfig() {
    _btns.flush();
    _state      = FIS_CONFIG;
    _menuCursor = 0;
    _redraw     = true;
}

void VAGFISPages::_exitConfig() {
    _btns.flush();
    _state    = FIS_PAGE;
    _lastPage = 0xFF;
}

void VAGFISPages::_configButtons() {
    FISPage& page  = _pages[_curPage];
    uint8_t  total = page.numConfigItems + 1; // +1 for EXIT row
    uint32_t now   = millis();

    bool clickUp  = _btns.wasClicked(BTN_UP);
    bool clickDn  = _btns.wasClicked(BTN_DN);
    bool longUp   = _btns.wasLongPressed(BTN_UP);
    bool longDn   = _btns.wasLongPressed(BTN_DN);
    bool longRst  = _btns.wasLongPressed(BTN_RST);
    (void)_btns.wasClicked(BTN_RST);

    if (clickUp && (now - _navCooldownMs[BTN_UP]) >= NAV_COOLDOWN) {
        _navCooldownMs[BTN_UP] = now;
#ifdef VAGFIS_DEBUG
        Serial.print(F("DBG cfg  UP click  dur=")); Serial.print(_btns.lastDurMs(BTN_UP));
        Serial.print(F("ms cursor=")); Serial.println(_menuCursor);
#endif
        _menuCursor = (_menuCursor == 0) ? total - 1 : _menuCursor - 1;
        _redraw = true;
    }
    if (clickDn && (now - _navCooldownMs[BTN_DN]) >= NAV_COOLDOWN) {
        _navCooldownMs[BTN_DN] = now;
#ifdef VAGFIS_DEBUG
        Serial.print(F("DBG cfg  DN click  dur=")); Serial.print(_btns.lastDurMs(BTN_DN));
        Serial.print(F("ms cursor=")); Serial.println(_menuCursor);
#endif
        _menuCursor = (_menuCursor + 1) % total;
        _redraw = true;
    }
    if (longUp) {
        if (_menuCursor == page.numConfigItems) {
#ifdef VAGFIS_DEBUG
            Serial.print(F("DBG cfg  UP long   dur=")); Serial.print(_btns.lastDurMs(BTN_UP));
            Serial.println(F("ms -> exit (EXIT row)"));
#endif
            _exitConfig();
        } else {
#ifdef VAGFIS_DEBUG
            Serial.print(F("DBG cfg  UP long   dur=")); Serial.print(_btns.lastDurMs(BTN_UP));
            Serial.print(F("ms -> enter item ")); Serial.println(_menuCursor);
#endif
            _enterSub(_menuCursor);
        }
    }
    if (longDn || longRst) {
#ifdef VAGFIS_DEBUG
        Serial.print(F("DBG cfg  long      dur="));
        Serial.print(longDn ? _btns.lastDurMs(BTN_DN) : _btns.lastDurMs(BTN_RST));
        Serial.println(F("ms -> exit config"));
#endif
        _exitConfig();
    }
}

void VAGFISPages::_drawConfig() {
    FISPage& page = _pages[_curPage];
    _clearScreen();
    _w.sendStringFS(0, ly(1), 1, String(F("CFG: ")) + page.name); _fisWait();

    for (uint8_t i = 0; i < page.numConfigItems; i++) {
        String row = (_menuCursor == i) ? F(">") : F(" ");
        row += page.configItems[i].label;
        row += F(": ");
        row += page.configItems[i].options[page.configItems[i].selectedIndex];
        _w.sendStringFS(0, ly(i + 2), 1, row); _fisWait();
    }

    uint8_t exitLine = page.numConfigItems + 2;
    _w.sendStringFS(0, ly(exitLine), 1,
        (_menuCursor == page.numConfigItems) ? F(">EXIT") : F(" EXIT"));
    _fisWait();
}

// ---- SUBCONFIG state ----

void VAGFISPages::_enterSub(uint8_t itemIndex) {
    // An item without options has nothing to select; entering it would also
    // make the cursor arithmetic divide by zero.
    if (_pages[_curPage].configItems[itemIndex].numOptions == 0) return;
    _btns.flush();
    _subItem   = itemIndex;
    _subCursor = _pages[_curPage].configItems[itemIndex].selectedIndex;
    _state     = FIS_SUBCONFIG;
    _redraw    = true;
}

void VAGFISPages::_confirmSub() {
    _btns.flush();
    FISConfigItem& item = _pages[_curPage].configItems[_subItem];
    item.selectedIndex = _subCursor;
    if (item.onChange) item.onChange(_subCursor);
    _state  = FIS_CONFIG;
    _redraw = true;
}

void VAGFISPages::_cancelSub() {
    _btns.flush();
    _state  = FIS_CONFIG;
    _redraw = true;
}

void VAGFISPages::_subConfigButtons() {
    FISConfigItem& item = _pages[_curPage].configItems[_subItem];
    uint32_t       now  = millis();

    bool clickUp  = _btns.wasClicked(BTN_UP);
    bool clickDn  = _btns.wasClicked(BTN_DN);
    bool longUp   = _btns.wasLongPressed(BTN_UP);
    bool longDn   = _btns.wasLongPressed(BTN_DN);
    bool longRst  = _btns.wasLongPressed(BTN_RST);
    (void)_btns.wasClicked(BTN_RST);

    if (clickUp && (now - _navCooldownMs[BTN_UP]) >= NAV_COOLDOWN) {
        _navCooldownMs[BTN_UP] = now;
#ifdef VAGFIS_DEBUG
        Serial.print(F("DBG sub  UP click  dur=")); Serial.print(_btns.lastDurMs(BTN_UP));
        Serial.print(F("ms cursor=")); Serial.println(_subCursor);
#endif
        _subCursor = (_subCursor == 0) ? item.numOptions - 1 : _subCursor - 1;
        _redraw = true;
    }
    if (clickDn && (now - _navCooldownMs[BTN_DN]) >= NAV_COOLDOWN) {
        _navCooldownMs[BTN_DN] = now;
#ifdef VAGFIS_DEBUG
        Serial.print(F("DBG sub  DN click  dur=")); Serial.print(_btns.lastDurMs(BTN_DN));
        Serial.print(F("ms cursor=")); Serial.println(_subCursor);
#endif
        _subCursor = (_subCursor + 1) % item.numOptions;
        _redraw = true;
    }
    if (longUp) {
#ifdef VAGFIS_DEBUG
        Serial.print(F("DBG sub  UP long   dur=")); Serial.print(_btns.lastDurMs(BTN_UP));
        Serial.print(F("ms -> confirm idx=")); Serial.println(_subCursor);
#endif
        _confirmSub();
    }
    if (longDn || longRst) {
#ifdef VAGFIS_DEBUG
        Serial.print(F("DBG sub  long      dur="));
        Serial.print(longDn ? _btns.lastDurMs(BTN_DN) : _btns.lastDurMs(BTN_RST));
        Serial.println(F("ms -> cancel"));
#endif
        _cancelSub();
    }
}

// ---- Helpers ----

void VAGFISPages::_drawSub() {
    FISConfigItem& item = _pages[_curPage].configItems[_subItem];
    _clearScreen();
    _w.sendStringFS(0, ly(1), 1, item.label); _fisWait();

    for (uint8_t i = 0; i < item.numOptions; i++) {
        String row = (_subCursor == i) ? F(">") : F(" ");
        row += item.options[i];
        _w.sendStringFS(0, ly(i + 2), 1, row); _fisWait();
    }
}

void VAGFISPages::_clearScreen() {
    _w.initFullScreen(); _fisWait();
}
