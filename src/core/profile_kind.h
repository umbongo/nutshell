#ifndef NUTSHELL_PROFILE_KIND_H
#define NUTSHELL_PROFILE_KIND_H

#include "session_io.h"

/* The transport a saved profile (Profile.kind string) selects. */
typedef enum { PROFILE_KIND_SSH = 0, PROFILE_KIND_LOCAL, PROFILE_KIND_TELNET } ProfileKind;

/* "local" and "telnet" exact; anything else, NULL included, is SSH. */
ProfileKind profile_kind_of(const char *kind);

/* "ssh", "local", "telnet". */
const char *profile_kind_name(ProfileKind k);

/* 22, 0, 23. */
int profile_kind_default_port(ProfileKind k);

/* 22 -> 23 going SSH -> Telnet and 23 -> 22 going back; any other port,
 * and any other pair of kinds, is returned unchanged. */
int profile_kind_swap_port(int port, ProfileKind from, ProfileKind to);

SessionKind profile_kind_session(ProfileKind k);

/* "ssh", "local", "telnet"; an unknown value reads as "ssh". */
const char *session_kind_name(SessionKind k);

#endif
