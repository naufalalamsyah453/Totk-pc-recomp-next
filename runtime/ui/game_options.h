// Entry points of runtime/ui/game_options.c that other runtime files call.
#pragma once

#include <stddef.h>
#include <stdint.h>

// UTF-16 text for a message key of one of the Options screen's injected rows (title "10<id>", value "10<id>_<vv>",
// caption "<id>"), in the menu language. Writes a NUL-terminated string of at most cap units and returns the units
// before the NUL; returns 0 and writes nothing for a key that is not ours.
size_t rt_opt_message_utf16(const char *key, uint16_t *out, size_t cap);

// The native Options' views, driven by menu_cursor.c while the Options screen has the focus (game thread): the home
// (view 0, one row per category) and the pages of settings (views 1..4). ready: the switch may run (TOTK_NATIVE_OPTIONS
// on, 1.4.3). request: the view a category row asked for since the last call, or -1 (consumed). current: the view the
// open screen shows. goto names the view a switch in progress ends on and commit moves the built-for view there and
// marks the next screen creation as the switch's reopen. desc is the screen descriptor a reopen calls the game's
// ensure-open with (0 before the first open); name is a view's text for the switch log.
int rt_opt_ready(void);
int rt_opt_page_request(void);
int rt_opt_page_current(void);
void rt_opt_page_goto(int v);
void rt_opt_page_commit(void);
uint64_t rt_opt_desc(void);
uint64_t rt_opt_closed_gen(void);
uint64_t rt_opt_dead_screen(void);
const char *rt_opt_page_name(int v);
