#include "profile_kind.h"

#include <string.h>

ProfileKind profile_kind_of(const char *kind)
{
    if (kind && strcmp(kind, "local") == 0) return PROFILE_KIND_LOCAL;
    if (kind && strcmp(kind, "telnet") == 0) return PROFILE_KIND_TELNET;
    return PROFILE_KIND_SSH;
}

const char *profile_kind_name(ProfileKind k)
{
    switch (k) {
    case PROFILE_KIND_LOCAL:  return "local";
    case PROFILE_KIND_TELNET: return "telnet";
    case PROFILE_KIND_SSH:
    default:                  return "ssh";
    }
}

int profile_kind_default_port(ProfileKind k)
{
    switch (k) {
    case PROFILE_KIND_LOCAL:  return 0;
    case PROFILE_KIND_TELNET: return 23;
    case PROFILE_KIND_SSH:
    default:                  return 22;
    }
}

int profile_kind_swap_port(int port, ProfileKind from, ProfileKind to)
{
    if (from == PROFILE_KIND_SSH && to == PROFILE_KIND_TELNET && port == 22) return 23;
    if (from == PROFILE_KIND_TELNET && to == PROFILE_KIND_SSH && port == 23) return 22;
    return port;
}

SessionKind profile_kind_session(ProfileKind k)
{
    switch (k) {
    case PROFILE_KIND_LOCAL:  return SESSION_LOCAL;
    case PROFILE_KIND_TELNET: return SESSION_TELNET;
    case PROFILE_KIND_SSH:
    default:                  return SESSION_SSH;
    }
}

const char *session_kind_name(SessionKind k)
{
    switch (k) {
    case SESSION_LOCAL:  return "local";
    case SESSION_TELNET: return "telnet";
    case SESSION_SSH:
    default:             return "ssh";
    }
}
