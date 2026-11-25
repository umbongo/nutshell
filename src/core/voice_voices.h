#ifndef NUTSHELL_VOICE_VOICES_H
#define NUTSHELL_VOICE_VOICES_H

/*
 * voice_voices -- pure rule for choosing the Windows voice that reads replies
 * aloud: English (UK) woman first. The registry enumeration that fills
 * VoiceInfo lives in src/ui. Spec: docs/superpowers/specs/
 * 2026-11-24-voice-live-conversation-design.md section 8.3.
 */

#include <stddef.h>

#define VOICE_INFO_MAX 64

typedef struct {
    char id[256];     /* full token path, e.g. "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens\\TTS_MS_EN-GB_HAZEL_11.0" */
    char name[128];   /* Attributes\Name, e.g. "Microsoft Hazel Desktop" */
    char lang[32];    /* Attributes\Language, hex LCIDs separated by ';', e.g. "809;9" */
    int  female;      /* Attributes\Gender == "Female" (case-insensitive) */
    int  onecore;     /* from the Speech_OneCore tree */
} VoiceInfo;

int    voice_info_lcid_primary(const VoiceInfo *v);   /* first LCID, 0 if none */
int    voice_info_is_en_gb(const VoiceInfo *v);       /* any LCID == 0x809 */
int    voice_info_is_english(const VoiceInfo *v);     /* any LCID with (lcid & 0x3FF) == 0x09 */

/* One entry per display name (case-insensitive, " Desktop" removed). Of two
 * entries with the same name the SAPI desktop one (onecore == 0) is kept:
 * SAPI tokens are certain to work with ISpVoice::SetVoice. The kept entry
 * holds the first one's position; order is otherwise kept. Returns the new
 * count. */
size_t voice_list_dedupe(VoiceInfo *v, size_t n);

/* The voice's name with " Desktop" removed, truncated to cap. */
void   voice_display_name(const VoiceInfo *v, char *out, size_t cap);

/* Automatic choice: an en-GB woman named Hazel, then Susan, then any other
 * en-GB woman; else any English woman; else any en-GB voice; else -1 (the
 * system default). Ties keep list order. */
int    voice_choose_auto(const VoiceInfo *v, size_t n);

/* Index of the voice whose id equals `id` (case-insensitive), or -1. */
int    voice_find_saved(const VoiceInfo *v, size_t n, const char *id);

/* 1 when the one-time "no English (UK) woman's voice" notice is due. */
int    voice_notice_due(const VoiceInfo *v, size_t n, int resolved, int using_auto,
                        int shown_this_launch);

#endif
