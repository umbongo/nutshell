#include "opt_feature.h"

static const OptFeatureInfo k_features[OPT_COUNT] = {
    { OPT_AI, "AI assistant", "ai_enabled", -1 },
    { OPT_VOICE, "Voice", "voice_enabled", OPT_AI },
};

const OptFeatureInfo *opt_feature_table(size_t *n)
{
    if (n) *n = (size_t)OPT_COUNT;
    return k_features;
}

int opt_feature_enabled(const Settings *s, OptFeature f)
{
    if (!s) return 0;
    switch (f) {
    case OPT_AI:    return s->ai_enabled ? 1 : 0;
    case OPT_VOICE: return s->voice_enabled ? 1 : 0;
    default:     return 0;
    }
}

const char *opt_feature_blocked_reason(const Settings *s, int requires)
{
    if (requires == (int)OPT_AI && !opt_feature_enabled(s, OPT_AI)) {
        return "Needs the AI assistant";
    }
    return NULL;
}

int opt_feature_active(const Settings *s, OptFeature f)
{
    if (!opt_feature_enabled(s, f)) return 0;
    for (size_t i = 0; i < (size_t)OPT_COUNT; i++) {
        if (k_features[i].id == f && k_features[i].requires >= 0)
            return opt_feature_active(s, (OptFeature)k_features[i].requires);
    }
    return 1;
}
