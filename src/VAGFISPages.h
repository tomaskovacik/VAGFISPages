#pragma once
#include <Arduino.h>
#include <PCINTbuttons.h>
#include <VAGFISWriter.h>

// Uncomment to enable serial button-event tracing
// #define VAGFIS_DEBUG

// ---- Compile-time limits (override BEFORE including this header) ----
#ifndef VAGFIS_CLUSTER_PAGES
#define VAGFIS_CLUSTER_PAGES  4   // passthrough pages before custom pages start
#endif
#ifndef VAGFIS_MAX_PAGES
#define VAGFIS_MAX_PAGES      8   // max custom pages
#endif
#ifndef VAGFIS_MAX_CONFIG_ITEMS
#define VAGFIS_MAX_CONFIG_ITEMS 8 // max config items per page
#endif
#ifndef VAGFIS_MAX_OPTIONS
#define VAGFIS_MAX_OPTIONS    8   // max selectable options per config item
#endif

// ---- Config item: a labelled setting with a selectable list of values ----
struct FISConfigItem {
    const char*  label;
    const char*  options[VAGFIS_MAX_OPTIONS];
    uint8_t      numOptions;
    uint8_t      selectedIndex;
    void (*onChange)(uint8_t newIndex);
};

// ---- Page descriptor ----
struct FISPage {
    const char*    name;
    void (*onEnter)();
    void (*onRender)();
    void (*onReset)();
    FISConfigItem* configItems;
    uint8_t        numConfigItems;
};

// ---- Navigation state machine states ----
enum FISNavState : uint8_t {
    FIS_PAGE      = 0,
    FIS_CONFIG    = 1,
    FIS_SUBCONFIG = 2,
};

// ---- Navigator ----
// Call begin() once in setup() (after addButton() calls on the PCINTButtons object),
// then update() once per loop().
//
// Button contract:
//   UP click / DN click  — navigate up / down (pages, menu items, option values)
//   UP long              — drill in  (open config | enter item | confirm value)
//   DN long              — drill out (subconfig → config → page)
//   RST long             — onReset callback (page state) or drill out (config states)
class VAGFISPages {
public:
    // Button id constants — pass as the id argument to PCINTButtons::addButton().
    static const uint8_t BTN_UP  = 0;
    static const uint8_t BTN_DN  = 1;
    static const uint8_t BTN_RST = 2;

    // btns must already have addButton() called for BTN_UP, BTN_DN, BTN_RST
    // before nav.begin() is called.
    VAGFISPages(VAGFISWriter& writer, PCINTButtons& btns);

    void begin();   // activates PCINTbuttons and prepares state
    void update();  // call once per loop()

    void markDirty()    { _redraw = true; }
    void forceReenter() { _lastPage = 0xFF; }

    uint8_t     rawPage()   const { return _rawPage; }
    uint8_t     pageIndex() const { return _curPage; }
    bool        isCustom()  const { return _rawPage > VAGFIS_CLUSTER_PAGES; }
    FISNavState navState()  const { return _state; }

    bool addPage(const FISPage& page);

private:
    VAGFISWriter&  _w;
    PCINTButtons&  _btns;

    FISPage  _pages[VAGFIS_MAX_PAGES];
    uint8_t  _numPages;
    uint8_t  _curPage;
    uint8_t  _rawPage;
    uint8_t  _lastPage;

    FISNavState _state;
    uint8_t  _menuCursor;
    uint8_t  _subCursor;
    uint8_t  _subItem;
    bool     _redraw;
    uint32_t _keepaliveMs;

    void _pageButtons();
    void _configButtons();
    void _subConfigButtons();

    void _enterConfig();
    void _exitConfig();
    void _enterSub(uint8_t itemIndex);
    void _confirmSub();
    void _cancelSub();

    void _drawConfig();
    void _drawSub();
    void _clearScreen();
    void _syncCurPage();

    static const uint8_t  DELAY             = 5;
    static const uint16_t KEEPALIVE_INTERVAL = 900;
    // Suppresses extra pulses that the VAG stalk generates per physical press.
    static const uint16_t NAV_COOLDOWN       = 300;
    uint32_t _navCooldownMs[3];

    static void _fisWait() {
        uint32_t t = millis();
        while (millis() - t < DELAY) {}
    }
};
