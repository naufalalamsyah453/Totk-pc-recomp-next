// Classifies the message keys TotK's Options screen asks for (runtime/ui/game_options.c, plan Phase 3.2).
//
// The screen builds an ASCII key per piece of text from the option id (entry+0x0) and looks it up in the sheet
// LayoutMsg/SystemOption_00. The formats are the ones the lifted row code feeds to sprintf (docs/native-options-plan.md,
// "Message scheme"):
//
//   "10%02d"      row title    (suffix T_Option_00)
//   "10%02d_%02d" value text   (suffix T_Text_00): option id, then value id
//   "%04d"        focused caption (suffix T_Caption_00)
//
// Our rows use option ids in [id_base, id_limit), a range the game does not use, so a key that parses into that range
// is ours and anything else belongs to the game's own sheet. The parser is pure (no guest memory, no cfg) so a host test
// can exercise it without game files.
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    OPT_MSG_NONE = 0,   // not one of our keys: leave it to the game
    OPT_MSG_TITLE,
    OPT_MSG_VALUE,
    OPT_MSG_CAPTION,
} OptMsgKind;

typedef struct {
    OptMsgKind kind;
    uint32_t id;      // option id, in [id_base, id_limit)
    uint32_t value;   // value id (OPT_MSG_VALUE only)
} OptMsgKey;

// Reads a run of decimal digits at *p; returns 0 when there is none or the value would not fit in 31 bits.
static inline int opt_msgkey_digits(const char **p, uint32_t *out) {
    const char *s = *p;
    uint64_t v = 0;
    int n = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (uint64_t)(*s - '0');
        if (v > 0x7fffffffull) return 0;
        s++;
        n++;
    }
    if (!n) return 0;
    *p = s;
    *out = (uint32_t)v;
    return 1;
}

static inline int opt_msgkey_ours(uint32_t id, uint32_t id_base, uint32_t id_limit) {
    return id >= id_base && id < id_limit;
}

// Parses key (NUL-terminated ASCII). Returns 1 and fills out when it is a title, value or caption key for one of our
// ids; returns 0 (out->kind = OPT_MSG_NONE) otherwise.
//
// A caption key is the bare id, a title key is "10" followed by the id, so the two overlap only when an id itself
// starts with "10". Ours do not (the base is far above 1099), but the order below still tries the title form on the
// remainder first and falls back to the bare id, so a game id of 10xx can never be read as a title of ours: its
// remainder is below id_base.
static inline int opt_msgkey_parse(const char *key, uint32_t id_base, uint32_t id_limit, OptMsgKey *out) {
    out->kind = OPT_MSG_NONE;
    out->id = out->value = 0;
    if (!key || !*key) return 0;

    if (key[0] == '1' && key[1] == '0') {
        const char *p = key + 2;
        uint32_t id, val;
        if (opt_msgkey_digits(&p, &id) && opt_msgkey_ours(id, id_base, id_limit)) {
            if (!*p) {
                out->kind = OPT_MSG_TITLE;
                out->id = id;
                return 1;
            }
            if (*p == '_') {
                p++;
                if (opt_msgkey_digits(&p, &val) && !*p) {
                    out->kind = OPT_MSG_VALUE;
                    out->id = id;
                    out->value = val;
                    return 1;
                }
            }
            return 0;   // an id of ours with a suffix we do not know
        }
    }

    const char *p = key;
    uint32_t id;
    if (opt_msgkey_digits(&p, &id) && !*p && opt_msgkey_ours(id, id_base, id_limit)) {
        out->kind = OPT_MSG_CAPTION;
        out->id = id;
        return 1;
    }
    return 0;
}
