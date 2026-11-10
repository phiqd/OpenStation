#include "settings.h"
#include "app_config.h"
#include "board.h"
#include "control.h"
#include "encoder.h"
#include "measure.h"
#include "power.h"
#include "profiles.h"
#include "ch32x035.h"
#include "ui.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SETTINGS_MAGIC        0x534C4452UL
#define SETTINGS_VERSION      11U
/* Settings live in the last 2 KiB of flash as a ring of eight 256-byte slots
 * (the chip erases and programs 256-byte pages). Each save goes to the next
 * slot with a higher sequence number, so wear is spread 8 ways and a power
 * cut during a write always leaves the previous record intact. */
#define SETTINGS_BASE         0x0800F000UL
#define SETTINGS_SLOT_BYTES   256U
#define SETTINGS_SLOTS        8U
/* Records written by 1.0.0-1.4.0 (v10) and by the original firmware (v9). */
#define SETTINGS_LEGACY_ADDR  0x0800F400UL

#ifdef SIM_FLASH
#define FLASH_PTR(addr) sim_flash_ptr(addr)
#else
#define FLASH_PTR(addr) ((const void *)(uintptr_t)(addr))
#endif

#define FLAG_BUZZER      0x01U
#define FLAG_READY_BEEP  0x02U
#define FLAG_TIP_WARNING 0x04U
#define FLAG_FLIP        0x08U
#define FLAG_ENC_REVERSE 0x10U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t active_tip;
    uint8_t encoder_step_c;
    uint8_t backlight_percent;
    uint8_t flags;
    uint8_t boost_delta_c;
    uint8_t boost_time_s;
    uint8_t sleep_delay_s;
    uint8_t sleep_off_min;
    int16_t sleep_temp_c;
    uint16_t max_target_c;
    uint16_t presets_c[3];
    int16_t ambient_c;      /* 0 in records written by 1.0.0/1.0.1 = 25 C */
    int16_t cal_c[TIP_COUNT][PROFILE_CAL_POINT_COUNT];
    uint8_t calibrated[TIP_COUNT];
    int8_t vin_trim;               /* supply reading trim, 0.1 % (was reserved) */
    uint8_t reserved;
    int16_t target_c[TIP_COUNT];   /* last set temperature per handle */
    uint32_t seq;                  /* ring position; excluded from comparisons */
    uint32_t checksum;
} settings_record_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t active_tip;
    uint8_t encoder_step_c;
    uint8_t backlight_percent;
    uint8_t flags;
    uint8_t boost_delta_c;
    uint8_t boost_time_s;
    uint8_t sleep_delay_s;
    uint8_t sleep_off_min;
    int16_t sleep_temp_c;
    uint16_t max_target_c;
    uint16_t presets_c[3];
    int16_t ambient_c;      /* 0 in records written by 1.0.0/1.0.1 = 25 C */
    int16_t cal_c[TIP_COUNT][PROFILE_CAL_POINT_COUNT];
    uint8_t calibrated[TIP_COUNT];
    uint8_t reserved[2];
    uint32_t checksum;
} settings_record_v10_t;

_Static_assert(sizeof(settings_record_t) <= SETTINGS_SLOT_BYTES, "settings exceed a slot");
_Static_assert(offsetof(settings_record_v10_t, checksum) == offsetof(settings_record_t, target_c),
               "v10 prefix must match");

/* ---- Original OpenStation 0.1.0 Build 13/14 record (version 9), read once
 * so an existing station keeps its boost, sleep, preset and C245 data. ---- */
typedef struct {
    uint8_t id; char name[8]; uint16_t revision;
    uint16_t rmin, rmax, rnom, uv_per_c; int16_t offset_c;
    uint16_t kp, ki, kd, max_integral;
    int16_t cal_c[PROFILE_CAL_POINT_COUNT];
    uint32_t max_power_mw;
    uint16_t max_duty, rated_mv, settle_us, rise_timeout_ms;
    int16_t default_c, sleep_c, maximum_c;
    uint8_t characterized;
} legacy_profile_t;

typedef struct {
    uint32_t magic; uint16_t version; uint16_t size;
    uint8_t active_profile, encoder_step_c, backlight_percent, buzzer_enabled;
    uint8_t notification_flags, encoder_reversed, ignore_faults, drop_compensation;
    uint32_t sleep_delay_ms;
    legacy_profile_t profiles[9];
    uint16_t presets_c[3];
    uint16_t max_target_c;
    uint16_t sleep_time_s;
    uint8_t sleep_type, reserved, boost_delta_c, boost_time_s;
    uint32_t checksum;
} legacy_v9_record_t;

_Static_assert(sizeof(legacy_profile_t) == 64U, "legacy profile layout");
_Static_assert(sizeof(legacy_v9_record_t) == 616U, "legacy v9 layout");

static settings_record_t s_saved;
static bool s_loaded;
static uint32_t s_seq;
static uint8_t s_slot = SETTINGS_SLOTS - 1U;   /* next save goes to slot 0 */
static bool s_tip_warning_enabled = true;

static uint32_t checksum_bytes(const void *record, size_t count)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t hash = 2166136261UL;
    for (size_t i = 0; i < count; ++i) {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash;
}

static uint32_t checksum_record(const settings_record_t *r)
{
    return checksum_bytes(r, offsetof(settings_record_t, checksum));
}

static void capture(settings_record_t *r)
{
    memset(r, 0, sizeof(*r));
    r->magic = SETTINGS_MAGIC;
    r->version = SETTINGS_VERSION;
    r->size = sizeof(*r);
    r->active_tip = profile_active_index();
    r->encoder_step_c = control_encoder_step_c();
    r->backlight_percent = board_backlight_get();
    r->flags = (board_buzzer_enabled() ? FLAG_BUZZER : 0U) |
               (board_setpoint_beep_get() ? FLAG_READY_BEEP : 0U) |
               (s_tip_warning_enabled ? FLAG_TIP_WARNING : 0U) |
               (ui_display_flipped() ? FLAG_FLIP : 0U) |
               (input_encoder_reversed() ? FLAG_ENC_REVERSE : 0U);
    r->boost_delta_c = control_boost_delta_c();
    r->boost_time_s = control_boost_time_s();
    r->sleep_delay_s = control_sleep_delay_s();
    r->sleep_off_min = control_sleep_off_min();
    r->sleep_temp_c = control_sleep_temp_c();
    r->max_target_c = control_max_target_c();
    r->ambient_c = profile_ambient_c();
    r->vin_trim = measure_vin_trim();
    for (uint8_t i = 0; i < 3U; ++i) r->presets_c[i] = control_preset_c(i);
    for (uint8_t t = 0; t < TIP_COUNT; ++t) {
        memcpy(r->cal_c[t], profile_get(t)->temp_cal_c, sizeof(r->cal_c[t]));
        r->calibrated[t] = profile_get(t)->calibrated;
        r->target_c[t] = control_tip_target(t);
    }
}

static void apply(const settings_record_t *r)
{
    for (uint8_t t = 0; t < TIP_COUNT; ++t) {
        if (profile_set_calibration_values(t, r->cal_c[t]))
            profile_set_calibrated(t, r->calibrated[t] != 0U);
        else profile_set_calibrated(t, false);
    }
    profile_select(r->active_tip < TIP_COUNT ? r->active_tip : TIP_C245);
    control_set_max_target_c(r->max_target_c);
    profile_set_ambient_c(r->ambient_c == 0 ? 25 : r->ambient_c);
    measure_set_vin_trim(r->vin_trim);
    control_set_encoder_step_c(r->encoder_step_c);
    control_set_boost_delta_c(r->boost_delta_c);
    control_set_boost_time_s(r->boost_time_s);
    control_set_sleep_delay_s(r->sleep_delay_s);
    control_set_sleep_off_min(r->sleep_off_min);
    control_set_sleep_temp_c(r->sleep_temp_c);
    for (uint8_t i = 0; i < 3U; ++i) control_set_preset_c(i, r->presets_c[i]);
    for (uint8_t t = 0; t < TIP_COUNT; ++t) control_set_tip_target(t, r->target_c[t]);
    board_backlight_set(r->backlight_percent < 10U ? 10U : r->backlight_percent);
    board_buzzer_enable((r->flags & FLAG_BUZZER) != 0U);
    board_setpoint_beep_enable((r->flags & FLAG_READY_BEEP) != 0U);
    s_tip_warning_enabled = (r->flags & FLAG_TIP_WARNING) != 0U;
    input_set_encoder_reversed((r->flags & FLAG_ENC_REVERSE) != 0U);
    ui_set_display_flipped((r->flags & FLAG_FLIP) != 0U);
}

void settings_factory_defaults(void)
{
    profiles_factory_reset();
    control_set_max_target_c(450U);
    control_set_encoder_step_c(5U);
    control_set_boost_delta_c(50U);
    control_set_boost_time_s(30U);
    control_set_sleep_delay_s(STATION_SLEEP_DELAY_S);
    control_set_sleep_off_min(STATION_SLEEP_OFF_MIN);
    control_set_sleep_temp_c(STATION_SLEEP_TEMP_C);
    control_set_preset_c(0, 300U);
    control_set_preset_c(1, 350U);
    control_set_preset_c(2, 400U);
    for (uint8_t t = 0; t < TIP_COUNT; ++t) control_set_tip_target(t, 0);
    input_set_encoder_reversed(false);
    board_backlight_set(80U);
    board_buzzer_enable(true);
    board_setpoint_beep_enable(true);
    s_tip_warning_enabled = true;
    ui_set_display_flipped(false);
}

static bool migrate_v9(const legacy_v9_record_t *old)
{
    if (old->magic != SETTINGS_MAGIC || old->version != 9U || old->size != sizeof(*old) ||
        old->checksum != checksum_bytes(old, offsetof(legacy_v9_record_t, checksum)))
        return false;
    /* Old index 2 = C245 family, 1 = C210; custom slots encode the family. */
    uint8_t family = old->active_profile < 9U ? old->active_profile :
                     (uint8_t)((old->active_profile - 9U) / 10U);
    profile_select(family == 1U ? TIP_C210 : TIP_C245);
    /* Keep a C245 six-point table only if it was made on the same base scale. */
    const legacy_profile_t *c245 = &old->profiles[2];
    if (c245->uv_per_c == profile_get(TIP_C245)->temp_uv_per_c &&
        c245->offset_c == profile_get(TIP_C245)->temp_offset_c)
        (void)profile_set_calibration_values(TIP_C245, c245->cal_c);
    /* The old C210 table was made on the wrong 8200 uV/C scale: discard. */
    control_set_max_target_c(old->max_target_c);
    control_set_encoder_step_c(old->encoder_step_c);
    input_set_encoder_reversed(old->encoder_reversed != 0U);
    control_set_boost_delta_c(old->boost_delta_c);
    control_set_boost_time_s(old->boost_time_s);
    control_set_sleep_delay_s((uint8_t)(old->sleep_delay_ms / 1000UL));
    control_set_sleep_off_min((uint8_t)((old->sleep_time_s + 59U) / 60U));
    control_set_sleep_temp_c(c245->sleep_c);
    for (uint8_t i = 0; i < 3U; ++i) control_set_preset_c(i, old->presets_c[i]);
    board_backlight_set(old->backlight_percent < 10U ? 10U : old->backlight_percent);
    board_buzzer_enable(old->buzzer_enabled != 0U);
    board_setpoint_beep_enable((old->notification_flags & 1U) != 0U);
    s_tip_warning_enabled = (old->notification_flags & 2U) != 0U;
    uint8_t orientation = (old->notification_flags >> 2U) & 3U;
    ui_set_display_flipped(orientation == 1U || orientation == 3U);
    return true;
}

static bool same_content(const settings_record_t *a, const settings_record_t *b)
{
    return memcmp(a, b, offsetof(settings_record_t, seq)) == 0;
}

static const settings_record_t *slot_record(uint8_t slot)
{
    return (const settings_record_t *)FLASH_PTR(SETTINGS_BASE + (uint32_t)slot * SETTINGS_SLOT_BYTES);
}

static bool slot_valid(const settings_record_t *r)
{
    return r->magic == SETTINGS_MAGIC && r->version == SETTINGS_VERSION &&
           r->size == sizeof(settings_record_t) && r->checksum == checksum_record(r);
}

static bool load_v10(void)
{
    const settings_record_v10_t *old = (const settings_record_v10_t *)FLASH_PTR(SETTINGS_LEGACY_ADDR);
    if (old->magic != SETTINGS_MAGIC || old->version != 10U || old->size != sizeof(*old) ||
        old->checksum != checksum_bytes(old, offsetof(settings_record_v10_t, checksum)))
        return false;
    settings_record_t r;
    memset(&r, 0, sizeof(r));
    memcpy(&r, old, offsetof(settings_record_v10_t, checksum));
    apply(&r);                       /* target_c = 0: tip defaults */
    return true;
}

void settings_init(void)
{
    settings_factory_defaults();
    int8_t best = -1;
    for (uint8_t i = 0; i < SETTINGS_SLOTS; ++i) {
        const settings_record_t *r = slot_record(i);
        if (!slot_valid(r)) continue;
        if (best < 0 || (int32_t)(r->seq - slot_record((uint8_t)best)->seq) > 0) best = (int8_t)i;
    }
    if (best >= 0) {
        memcpy(&s_saved, slot_record((uint8_t)best), sizeof(s_saved));
        s_seq = s_saved.seq;
        s_slot = (uint8_t)best;
        apply(&s_saved);
        s_loaded = true;
        return;
    }
    if (!load_v10()) (void)migrate_v9((const legacy_v9_record_t *)FLASH_PTR(SETTINGS_LEGACY_ADDR));
    /* s_saved stays empty so the first save writes the new format. */
    memset(&s_saved, 0, sizeof(s_saved));
    s_loaded = false;
}

bool settings_dirty(void)
{
    settings_record_t current;
    capture(&current);
    return !s_loaded || !same_content(&current, &s_saved);
}

bool settings_save(void)
{
    static uint32_t page[SETTINGS_SLOT_BYTES / sizeof(uint32_t)];
    settings_record_t current;
    capture(&current);
    if (s_loaded && same_content(&current, &s_saved)) return true;
    uint8_t slot = (uint8_t)((s_slot + 1U) % SETTINGS_SLOTS);
    uint32_t addr = SETTINGS_BASE + (uint32_t)slot * SETTINGS_SLOT_BYTES;
    current.seq = s_seq + 1U;
    current.checksum = checksum_record(&current);
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &current, sizeof(current));
    power_force_off();
    __disable_irq();
    FLASH_Status erased = FLASH_ROM_ERASE(addr, SETTINGS_SLOT_BYTES);
    FLASH_Status written = erased == FLASH_COMPLETE ?
        FLASH_ROM_WRITE(addr, page, SETTINGS_SLOT_BYTES) : erased;
    __enable_irq();
    measure_requalify_quiet_sample();
    if (written != FLASH_COMPLETE) return false;
    if (memcmp(FLASH_PTR(addr), &current, sizeof(current)) != 0) return false;
    memcpy(&s_saved, &current, sizeof(s_saved));
    s_seq = current.seq;
    s_slot = slot;
    s_loaded = true;
    return true;
}


bool settings_loaded_from_flash(void) { return s_loaded; }
bool settings_tip_warning_enabled(void) { return s_tip_warning_enabled; }
void settings_set_tip_warning_enabled(bool enabled) { s_tip_warning_enabled = enabled; }
