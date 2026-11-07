// Entry points of runtime/ui/game_options.c that other runtime files call.
#pragma once

#include <stddef.h>
#include <stdint.h>

// UTF-16 text for a message key of one of the Options screen's injected rows (title "10<id>", value "10<id>_<vv>",
// caption "<id>"), in the menu language. Writes a NUL-terminated string of at most cap units and returns the units
// before the NUL; returns 0 and writes nothing for a key that is not ours.
size_t rt_opt_message_utf16(const char *key, uint16_t *out, size_t cap);

// The page switch behind L and R, driven by menu_cursor.c while the Options screen has the focus (game thread).
// ready: the tabs may run (TOTK_NATIVE_OPTIONS on, 1.4.3). arm takes the target page from dir (-1/+1, wrapped)
// and returns it; commit moves the built-for page there and marks the next screen creation as a switch's reopen.
// desc is the screen descriptor a reopen calls the game's ensure-open with (0 before the first open); closed-gen
// is the teardown counter a reopen waits on (ensure-open early-outs while the dying screen still holds the
// manager's slot); name is the page's tab text for the switch log.
int rt_opt_ready(void);
int rt_opt_page_arm(int dir);
void rt_opt_page_commit(void);
uint64_t rt_opt_desc(void);
uint64_t rt_opt_closed_gen(void);
uint64_t rt_opt_dead_screen(void);
const char *rt_opt_page_name(int p);
