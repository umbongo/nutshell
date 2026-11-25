#ifndef NUTSHELL_VOICE_VOICES_WIN_H
#define NUTSHELL_VOICE_VOICES_WIN_H

#include <windows.h>
#include <stddef.h>
#include "voice_voices.h"

/*
 * voice_voices_win -- the installed Windows speech voices, read straight from
 * the registry (never loads sapi.dll).
 */

/* Each subkey of `sapi_tokens` (onecore = 0) and then of `onecore_tokens`
 * (onecore = 1) is a voice; either path may be NULL. root must be
 * HKEY_LOCAL_MACHINE or HKEY_CURRENT_USER. id = the root's name + "\" + path
 * + "\" + subkey. Name, Gender, Language come from the Attributes subkey
 * (name falls back to the token's default value). Not deduped. */
size_t voice_voices_read(HKEY root, const wchar_t *sapi_tokens, const wchar_t *onecore_tokens,
                         VoiceInfo *out, size_t max);

/* HKLM\SOFTWARE\Microsoft\Speech\Voices\Tokens and
 * HKLM\SOFTWARE\Microsoft\Speech_OneCore\Voices\Tokens, deduped
 * (voice_list_dedupe keeps the SAPI desktop token). */
size_t voice_voices_list(VoiceInfo *out, size_t max);

#endif
