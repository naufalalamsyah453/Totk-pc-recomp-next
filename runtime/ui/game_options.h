// Entry points of runtime/ui/game_options.c that other runtime files call.
#pragma once

#include <stddef.h>
#include <stdint.h>

// UTF-16 text for a message key of one of the Options screen's injected rows (title "10<id>", value "10<id>_<vv>",
// caption "<id>"), in the menu language. Writes a NUL-terminated string of at most cap units and returns the units
// before the NUL; returns 0 and writes nothing for a key that is not ours.
size_t rt_opt_message_utf16(const char *key, uint16_t *out, size_t cap);
