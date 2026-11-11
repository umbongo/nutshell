#ifndef NUTSHELL_SETTINGS_ENTRY_H
#define NUTSHELL_SETTINGS_ENTRY_H

/*
 * settings_entry — the Settings dialog's keep-the-old-value Save rule for
 * numeric edit boxes, as a portable (Win32-free) function.
 *
 * Returns (int)v when parsed_ok is nonzero and v lies in lo..hi, or when v
 * is 0 and zero_ok is nonzero. Otherwise returns `current`: an entry that
 * did not parse or is out of range leaves the stored value alone.
 * lo must be >= 0 and hi must fit in an int.
 */
int settings_entry_or_keep(int parsed_ok, unsigned v, int lo, int hi,
                           int zero_ok, int current);

#endif /* NUTSHELL_SETTINGS_ENTRY_H */
