#ifndef NUTSHELL_OPT_FEATURE_H
#define NUTSHELL_OPT_FEATURE_H

#include <stddef.h>
#include "../config/config.h"

/*
 * opt_feature -- the optional features Nutshell ships switched off. The AI
 * assistant, and voice, which has `requires = OPT_AI`; opt_feature_blocked_reason()
 * says why one is unavailable. Portable: no Win32.
 */

typedef enum { OPT_AI = 0, OPT_VOICE = 1, OPT_COUNT } OptFeature;

typedef struct {
    OptFeature id;
    const char *name;      /* "AI assistant" */
    const char *json_key;  /* the settings key that holds the switch */
    int requires;          /* another OptFeature, or -1 for none */
} OptFeatureInfo;

const OptFeatureInfo *opt_feature_table(size_t *n);

/* 1 when the feature's switch is on in `s`; 0 when off, for a NULL `s` or an
 * unknown feature. */
int opt_feature_enabled(const Settings *s, OptFeature f);

/* 1 when the feature's switch is on and, when it `requires` another feature,
 * that one is active too; else 0. */
int opt_feature_active(const Settings *s, OptFeature f);

/* Why a feature that needs `requires` is unavailable ("Needs the AI
 * assistant"), or NULL when it is available. `requires` is an OptFeature, or
 * -1 for none. */
const char *opt_feature_blocked_reason(const Settings *s, int requires);

#endif /* NUTSHELL_OPT_FEATURE_H */
