// Host check for runtime/ui/opt_msgkey.h: which Options-screen message keys belong to our injected rows.
#include <stdio.h>

#include "ui/opt_msgkey.h"

#define BASE 0x8000u
#define LIMIT (0x8000u + 32u)

static int fails;

static void expect(const char *key, OptMsgKind kind, uint32_t id, uint32_t value) {
    OptMsgKey k;
    int r = opt_msgkey_parse(key, BASE, LIMIT, &k);
    int ok = (r == (kind != OPT_MSG_NONE)) && k.kind == kind && (kind == OPT_MSG_NONE || k.id == id) &&
             (kind != OPT_MSG_VALUE || k.value == value);
    if (!ok) {
        printf("FAIL %-14s -> r=%d kind=%d id=%u value=%u (want kind=%d id=%u value=%u)\n", key ? key : "(null)", r,
               (int)k.kind, k.id, k.value, (int)kind, id, value);
        fails++;
    }
}

int main(void) {
    // Ours: ids 0x8000 = 32768 and up.
    expect("1032768", OPT_MSG_TITLE, 32768, 0);
    expect("1032799", OPT_MSG_TITLE, 32799, 0);
    expect("1032768_00", OPT_MSG_VALUE, 32768, 0);
    expect("1032770_02", OPT_MSG_VALUE, 32770, 2);
    expect("32768", OPT_MSG_CAPTION, 32768, 0);
    expect("32775", OPT_MSG_CAPTION, 32775, 0);

    // The game's own keys stay the game's: small ids, the kind-2 scheme, ids past our range.
    expect("1001", OPT_MSG_NONE, 0, 0);
    expect("1001_02", OPT_MSG_NONE, 0, 0);
    expect("1000_03", OPT_MSG_NONE, 0, 0);
    expect("0007", OPT_MSG_NONE, 0, 0);
    expect("2001", OPT_MSG_NONE, 0, 0);
    expect("1032800", OPT_MSG_NONE, 0, 0);   // one past the limit
    expect("1032767", OPT_MSG_NONE, 0, 0);   // one below the base
    expect("32800", OPT_MSG_NONE, 0, 0);
    expect("32767", OPT_MSG_NONE, 0, 0);

    // Malformed: never ours, never a crash.
    expect("", OPT_MSG_NONE, 0, 0);
    expect(NULL, OPT_MSG_NONE, 0, 0);
    expect("10", OPT_MSG_NONE, 0, 0);
    expect("abc", OPT_MSG_NONE, 0, 0);
    expect("1032768_", OPT_MSG_NONE, 0, 0);
    expect("1032768_x", OPT_MSG_NONE, 0, 0);
    expect("1032768_01_02", OPT_MSG_NONE, 0, 0);
    expect("1032768x", OPT_MSG_NONE, 0, 0);
    expect("32768_00", OPT_MSG_NONE, 0, 0);
    expect("99999999999999999999", OPT_MSG_NONE, 0, 0);   // would overflow the digit run

    if (fails) {
        printf("opt_msgkey_check: %d failure(s)\n", fails);
        return 1;
    }
    puts("opt_msgkey_check: ok");
    return 0;
}
