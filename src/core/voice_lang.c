/* src/core/voice_lang.c -- see voice_lang.h. */
#include "voice_lang.h"
#include <stdio.h>
#include <string.h>

static const VoiceLang k_langs[] = {
    {"", "Auto-detect", ""},
    {"en-GB", "English (UK)", "en"},
    {"en-US", "English (US)", "en"},
    {"ar", "Arabic", "ar"},
    {"zh", "Chinese", "zh"},
    {"nl", "Dutch", "nl"},
    {"fr", "French", "fr"},
    {"de", "German", "de"},
    {"hi", "Hindi", "hi"},
    {"it", "Italian", "it"},
    {"ja", "Japanese", "ja"},
    {"ko", "Korean", "ko"},
    {"pl", "Polish", "pl"},
    {"pt", "Portuguese", "pt"},
    {"ru", "Russian", "ru"},
    {"es", "Spanish", "es"},
    {"sv", "Swedish", "sv"},
    {"tr", "Turkish", "tr"},
    {"uk", "Ukrainian", "uk"},
};
#define N_LANGS (sizeof k_langs / sizeof k_langs[0])

typedef struct { char code[3]; const char *name; } IsoName;

static const IsoName k_iso[] = {
    {"aa","Afar"},{"ab","Abkhazian"},{"ae","Avestan"},{"af","Afrikaans"},{"ak","Akan"},
    {"am","Amharic"},{"an","Aragonese"},{"ar","Arabic"},{"as","Assamese"},{"av","Avaric"},
    {"ay","Aymara"},{"az","Azerbaijani"},{"ba","Bashkir"},{"be","Belarusian"},
    {"bg","Bulgarian"},{"bi","Bislama"},{"bm","Bambara"},{"bn","Bengali"},{"bo","Tibetan"},
    {"br","Breton"},{"bs","Bosnian"},{"ca","Catalan"},{"ce","Chechen"},{"ch","Chamorro"},
    {"co","Corsican"},{"cr","Cree"},{"cs","Czech"},{"cu","Church Slavic"},{"cv","Chuvash"},
    {"cy","Welsh"},{"da","Danish"},{"de","German"},{"dv","Divehi"},{"dz","Dzongkha"},
    {"ee","Ewe"},{"el","Greek"},{"en","English"},{"eo","Esperanto"},{"es","Spanish"},
    {"et","Estonian"},{"eu","Basque"},{"fa","Persian"},{"ff","Fulah"},{"fi","Finnish"},
    {"fj","Fijian"},{"fo","Faroese"},{"fr","French"},{"fy","Western Frisian"},{"ga","Irish"},
    {"gd","Scottish Gaelic"},{"gl","Galician"},{"gn","Guarani"},{"gu","Gujarati"},
    {"gv","Manx"},{"ha","Hausa"},{"he","Hebrew"},{"hi","Hindi"},{"ho","Hiri Motu"},
    {"hr","Croatian"},{"ht","Haitian Creole"},{"hu","Hungarian"},{"hy","Armenian"},
    {"hz","Herero"},{"ia","Interlingua"},{"id","Indonesian"},{"ie","Interlingue"},
    {"ig","Igbo"},{"ii","Sichuan Yi"},{"ik","Inupiaq"},{"io","Ido"},{"is","Icelandic"},
    {"it","Italian"},{"iu","Inuktitut"},{"ja","Japanese"},{"jv","Javanese"},{"ka","Georgian"},
    {"kg","Kongo"},{"ki","Kikuyu"},{"kj","Kuanyama"},{"kk","Kazakh"},{"kl","Kalaallisut"},
    {"km","Khmer"},{"kn","Kannada"},{"ko","Korean"},{"kr","Kanuri"},{"ks","Kashmiri"},
    {"ku","Kurdish"},{"kv","Komi"},{"kw","Cornish"},{"ky","Kyrgyz"},{"la","Latin"},
    {"lb","Luxembourgish"},{"lg","Ganda"},{"li","Limburgish"},{"ln","Lingala"},{"lo","Lao"},
    {"lt","Lithuanian"},{"lu","Luba-Katanga"},{"lv","Latvian"},{"mg","Malagasy"},
    {"mh","Marshallese"},{"mi","Maori"},{"mk","Macedonian"},{"ml","Malayalam"},
    {"mn","Mongolian"},{"mr","Marathi"},{"ms","Malay"},{"mt","Maltese"},{"my","Burmese"},
    {"na","Nauru"},{"nb","Norwegian Bokmal"},{"nd","North Ndebele"},{"ne","Nepali"},
    {"ng","Ndonga"},{"nl","Dutch"},{"nn","Norwegian Nynorsk"},{"no","Norwegian"},
    {"nr","South Ndebele"},{"nv","Navajo"},{"ny","Chichewa"},{"oc","Occitan"},{"oj","Ojibwa"},
    {"om","Oromo"},{"or","Odia"},{"os","Ossetian"},{"pa","Punjabi"},{"pi","Pali"},
    {"pl","Polish"},{"ps","Pashto"},{"pt","Portuguese"},{"qu","Quechua"},{"rm","Romansh"},
    {"rn","Rundi"},{"ro","Romanian"},{"ru","Russian"},{"rw","Kinyarwanda"},{"sa","Sanskrit"},
    {"sc","Sardinian"},{"sd","Sindhi"},{"se","Northern Sami"},{"sg","Sango"},{"si","Sinhala"},
    {"sk","Slovak"},{"sl","Slovenian"},{"sm","Samoan"},{"sn","Shona"},{"so","Somali"},
    {"sq","Albanian"},{"sr","Serbian"},{"ss","Swati"},{"st","Southern Sotho"},{"su","Sundanese"},
    {"sv","Swedish"},{"sw","Swahili"},{"ta","Tamil"},{"te","Telugu"},{"tg","Tajik"},
    {"th","Thai"},{"ti","Tigrinya"},{"tk","Turkmen"},{"tl","Tagalog"},{"tn","Tswana"},
    {"to","Tonga"},{"tr","Turkish"},{"ts","Tsonga"},{"tt","Tatar"},{"tw","Twi"},
    {"ty","Tahitian"},{"ug","Uyghur"},{"uk","Ukrainian"},{"ur","Urdu"},{"uz","Uzbek"},
    {"ve","Venda"},{"vi","Vietnamese"},{"vo","Volapuk"},{"wa","Walloon"},{"wo","Wolof"},
    {"xh","Xhosa"},{"yi","Yiddish"},{"yo","Yoruba"},{"za","Zhuang"},{"zh","Chinese"},
    {"zu","Zulu"},
};
#define N_ISO (sizeof k_iso / sizeof k_iso[0])

/* Other spellings people type. */
static const IsoName k_alias[] = {
    {"mi", "M\xC4\x81ori"}, {"fa", "Farsi"}, {"zh", "Mandarin"}, {"nl", "Flemish"},
    {"ht", "Haitian"}, {"my", "Myanmar"},
};

const VoiceLang *voice_lang_table(size_t *n)
{
    if (n) *n = N_LANGS;
    return k_langs;
}

int voice_lang_find(const char *id)
{
    if (!id) return -1;
    for (size_t i = 0; i < N_LANGS; i++)
        if (strcmp(k_langs[i].id, id) == 0) return (int)i;
    return -1;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (lower(*a) != lower(*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

const char *voice_lang_name(const char *code)
{
    if (!code || strlen(code) != 2) return NULL;
    for (size_t i = 0; i < N_ISO; i++)
        if (k_iso[i].code[0] == code[0] && k_iso[i].code[1] == code[1]) return k_iso[i].name;
    return NULL;
}

int voice_lang_parse_other(const char *text, char *code, size_t cap)
{
    if (code && cap) code[0] = '\0';
    if (!text || !code || cap < 3) return 0;
    while (*text == ' ' || *text == '\t') text++;
    size_t n = strlen(text);
    while (n > 0 && (text[n - 1] == ' ' || text[n - 1] == '\t')) n--;
    if (n == 0 || n > 64) return 0;
    char buf[65];
    memcpy(buf, text, n);
    buf[n] = '\0';
    if (n == 2) {
        char c2[3] = { lower(buf[0]), lower(buf[1]), '\0' };
        if (voice_lang_name(c2)) { memcpy(code, c2, 3); return 1; }
        return 0;
    }
    for (size_t i = 0; i < N_ISO; i++)
        if (ieq(buf, k_iso[i].name)) { memcpy(code, k_iso[i].code, 3); return 1; }
    for (size_t i = 0; i < sizeof k_alias / sizeof k_alias[0]; i++)
        if (ieq(buf, k_alias[i].name)) { memcpy(code, k_alias[i].code, 3); return 1; }
    return 0;
}

int voice_lang_other_hint(const char *text, char *out, size_t cap)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    char code[3];
    const char *t = text ? text : "";
    while (*t == ' ' || *t == '\t') t++;
    if (*t == '\0') {
        (void)snprintf(out, cap, "%s",
            "Type a two-letter ISO 639-1 language code such as cy for Welsh, "
            "or the language's English name such as Welsh.");
        return 0;
    }
    if (voice_lang_parse_other(t, code, sizeof code)) {
        (void)snprintf(out, cap, "%s (%s)", voice_lang_name(code), code);
        return 0;
    }
    (void)snprintf(out, cap, "%s",
        "Not recognised. Use a two-letter language code such as cy, or the "
        "language's English name such as Welsh. It will not be saved.");
    return 1;
}

void voice_lang_iso(const char *id, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!id || !id[0]) return;
    int idx = voice_lang_find(id);
    if (idx >= 0) {
        (void)snprintf(out, cap, "%s", k_langs[idx].iso);
        return;
    }
    size_t i = 0;
    while (id[i] && id[i] != '-' && id[i] != '_' && i + 1 < cap) {
        out[i] = lower(id[i]);
        i++;
    }
    out[i] = '\0';
}

const char *voice_lang_spelling_hint(const char *id)
{
    if (id && strcmp(id, "en-GB") == 0)
        return "Use British English spelling, for example colour, organise, centre and travelled.";
    return NULL;
}

int voice_lang_combo_index(const char *stored)
{
    if (!stored || !stored[0]) return 0;
    int idx = voice_lang_find(stored);
    return idx >= 0 ? idx : (int)N_LANGS;
}

void voice_lang_save_value(int sel, const char *other_text, const char *prev,
                           char *out, size_t cap)
{
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (sel >= 0 && (size_t)sel < N_LANGS) {
        (void)snprintf(out, cap, "%s", k_langs[sel].id);
        return;
    }
    if (sel == (int)N_LANGS && voice_lang_parse_other(other_text ? other_text : "", out, cap))
        return;
    (void)snprintf(out, cap, "%s", prev ? prev : "");
}
