# Control State Machine — Process Notes

Why this file exists: I'm new to the club and to C, and I'm building the BMS control
state machine from scratch on the `state-machine` branch. This is a running log of
what I did, why, and what I learned along the way — so anyone picking this up later
(including future me) can follow the reasoning, not just read the final code.

Scope: I own the control state machine only. Balancing logic and the sensor/CAN
drivers are owned by others and are out of scope here — this file only covers the
FSM itself.

## Progress log

### Branch setup and cleanup

- Created the `state-machine` branch off `main`.
- Found `control.h` / `control.cpp` / `constants.h` already existed, left over from
  last year's C++ codebase (Sentinel_BMS) — the lead had carried them over to give
  new members a starting point, but they were never finished and don't reflect the
  actual design we landed on (wrong state names, a couple of real bugs, C++ instead
  of C). Deleted them rather than build on top of them.
- Decided to write the new FSM in **C, not C++**: the CMake project only enables
  `C ASM` (`enable_language(C ASM)` in `CMakeLists.txt`), not `CXX`. It also turns
  out nothing under `Core/Src/application/` was actually being compiled at all —
  `target_sources()` in the top-level `CMakeLists.txt` was empty. So the old
  `control.cpp` never even reached the compiler; that's a second, independent
  reason its bugs never surfaced.
- Created empty `Core/Inc/include/control.h` and `Core/Src/application/control.c`,
  and added the `.c` file to `target_sources()` in `CMakeLists.txt` so it's part of
  the build going forward.
- Could not run a real cross-compiled build on this machine (no `arm-none-eabi-gcc`
  installed) — still need to confirm it builds in STM32CubeIDE.

### States enum — done

Added the `BmsState` enum to `control.h`: `BMS_STATE_IDLE`, `BMS_STATE_READY`,
`BMS_STATE_RUNNING`, `BMS_STATE_CHARGING`, `BMS_STATE_FAULT`.

### Inputs struct — done

Added `BmsInputs` to `control.h`: all nine input flags, grouped with comments by
which state actually uses them (Idle's connection checks, Ready's charging check,
the GOOD checks shared by Ready/Running/Charging, and Fault's reset check).

### Outputs struct — done

Added `BmsOutputs` to `control.h`: `state`, `shutdownCircuitClosed`, `canBalance`.
Dropped a fourth `inFault` field from the original plan — it would have duplicated
`state` (anything that needs to know "is it Fault" can just check
`state == BMS_STATE_FAULT`), and a duplicate is just a second place for the two to
drift out of sync.

### Context struct — done

Added `BmsContext` to `control.h`: `state` (`BmsState`) and `idleRetryTicks`
(`uint32_t`, counts down once per 10ms tick while Idle waits to retry a
connection check).

### Step function signature — done

`Control_Step(BmsContext *ctx, const BmsInputs *inputs, BmsOutputs *outputs)` is
declared in `control.h` and defined (still empty) in `control.c`. Caught a real
mistake here: I'd first written the full body inside the header, which would
cause a "multiply defined" linker error once more than one `.c` file includes
`control.h`. A header only gets the declaration (ends in `;`); the body lives in
exactly one `.c` file.

### Switch skeleton — done, first compile check passed

`control.c` now has the full `switch (ctx->state)` shape with all five empty
cases. Host-compiled it directly (`cc -c -std=c11 -Wall -Wextra -Wpedantic`,
not the ARM toolchain — just a syntax/shape check) and it compiles clean, aside
from two expected `unused parameter` warnings for `inputs`/`outputs`, which will
go away as each case gets filled in.

Also note to self: caught a save issue here — I'd told Claude I'd added code
twice when it actually hadn't saved to disk yet. Worth double-checking the file
on disk (or just re-reading it) matches what's in the editor before saying a
step is done.

### Idle's logic — done

`BMS_STATE_IDLE` now has the real transition rules: Ready if everything's
connected, Fault if isoSPI/CAN are up but the sensor chain isn't, otherwise stay
Idle and run the 100ms retry countdown. Compiles clean, no warnings.

### Ready's logic — done, and a real bug caught along the way

First draft set `outputs` inline at the bottom of each case (same as Idle). That
had a genuine bug, not just a style issue: if voltages/temps went bad while in
Ready, `ctx->state` correctly became Fault, but the outputs lines still ran with
Ready's values — `shutdownCircuitClosed = true`, `canBalance = true` — meaning
the safety-relevant outputs would say "still fine" one cycle after the machine
actually decided it wasn't. Fixed by splitting into two switches on
`ctx->state`: the first decides the next state, the second (running after,
reading the *new* `ctx->state`) sets outputs. That way outputs always describe
where the machine actually ended up, not which case happened to run.

Lesson: "compiles clean" only proves the syntax is valid, not that the logic is
right. Need to actually trace through what happens on each branch, especially
the ones that change state.

### Running, Charging, Fault — done. All five states implemented.

Chose to write Running and Charging's checks separately rather than factor out
a shared helper — reasonable call for two small, simple cases; would reconsider
if a third near-identical case showed up.

Caught my own leftover dead code (a stray `break;` outside any case, from
copy-pasting) before it shipped — worth double-checking for that kind of
copy-paste leftover any time a case is built by copying another one.

Fault only clears on `resetRequested`, matching the latching rule. Compiles
clean with all five states in both switches (`cc -std=c11 -Wall -Wextra
-Wpedantic`).

Noticed `canBalance` and `shutdownCircuitClosed` end up identical in every
state (both true for Ready/Running/Charging, both false for Idle/Fault) — makes
sense, since the rule (FSAE EV.7.3.3) is "balancing must stop when shutdown is
open," and that's the minimum this implements. Open team question: should
balancing also be restricted to Ready/Charging only (not while actively
Running), as a policy choice on top of the safety minimum?

Open questions still to raise with the lead: is Fault really the only way out
of Running/Charging (no return to Ready)? Should `idleRetryTicks` reset on a
Fault→Idle transition?

### First test — passing

`tests/test_control.c`: one test so far ("Idle stays Idle when nothing is
connected"), compiled and run with plain `cc` (no ARM toolchain, no CMake,
never touches the embedded build). Passing.

Also installed `cmake` and `ninja` via Homebrew — neither was on this machine
at all, which is what the VS Code CMake Tools popup was complaining about.
Still don't have the ARM cross-compiler (`arm-none-eabi-gcc`), so a real
embedded build still isn't possible here — only these host-side checks.

### Second test — passing, after an ordering bug

Refactored into one function per test (`static`, `(void)` params) plus `main`
calling both. Took two tries to get right:
- First attempt nested both test functions *inside* `main`'s braces — not
  legal in C at all, functions can't be defined inside other functions.
- Second attempt un-nested them but put `main` first in the file, calling
  functions defined further down. C compiles top to bottom in one pass — by
  the time it reaches a call, the thing being called has to already be defined
  above it in the file. Fixed by moving both test functions above `main`.

Both tests pass now (`test_idle_stays_idle_when_nothing_connected`,
`test_idle_moves_to_ready_when_everything_connects`).

### Idle fully covered

Wrote five more tests independently (past what was asked): modules-missing,
voltage-missing, and temp-missing tested individually, plus two combinations.
Checks each sensor condition can independently trigger Fault, not just all of
them together — solid coverage of that branch. All seven tests pass.
(Couple of typos in test names to clean up later — `senosrs` — doesn't affect
correctness.)

### Retry-timer single-tick test — done

`test_idle_retry_timer_counts_down_one_tick`: starts `idleRetryTicks` at 10,
calls once with nothing connected, asserts it's 9. Claude wrote this one at my
request rather than me typing it. All 8 tests pass.

isoSPI-vs-CAN split test — done, written independently:
`test_idle_stays_idle_when_isoSpi_not_connected` and
`test_idle_stays_idle_when_can_not_connected`, each with only one of the two
false. 10 tests total now, all passing. Idle is thoroughly covered.

Retry-timer wraparound — done, and extended independently again: wrote the
base wraparound test (loop 10 calls, assert 0, one more call, assert back to
10), plus two more variants checking the same wraparound happens whether it's
isoSPI or CAN specifically that's missing. 13 tests total, all passing. Idle's
case is now about as thoroughly tested as a single state can be.

Grouped `main`'s calls under a `//IDLE state tests` comment — will need a
matching comment per state as Ready/Running/Charging/Fault tests get added.

### CI added

`.github/workflows/build.yml`: runs on every push to `main` and every PR.
Installs the real `arm-none-eabi-gcc` toolchain + `ninja`, then runs
`cmake --preset Debug` / `cmake --build --preset Debug` — the project's own
existing preset, not a workaround. This is the real embedded build, not the
host-side `cc` checks used locally — catches anything that breaks the actual
firmware, for anyone's code, not just mine.

### Next: Ready, Running, Charging, Fault, and the retry timer

Still need: Ready's other two branches (→ Charging, → Fault), Running/Charging
→ Fault, Fault → Idle on reset, and the retry-timer countdown (does it
actually decrement, and reset back to 10 after hitting 0).

## C syntax notes

A running glossary of C concepts, added as I actually use them — not a general C
tutorial, just the pieces this file needed.

- **`typedef enum { ... } Name;`** — defines a fixed set of named values as a type,
  and gives that type a usable name (`Name`) in one line. In C, `enum { ... };`
  alone doesn't give you a short type name to use elsewhere — the `typedef` is
  what makes `BmsState x;` work instead of `enum BmsState x;`.
- **`typedef struct { ... } Name;`** — same idea, but groups several *different*
  fields into one type instead of listing alternative values. `#include <stdbool.h>`
  is needed for `bool`/`true`/`false` in C — they aren't built into the language
  like they are in C++.
