/* Host test of the settings ring on a simulated 2 KiB flash area.
 * Run: sim/settings_test.sh */
#define SIM_FLASH 1
#include <stdarg.h>
#include <stdio.h>
#include "../src/settings.c"
#include "mock.h"

#define AREA 0x0800F000UL
static uint8_t g_flash[2048];
static int g_erases[8];

const void *sim_flash_ptr(uint32_t addr) { return &g_flash[addr - AREA]; }
FLASH_Status FLASH_ROM_ERASE(uint32_t a, uint32_t len)
{
    if ((a & 255U) || (len & 255U) || a < AREA || a + len > AREA + sizeof(g_flash)) return FLASH_ALIGN_ERROR;
    memset(&g_flash[a - AREA], 0xFF, len);
    for (uint32_t s = (a - AREA) / 256U; s < (a - AREA + len) / 256U; ++s) ++g_erases[s];
    return FLASH_COMPLETE;
}
FLASH_Status FLASH_ROM_WRITE(uint32_t a, uint32_t *buf, uint32_t len)
{
    if ((a & 255U) || (len & 255U) || a < AREA || a + len > AREA + sizeof(g_flash)) return FLASH_ALIGN_ERROR;
    const uint8_t *b = (const uint8_t *)buf;
    for (uint32_t i = 0; i < len; ++i) g_flash[a - AREA + i] &= b[i];   /* can only clear bits */
    return FLASH_COMPLETE;
}

/* UI stubs */
static bool g_flip, g_menu;
bool ui_display_flipped(void) { return g_flip; }
void ui_set_display_flipped(bool f) { g_flip = f; }
bool ui_settings_active(void) { return g_menu; }

static int g_fail;
static void check(bool ok, const char *fmt, ...)
{
    va_list a; va_start(a, fmt);
    printf("%s ", ok ? "PASS" : "FAIL"); vprintf(fmt, a); printf("\n");
    va_end(a);
    if (!ok) ++g_fail;
}

static void boot(void)
{
    s_loaded = false; s_seq = 0; s_slot = SETTINGS_SLOTS - 1U;
    memset(&s_saved, 0, sizeof(s_saved));
    mock_reset(25.0);
    mock_cycle();
    settings_init();
    control_init();
}

/* Turn the knob to t and click on the main screen (commit + save). */
static void knob_to(int16_t t)
{
    control_set_target_c(t);
    control_commit_target();
    settings_save();
}

static int saves(void) { int n = 0; for (int i = 0; i < 8; ++i) n += g_erases[i]; return n; }

int main(void)
{
    memset(g_flash, 0xFF, sizeof(g_flash));
    boot();
    check(control_get()->target_c == 300, "empty flash: C245 default 300 (%d)", control_get()->target_c);

    knob_to(330);
    check(saves() == 1, "set 330 + click: saved once (%d writes)", saves());
    settings_save();
    check(saves() == 1, "saving again without a change does not write");
    boot();
    check(control_get()->target_c == 330, "after power cycle: 330 (%d)", control_get()->target_c);

    control_set_target_c(345);
    check(control_target_pending() && saves() == 1, "turning the knob alone does not save");
    boot();
    check(control_get()->target_c == 330, "unclicked 345 is not kept after power cycle (%d)",
          control_get()->target_c);
    knob_to(350);
    check(saves() == 2 && !control_target_pending(), "click saves 350 (%d writes)", saves());

    control_select_profile(TIP_C210);
    check(control_get()->target_c == 280, "C210 starts at its default 280 (%d)", control_get()->target_c);
    knob_to(260);
    boot();
    check(profile_active_index() == TIP_C210 && control_get()->target_c == 260,
          "after power cycle: C210 at 260 (%d)", control_get()->target_c);
    control_select_profile(TIP_C245);
    check(control_get()->target_c == 350, "switching back: C245 remembers 350 (%d)", control_get()->target_c);

    for (int k = 0; k < 30; ++k) knob_to((int16_t)(300 + (k % 2) * 10 + k));
    int16_t last = control_get()->target_c;
    boot();
    check(control_get()->target_c == last, "after 30 more saves (ring wrapped): %d (%d)", last,
          control_get()->target_c);
    int lo = 1 << 30, hi = 0;
    for (int i = 0; i < 8; ++i) { if (g_erases[i] < lo) lo = g_erases[i]; if (g_erases[i] > hi) hi = g_erases[i]; }
    check(hi - lo <= 1, "wear spread over 8 slots (min %d, max %d erases)", lo, hi);

    /* Power lost right after erasing the next slot: previous value survives. */
    int16_t before = control_get()->target_c;
    control_set_target_c(420);
    uint8_t next = (uint8_t)((s_slot + 1U) % SETTINGS_SLOTS);
    FLASH_ROM_ERASE(AREA + next * 256U, 256U);       /* the write never happened */
    boot();
    check(control_get()->target_c == before, "power cut during a save: keeps %d (%d)", before,
          control_get()->target_c);
    /* ...and a torn record (half written) is ignored too */
    knob_to(430);
    uint8_t cur = s_slot;
    memset(&g_flash[cur * 256U + 20U], 0x00, 8);
    boot();
    check(control_get()->target_c == before, "corrupt newest slot ignored: %d (%d)", before,
          control_get()->target_c);

    /* A menu save (Save and exit) keeps the last clicked value, not an
     * unclicked knob position. */
    control_set_target_c(380);
    control_set_boost_time_s(60);
    settings_save();
    boot();
    check(control_get()->target_c == before && control_boost_time_s() == 60,
          "menu save keeps clicked temp %d, not unclicked 380 (%d)", before, control_get()->target_c);

    /* Supply trim survives a power cycle; factory values elsewhere untouched. */
    measure_set_vin_trim(26);
    settings_save();
    boot();
    check(measure_vin_trim() == 26, "supply trim +2.6%% kept after power cycle (%d)", measure_vin_trim());
    measure_set_vin_trim(0); settings_save();

    /* Migration from a v10 record (firmware 1.0.0-1.4.0) at 0x0800F400. */
    memset(g_flash, 0xFF, sizeof(g_flash));
    settings_record_v10_t old;
    memset(&old, 0, sizeof(old));
    old.magic = SETTINGS_MAGIC; old.version = 10U; old.size = sizeof(old);
    old.active_tip = TIP_C245; old.encoder_step_c = 1; old.backlight_percent = 60;
    old.flags = FLAG_BUZZER; old.boost_delta_c = 40; old.boost_time_s = 45;
    old.sleep_delay_s = 9; old.sleep_off_min = 7; old.sleep_temp_c = 180; old.max_target_c = 420;
    old.ambient_c = 29;
    for (int t = 0; t < TIP_COUNT; ++t)
        for (int i = 0; i < 6; ++i) old.cal_c[t][i] = (int16_t)(100 + i * 70 + t);
    old.cal_c[0][0] = 105; old.calibrated[0] = 1;
    old.checksum = checksum_bytes(&old, offsetof(settings_record_v10_t, checksum));
    memcpy(&g_flash[0x400], &old, sizeof(old));
    boot();
    check(control_boost_delta_c() == 40 && control_boost_time_s() == 45 &&
          control_sleep_delay_s() == 9 && control_sleep_off_min() == 7 &&
          control_sleep_temp_c() == 180 && profile_ambient_c() == 29 &&
          board_backlight_get() == 60 && profile_get(TIP_C245)->temp_cal_c[0] == 105,
          "v10 settings migrated (boost, sleep, room temp, brightness, C245 cal)");
    knob_to(310);
    boot();
    check(control_get()->target_c == 310 && control_boost_time_s() == 45,
          "after migration the ring takes over (310, boost 45 s kept)");

    printf("\n%d check(s) failed\n", g_fail);
    return g_fail ? 1 : 0;
}
