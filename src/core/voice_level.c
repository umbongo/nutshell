#include "voice_level.h"
#include <math.h>

double voice_dbfs(int peak)
{
    if (peak <= 0) return VOICE_METER_FLOOR_DB;
    double db = 20.0 * log10((double)peak / 32768.0);
    return db < VOICE_METER_FLOOR_DB ? VOICE_METER_FLOOR_DB : db;
}

VoiceLevelClass voice_level_classify(double peak_db)
{
    if (peak_db < VOICE_LEVEL_SILENT_DB) return VLEVEL_SILENT;
    if (peak_db < VOICE_LEVEL_GOOD_LO_DB) return VLEVEL_QUIET;
    if (peak_db <= VOICE_LEVEL_GOOD_HI_DB) return VLEVEL_GOOD;
    if (peak_db < VOICE_LEVEL_LOUD_DB) return VLEVEL_HOT;
    return VLEVEL_LOUD;
}

const char *voice_level_label(VoiceLevelClass c)
{
    switch (c) {
    case VLEVEL_SILENT: return "Speak to see the level";
    case VLEVEL_QUIET:  return "Too quiet \xE2\x80\x94 raise the gain";
    case VLEVEL_GOOD:
    case VLEVEL_HOT:    return "Good";
    case VLEVEL_LOUD:   return "Too loud \xE2\x80\x94 lower the gain";
    }
    return "";
}

void voice_meter_reset(VoiceMeter *m)
{
    m->level_db = VOICE_METER_FLOOR_DB;
    m->hold_db = VOICE_METER_FLOOR_DB;
    m->hold_age_ms = 0;
    for (int i = 0; i < VOICE_METER_SLOTS; i++) m->win_db[i] = VOICE_METER_FLOOR_DB;
    m->win_pos = 0;
    m->win_acc_ms = 0;
}

static double fall(double db, double ms)
{
    db -= VOICE_METER_RELEASE_DB_PER_S * ms / 1000.0;
    return db < VOICE_METER_FLOOR_DB ? VOICE_METER_FLOOR_DB : db;
}

void voice_meter_feed(VoiceMeter *m, int peak, uint32_t elapsed_ms)
{
    double p = voice_dbfs(peak);

    /* bar: attack at once, release linearly */
    m->level_db = fall(m->level_db, (double)elapsed_ms);
    if (p > m->level_db) m->level_db = p;

    /* hold tick: held for 1000 ms, then released at the same rate */
    if (p >= m->hold_db) {
        m->hold_db = p;
        m->hold_age_ms = 0;
    } else {
        uint32_t before = m->hold_age_ms;
        m->hold_age_ms += elapsed_ms;
        if (m->hold_age_ms > VOICE_METER_HOLD_MS) {
            uint32_t from = before > VOICE_METER_HOLD_MS ? before : VOICE_METER_HOLD_MS;
            m->hold_db = fall(m->hold_db, (double)(m->hold_age_ms - from));
        }
    }

    /* 1 s window of 50 ms slots, each holding its max */
    uint32_t left = elapsed_ms;
    if (left == 0 && p > m->win_db[m->win_pos]) m->win_db[m->win_pos] = p;
    while (left > 0) {
        uint32_t take = VOICE_METER_SLOT_MS - m->win_acc_ms;
        if (take > left) take = left;
        if (p > m->win_db[m->win_pos]) m->win_db[m->win_pos] = p;
        m->win_acc_ms += take;
        left -= take;
        if (m->win_acc_ms >= VOICE_METER_SLOT_MS) {
            m->win_acc_ms = 0;
            m->win_pos = (m->win_pos + 1) % VOICE_METER_SLOTS;
            m->win_db[m->win_pos] = VOICE_METER_FLOOR_DB;
        }
    }
}

double voice_meter_window_peak_db(const VoiceMeter *m)
{
    double best = VOICE_METER_FLOOR_DB;
    for (int i = 0; i < VOICE_METER_SLOTS; i++)
        if (m->win_db[i] > best) best = m->win_db[i];
    return best;
}

double voice_meter_frac(double db)
{
    double f = (db - VOICE_METER_FLOOR_DB) / (0.0 - VOICE_METER_FLOOR_DB);
    return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
}
