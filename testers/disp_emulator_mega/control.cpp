#include "control.h"
#include "board_mega.h"
#include <avr/wdt.h>
#include <Adafruit_SSD1306.h>

extern Adafruit_SSD1306 oled;           // created in disp_emulator_mega.ino

emu_state_t emu = { CTL_MENU, false, false, -1, -1, 0, 42300, 125, false, false, false, false, "" };

// Survives the watchdog restart used by MODE: which display type to start after it.
static uint32_t boot_magic __attribute__((section(".noinit")));
static int8_t boot_mode __attribute__((section(".noinit")));
static const uint32_t c_boot_magic = 0x5A6E3C21UL;

static char line[48];
static uint8_t line_len = 0;
static int16_t pending_steps = 0;
static bool pending_enter = false;
static bool pending_back = false;

// front-panel edge detection (for events and the screen)
static const uint8_t c_watch_pins[] = { c_pin_btn_up, c_pin_btn_down, c_pin_btn_enter, c_pin_btn_back,
                                        c_pin_btn_nzl_status, c_pin_btn_pumping };
static const char *const c_watch_names[] = { "UP", "DOWN", "ENTER", "BACK", "NOZZLE", "PUMP" };
static uint8_t watch_last[sizeof(c_watch_pins)];

static const char *const c_mode_names[] = { "MENU", "SAN6", "CEN6", "CEN7", "C7CS", "WAY6", "HY8", "WAY2" };
static const char *mode_name(int8_t m) {
    return (m >= CTL_SAN6 && m <= CTL_WAY2) ? c_mode_names[m] : "MENU";
}
const char *control_mode_name(int8_t m) { return mode_name(m); }
static int8_t mode_from_name(const char *s) {
    if (!s) return -1;
    for (int8_t m = CTL_SAN6; m <= CTL_WAY2; m++) if (!strcmp(s, c_mode_names[m])) return m;
    return -1;
}

void control_event(const char *what, const char *detail) {
    Serial.print(F("EVT "));
    Serial.print(what);
    if (detail) { Serial.print(' '); Serial.print(detail); }
    Serial.println();
}

static void set_last(const char *a, const char *b) {
    snprintf(emu.last_event, sizeof(emu.last_event), "%s %s", a, b);
}

uint32_t control_total() {
    return (uint32_t)((uint64_t)emu.volume_ml * emu.unit_price / 1000);
}

void control_init() {
    for (uint8_t i = 0; i < sizeof(c_watch_pins); i++) watch_last[i] = digitalRead(c_watch_pins[i]);
    control_event("READY", "disp_emulator_mega");
}

int8_t control_boot_mode() {
    int8_t m = (boot_magic == c_boot_magic) ? boot_mode : -1;
    boot_magic = 0;
    return (m >= CTL_SAN6 && m <= CTL_WAY2) ? m : -1;
}

void control_restart(int8_t start_mode) {
    boot_mode = start_mode;
    boot_magic = start_mode >= 0 ? c_boot_magic : 0;
    Serial.flush();
    wdt_enable(WDTO_15MS);
    while (true) {}
}

bool control_nozzle(bool switch_pressed) {
    bool v = emu.remote_nozzle >= 0 ? emu.remote_nozzle : switch_pressed;
    if (v != emu.nozzle) { emu.nozzle = v; control_event("NOZZLE", v ? "1" : "0"); }
    return v;
}

bool control_pumping(bool button_pressed) {
    bool v = emu.remote_pumping >= 0 ? emu.remote_pumping : button_pressed;
    if (v != emu.pumping) { emu.pumping = v; control_event("PUMP", v ? "1" : "0"); }
    return v;
}

int16_t control_take_steps() { int16_t s = pending_steps; pending_steps = 0; return s; }
bool control_take_enter() { bool e = pending_enter; pending_enter = false; return e; }
bool control_take_back() { bool b = pending_back; pending_back = false; return b; }

// ── commands ────────────────────────────────────────────────────────────────

static bool parse_u32(const char *s, uint32_t &out) {
    if (!s || !*s) return false;
    uint32_t v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return false;
        v = v * 10 + (uint32_t)(*s - '0');
    }
    out = v;
    return true;
}

// 1 / 0 / AUTO -> 1, 0, -1 ; anything else -> -2
static int8_t parse_onoff(const char *s) {
    if (!s) return -2;
    if (!strcmp(s, "1") || !strcmp(s, "ON") || !strcmp(s, "UP")) return 1;
    if (!strcmp(s, "0") || !strcmp(s, "OFF") || !strcmp(s, "DOWN")) return 0;
    if (!strcmp(s, "AUTO")) return -1;
    return -2;
}

static void print_status() {
    uint32_t t = control_total();
    Serial.print(F("OK STATUS mode=")); Serial.print(mode_name(emu.mode));
    Serial.print(F(" nozzle=")); Serial.print(emu.nozzle ? 1 : 0);
    Serial.print(F(" pump=")); Serial.print(emu.pumping ? 1 : 0);
    Serial.print(F(" vol_ml=")); Serial.print(emu.volume_ml);
    Serial.print(F(" price=")); Serial.print(emu.unit_price);
    Serial.print(F(" total=")); Serial.print(t);
    Serial.print(F(" step_ml=")); Serial.print(emu.step_ml);
    Serial.print(F(" ctl_nozzle=")); Serial.print(emu.remote_nozzle < 0 ? F("switch") : F("pc"));
    Serial.print(F(" ctl_pump=")); Serial.println(emu.remote_pumping < 0 ? F("button") : F("pc"));
}

static void handle(char *cmd) {
    for (char *p = cmd; *p; p++) *p = toupper(*p);
    char *verb = strtok(cmd, " \t");
    char *arg = strtok(nullptr, " \t");
    if (!verb) return;

    if (!strcmp(verb, "PING")) { Serial.println(F("OK PONG")); return; }
    if (!strcmp(verb, "HELP")) {
        Serial.println(F("OK HELP PING STATUS MODE<SAN6|CEN6|CEN7|C7CS|WAY6|HY8|WAY2> MENU NOZZLE<1|0|AUTO> PUMP<1|0|AUTO> PRICE<x100> VOLUME<ml> STEP<ml> BUTTON<UP|DOWN|ENTER|BACK> DEBUG<1|0>"));
        return;
    }
    if (!strcmp(verb, "STATUS")) { print_status(); return; }
    if (!strcmp(verb, "MODE")) {
        int8_t m = mode_from_name(arg);
        if (m < 0) { Serial.println(F("ERR MODE needs SAN6, CEN6, CEN7, C7CS, WAY6, HY8 or WAY2")); return; }
        Serial.print(F("OK MODE ")); Serial.println(mode_name(m));
        if (emu.mode == m) return;
        control_restart(m);                    // restart, then start that display type
    }
    if (!strcmp(verb, "MENU")) {
        Serial.println(F("OK MENU"));
        if (emu.mode != CTL_MENU) control_restart(-1);
        return;
    }
    if (!strcmp(verb, "NOZZLE") || !strcmp(verb, "PUMP")) {
        int8_t v = parse_onoff(arg);
        if (v < -1) { Serial.print(F("ERR ")); Serial.print(verb); Serial.println(F(" needs 1, 0 or AUTO")); return; }
        if (verb[0] == 'N') emu.remote_nozzle = v; else emu.remote_pumping = v;
        Serial.print(F("OK ")); Serial.print(verb); Serial.print(' '); Serial.println(arg);
        set_last(verb, "PC");
        return;
    }
    if (!strcmp(verb, "PRICE") || !strcmp(verb, "VOLUME") || !strcmp(verb, "STEP")) {
        uint32_t v;
        if (!parse_u32(arg, v)) { Serial.print(F("ERR ")); Serial.print(verb); Serial.println(F(" needs a whole number")); return; }
        if (verb[0] == 'P') { emu.unit_price = v; emu.price_set = true; }
        else if (verb[0] == 'V') emu.volume_ml = v;
        else { emu.step_ml = v; emu.step_set = true; }
        emu.dirty = true;
        Serial.print(F("OK ")); Serial.print(verb); Serial.print(' '); Serial.println(v);
        set_last(verb, "PC");
        return;
    }
    if (!strcmp(verb, "BUTTON")) {
        if (!arg) { Serial.println(F("ERR BUTTON needs UP, DOWN, ENTER or BACK")); return; }
        if (!strcmp(arg, "UP")) pending_steps--;
        else if (!strcmp(arg, "DOWN")) pending_steps++;
        else if (!strcmp(arg, "ENTER")) pending_enter = true;
        else if (!strcmp(arg, "BACK")) pending_back = true;
        else { Serial.println(F("ERR BUTTON needs UP, DOWN, ENTER or BACK")); return; }
        Serial.print(F("OK BUTTON ")); Serial.println(arg);
        set_last(arg, "PC");
        return;
    }
    if (!strcmp(verb, "DEBUG")) {
        int8_t v = parse_onoff(arg);
        if (v < 0) { Serial.println(F("ERR DEBUG needs 1 or 0")); return; }
        emu.debug = v;
        Serial.print(F("OK DEBUG ")); Serial.println(v);
        return;
    }
    Serial.print(F("ERR unknown command ")); Serial.println(verb);
}

void control_poll() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r' || c == '\n') {
            if (line_len) { line[line_len] = '\0'; handle(line); line_len = 0; }
        } else if (line_len < sizeof(line) - 1) {
            line[line_len++] = c;
        } else {
            line_len = 0;                       // too long: drop the line
            Serial.println(F("ERR line too long"));
        }
    }
    // front-panel presses → events + "last" on the screen
    for (uint8_t i = 0; i < sizeof(c_watch_pins); i++) {
        uint8_t v = digitalRead(c_watch_pins[i]);
        if (v != watch_last[i]) {
            watch_last[i] = v;
            if (v == LOW) { control_event("BUTTON", c_watch_names[i]); set_last(c_watch_names[i], "button"); }
        }
    }
}

// ── status screen (running) ─────────────────────────────────────────────────

static void print_fixed(uint32_t v, uint8_t decimals) {   // e.g. 12375, 3 -> "12.375"
    uint32_t div = 1;
    for (uint8_t i = 0; i < decimals; i++) div *= 10;
    oled.print(v / div);
    oled.print('.');
    uint32_t frac = v % div;
    for (uint32_t d = div / 10; d > 1 && frac < d; d /= 10) oled.print('0');
    oled.print(frac);
}

void control_screen(bool force) {
    static uint32_t last_ms = 0;
    static uint32_t sig = 0;
    // redraw only when something changed, and at most 4 times a second (I2C redraw pauses the bus output)
    uint32_t s = emu.volume_ml * 31u + emu.unit_price * 7u + emu.nozzle * 3u + emu.pumping * 5u
                 + (emu.remote_nozzle + 1) * 11u + (emu.remote_pumping + 1) * 13u + (uint8_t)emu.last_event[0] * 17u
                 + (uint8_t)emu.last_event[1] * 19u;
    if (!force && (s == sig || millis() - last_ms < 250)) return;
    sig = s;
    last_ms = millis();

    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    // top (yellow band, 16 px): mode + nozzle / pump
    oled.setTextSize(2);
    oled.setCursor(0, 0);
    oled.print(mode_name(emu.mode));
    oled.setTextSize(1);
    oled.setCursor(62, 0);
    oled.print(F("NOZ ")); oled.print(emu.nozzle ? F("UP") : F("--"));
    oled.setCursor(62, 8);
    oled.print(F("PMP ")); oled.print(emu.pumping ? F("ON") : F("--"));
    // values (blue area)
    oled.setCursor(0, 18); oled.print(F("Vol   ")); print_fixed(emu.volume_ml, 3); oled.print(F(" L"));
    oled.setCursor(0, 27); oled.print(F("Price ")); print_fixed(emu.unit_price, 2);
    oled.setCursor(0, 36); oled.print(F("Total ")); print_fixed(control_total(), 2);
    oled.setCursor(0, 46);
    oled.print(F("Ctl "));
    oled.print(emu.remote_nozzle < 0 ? F("sw") : F("PC")); oled.print('/');
    oled.print(emu.remote_pumping < 0 ? F("btn") : F("PC"));
    oled.setCursor(0, 56); oled.print(F("Last ")); oled.print(emu.last_event[0] ? emu.last_event : "-");
    oled.display();
}
