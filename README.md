# VAGFISPages

Custom pages and config menus for **VAG (VW/Audi) FIS instrument-cluster displays**, navigated with the standard steering-column stalk buttons.

The cluster keeps its own pages (trip computer, navigation, ...). This library adds your pages *after* them: scroll past the last cluster page and you are in your own UI — ECU live data, multimedia info, TPMS, whatever you render. Scroll past your last page and the display is handed back to the cluster.

Built on top of:

- [VAGFISWriter](https://github.com/tomaskovacik/VAGFISWriter) — the 3-line bus (3LB) display protocol
- [PCINTbuttons](https://github.com/tomaskovacik/PCINTbuttons) — ISR-latched stalk buttons that never miss a click, even while the display protocol blocks the loop for hundreds of milliseconds

## Navigation contract

Three buttons: **UP**, **DOWN**, **RESET** (the standard stalk set).

| Input | Page view | Config menu | Option submenu |
|---|---|---|---|
| UP / DN click | previous / next page | move cursor | move cursor |
| UP long | open config menu¹ | enter item (or EXIT) | **confirm** selection |
| DN long | — | exit menu | **cancel** |
| RST long | page's `onReset` callback¹ | exit menu | cancel |

¹ only on custom pages; on cluster pages the stalk works as the cluster intended.

Every page can expose a config menu: a list of labelled items, each with a fixed set of options (e.g. `REFRESH: 50ms / 100ms / 250ms / 500ms`). Selecting an option fires the item's `onChange` callback.

## Quick start

```cpp
#include <VAGFISWriter.h>
#include <PCINTbuttons.h>
#include <VAGFISPages.h>

VAGFISWriter fis(SCK, MOSI, 49);                     // CLK, DATA, ENA
PCINTButtons stalk(PCINTButtons::VECTOR_2, 5, 2000); // debounce 5ms, long press 2s
VAGFISPages  nav(fis, stalk);

void tpmsRender() {
    fis.sendStringFS(0,  8, 1, "TPMS");
    fis.sendStringFS(0, 24, 1, "FL: 230 kPa");
    // ...
}

static FISPage pages[] = {
    //  name    onEnter  onRender    onReset  configItems  numConfigItems
    { "TPMS",  nullptr, tpmsRender, nullptr, nullptr,     0 },
};

void setup() {
    stalk.addButton(A8,  VAGFISPages::BTN_UP);
    stalk.addButton(A9,  VAGFISPages::BTN_RST);
    stalk.addButton(A10, VAGFISPages::BTN_DN);

    fis.begin();
    nav.begin();                  // also starts the buttons
    nav.addPage(pages[0]);
}

void loop() {
    nav.update();                 // call every loop, nothing else required

    if (!nav.isCustom()) return;  // cluster owns the display right now

    // Poll your data sources here; call nav.markDirty() when displayed
    // values changed — that triggers one onRender without flicker.
}
```

A complete sketch with config items, `onChange` callbacks, and multiple pages is in [examples/BasicUsage](examples/BasicUsage/BasicUsage.ino).

## Defining pages

```cpp
struct FISPage {
    const char*    name;            // shown in the config menu header
    void (*onEnter)();              // page became visible (screen already cleared)
    void (*onRender)();             // draw/refresh content; also fired by markDirty()
    void (*onReset)();              // RST long press on this page
    FISConfigItem* configItems;     // nullptr if the page has no settings
    uint8_t        numConfigItems;
};

struct FISConfigItem {
    const char*  label;                       // "REFRESH"
    const char*  options[VAGFIS_MAX_OPTIONS]; // {"50ms", "100ms", ...}
    uint8_t      numOptions;
    uint8_t      selectedIndex;               // current selection, updated on confirm
    void (*onChange)(uint8_t newIndex);       // fired when the user confirms
};
```

Rendering is plain `VAGFISWriter` calls inside your callbacks — the library does not impose a layout on page content. Menu screens (config and submenu) are drawn by the library.

## How the display claim works

The cluster owns the display by default. While you are on a custom page, the library sends a keep-alive message every 900 ms so the cluster does not reclaim the screen (it would after ~3–4 s of silence). Rendering counts as activity too. The moment you navigate back into the cluster's page range, the library sends a release and the cluster takes over instantly.

`update()` is also where stalk events are processed. Because the buttons are interrupt-latched, the blocking nature of the FIS protocol (a keep-alive blocks ~200 ms, a full page render ~30 ms) cannot cause missed presses.

## Compile-time configuration

Override before including `VAGFISPages.h`:

| Macro | Default | Meaning |
|---|---|---|
| `VAGFIS_CLUSTER_PAGES` | 5 | how many native cluster pages precede your pages |
| `VAGFIS_MAX_PAGES` | 8 | max custom pages |
| `VAGFIS_MAX_CONFIG_ITEMS` | 8 | max config items per page |
| `VAGFIS_MAX_OPTIONS` | 8 | max options per config item |

Uncomment `#define VAGFIS_DEBUG` in `VAGFISPages.h` to get serial traces of every button event (with measured press durations), page changes, and keep-alives — invaluable when tuning the long-press threshold for your stalk.

## API summary

| Method | Description |
|---|---|
| `VAGFISPages(writer, buttons)` | References to a `VAGFISWriter` and a `PCINTButtons` instance. |
| `begin()` | Starts the button driver. Call after `addButton()` registrations. |
| `update()` | Process buttons, render, keep-alive. Call once per `loop()`. |
| `addPage(page)` | Register a page. Returns `false` when full. |
| `markDirty()` | Request one `onRender` on the next `update()` — flicker-free refresh. |
| `rawPage()` | 1-based absolute page (cluster pages included). |
| `pageIndex()` | 0-based index into your registered pages. |
| `isCustom()` | `true` while one of your pages is active. |
| `navState()` | `FIS_PAGE`, `FIS_CONFIG` or `FIS_SUBCONFIG`. |

## Hardware notes

- Tested on Arduino Mega 2560; stalk buttons on Port K (A8/A9/A10) via pin-change interrupts — no conflicts with Serial, I²C or SPI.
- Stalk buttons are active-low; internal pull-ups are enabled by the button library.
- VAG stalks generate multiple short pulses per physical press; a 300 ms per-button navigation cooldown inside the library absorbs them.
