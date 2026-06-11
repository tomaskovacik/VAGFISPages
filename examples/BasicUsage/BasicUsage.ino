  // BasicUsage — VAGFISPages integration example
// Demonstrates: page registration, config items, onReset, and cluster passthrough.
//
// Hardware (Arduino Mega):
//   FIS_CLK  = pin 52 (SCK)
//   FIS_DATA = pin 51 (MOSI)
//   FIS_ENA  = pin 49
//   BTN_UP   = A8  (stalk UP,    Port K / PCINT2, active LOW)
//   BTN_RST  = A9  (stalk RESET, Port K / PCINT2, active LOW)
//   BTN_DN   = A10 (stalk DOWN,  Port K / PCINT2, active LOW)

#include <VAGFISWriter.h>
#include <PCINTbuttons.h>
#include <VAGFISPages.h>

// ---- Hardware ----
#define FIS_CLK   SCK
#define FIS_DATA  MOSI
#define FIS_ENA   49

VAGFISWriter fis(FIS_CLK, FIS_DATA, FIS_ENA);

PCINTButtons stalk(PCINTButtons::VECTOR_2, 5, 2000);  // debounce=5ms, long press=2s

VAGFISPages nav(fis, stalk);

// ---- Page 1: ECU engine ----

static uint8_t ecuRefreshIdx = 1; // default: 100 ms

void ecuEnter() {
    fis.sendStringFS(0, 8,  1, "ECU ENGINE");
    fis.sendStringFS(0, 16, 1, "CONNECTING...");
}

void ecuRender() {
    fis.sendStringFS(0,  8, 1, "ECU ENGINE");
    fis.sendStringFS(0, 24, 1, "RPM:  ----");
    fis.sendStringFS(0, 32, 1, "TEMP: ----");
    fis.sendStringFS(0, 40, 1, "LOAD: ----%");
}

void ecuReset() {
    fis.initFullScreen();
    fis.sendStringFS(0, 24, 1, "RECONNECTING...");
}

void onEcuRefreshChange(uint8_t idx) {
    ecuRefreshIdx = idx;
}

static FISConfigItem ecuConfig[] = {
    {
        "REFRESH",
        { "50ms", "100ms", "250ms", "500ms" },
        4, 1,
        onEcuRefreshChange
    },
    {
        "BLOCK",
        { "GROUP 2", "GROUP 11", "GROUP 20" },
        3, 0,
        nullptr
    },
};

// ---- Page 2: Multimedia ----

void multiRender() {
    fis.sendStringFS(0,  8, 1, "MULTIMEDIA");
    fis.sendStringFS(0, 24, 1, "-- NO SIGNAL --");
}

void multiReset() {
    fis.initFullScreen();
}

// ---- Page 3: TPMS ----

void tpmsRender() {
    fis.sendStringFS(0,  8, 1, "TPMS");
    fis.sendStringFS(0, 24, 1, "FL: --- kPa");
    fis.sendStringFS(0, 32, 1, "FR: --- kPa");
    fis.sendStringFS(0, 40, 1, "RL: --- kPa");
    fis.sendStringFS(0, 48, 1, "RR: --- kPa");
}

void tpmsReset() {
    fis.initFullScreen();
    fis.sendStringFS(0, 24, 1, "TPMS REINIT...");
}

// ---- Page table ----

static FISPage pages[] = {
    { "ECU",   ecuEnter,  ecuRender,   ecuReset,  ecuConfig, 2 },
    { "MULTI", nullptr,   multiRender, multiReset, nullptr,  0 },
    { "TPMS",  nullptr,   tpmsRender,  tpmsReset,  nullptr,  0 },
};

// ---- setup / loop ----

void setup() {
    Serial.begin(115200);

    stalk.addButton(A8,  VAGFISPages::BTN_UP);
    stalk.addButton(A9,  VAGFISPages::BTN_RST);
    stalk.addButton(A10, VAGFISPages::BTN_DN);

    fis.begin();
    nav.begin();   // activates PCINT interrupts

    for (uint8_t i = 0; i < 3; i++) nav.addPage(pages[i]);

    Serial.println(F("VAGFISPages ready"));
}

void loop() {
    nav.update();

    if (!nav.isCustom()) return;

    // Poll your data sources here. Call nav.markDirty() whenever displayed
    // values change — that triggers onRender once without any blinking.
    // Example: if (kwp.poll()) nav.markDirty();
}
