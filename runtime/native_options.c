// Hooks the lifted Options screen calls into the runtime (recomp/patches-v143.txt `site opts ... @host` /
// `@hostif`, mechanism in recomp/patches.py and recomp/gen.py; the plan is docs/native-options-plan.md, Phase 2).
// Every site is the entry instruction of its guest function, so the state is pristine when a hook runs.
//
// A @host hook (the void ones) runs `rt_opt_*(c)` before the original instruction, which then runs as usual: it
// may only add side effects and must leave c->nzcv, c->sp and x19-x29 alone (the generator spills and reloads the
// register locals around the call, but flags and the guest's callee-saved registers are not a hook's to change).
// rt_opt_decide additionally may rewrite c->x[1] to a sentinel the entry lookup cannot match, which turns the
// dispatcher into a no-op for a row the runtime owns.
//
// A @hostif hook (the int ones) runs first and decides who does the guest call: return 0 and the original
// instruction runs; return non-zero and the hook has taken the call over - it performed the whole operation,
// left the value the caller expects in c->x[0], and left c->x[30], c->sp, c->nzcv and x19-x29 untouched - and
// the generator returns from the lifted function.
//
// Phase 2 ships the call sites with these fail-soft stubs (no hook ever takes a call over, no hook has side
// effects); Phase 3 fills them in with the native Options rows.
#include "cpu.h"

void rt_opt_screen_open(Cpu *c) {
    (void)c;
}

void rt_opt_build_rows(Cpu *c) {
    (void)c;
}

void rt_opt_tick(Cpu *c) {
    (void)c;
}

void rt_opt_decide(Cpu *c) {
    (void)c;
}

int rt_opt_read(Cpu *c) {
    (void)c;
    return 0;
}

int rt_opt_store(Cpu *c) {
    (void)c;
    return 0;
}

int rt_opt_msg_lookup(Cpu *c) {
    (void)c;
    return 0;
}
