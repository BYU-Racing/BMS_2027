# BMS Software — Team Task Breakdown

17 September 2026 · Rugby · Levi

Software architecture and task assignments for the BYU Racing custom BMS master board (STM32G474 + nine LTC6811-1 slave boards over isoSPI), split into three roughly even tracks so the new BMS software team can start immediately.

## Overview

**Pack:** Overall 108S4P (108 cells in series and 4 sets of those in parallel), built from nine 12S4P modules. Each module has its own LTC6811-1 slave IC; all nine are daisy-chained back to the master board's LTC6820 isoSPI transceiver.

**Master board:** STM32G474RET3, plus an SST26VF064B SPI NOR flash (8 MB) for data logging.

**Already working, so the team doesn't have to re-prove any of this:**

- Bare FreeRTOS project builds, flashes, and runs a blinky task
- CAN transmit and receive confirmed on the hardware
- Flashing and debug mode both reliable

## Before Writing Main Code Blocks

**1. Repo structure.** Suggested layout on top of the existing skeleton:

```
/Core          <- STM32CubeIDE generated init code
/Drivers       <- HAL + custom drivers (LTC6811, SPI flash, CAN)
/App
  /tasks       <- one file per FreeRTOS task
  /bms_logic   <- balancing algorithm, fault logic, state machine
  /can         <- message packing/unpacking
/Interfaces    <- shared header files defining structs/enums used by more than one task
```

**2. Shared interface headers.** Define these before anyone writes real logic, so all three tasks can compile against them immediately:

- A cell-data struct: per-cell voltage, per-cell temperature, per-cell/per-IC status flags
- A fault/state enum covering every state the team wants (see Task 2 below)

Even a rough first draft is enough — it can change as tasks 1 through 3 get built out.

**3. One repo rule worth setting now:** route CubeMX-regenerated peripheral config changes (clock config, pin mux, `.ioc` file) through one person (Levi).

**4. Pin/peripheral reference file.** Add a small `HW_peripherals.h` (or similar) that just lists each pin name and what it's wired to / which variable it maps to in code, so the team isn't pinging Rugby every time they need to know which SPI instance drives the isoSPI chain. No naming conventions needed here, just a lookup table.

**Repo:** [BMS\_2027 (hardware\_test branch)](https://github.com/BYU-Racing/BMS_2027/tree/hardware_test)

## Task 1: Slave Board Communication (Levi + 1 other)

**What this task owns:** talking to the nine daisy-chained LTC6811-1 ICs over isoSPI, and writing the resulting data (voltages, temperatures) into the shared cell-data struct.

**Why this is a good first FreeRTOS task:** it's one periodic task that wakes up, does a fixed sequence of SPI commands, and updates shared data — a clean, self-contained example of the acquire-and-publish pattern used everywhere in embedded systems.

**What to research first (new to most people, worth reading before writing code):**

- FreeRTOS tasks, and specifically periodic tasks using `vTaskDelayUntil` (keeps a fixed polling rate even as the task's own work time varies)
- FreeRTOS mutexes or queues — used here to protect the shared cell-data struct from being read and written at the same time
- SPI basics, and specifically isoSPI (an isolated variant used for the daisy chain link between boards)
- The LTC6811-1's command set: starting an ADC conversion (ADCV), polling for completion, reading back cell voltage registers, and the PEC (packet error code) that every command needs

**Suggested build order:**

1. Get a single LTC6811-1 responding to a basic command (e.g. read configuration register) over isoSPI
2. Extend to the full 9-IC daisy chain
3. Build the periodic task: start conversion, poll, read all cells and temps, write into the shared struct
4. Once acquisition is solid, publish cell data into the shared struct at a stable rate — that's the handoff point for Task 4 (flash logging), which is now a separate task below

**Confirm before finishing:** exact polling rate, and whether logging should run continuously or only in specific states (see Task 2's state machine) — Rugby's call, and can be decided once Task 2's states exist.

## Task 2: CAN Messaging and State Machine (Levi)

**What this task owns:** packing cell data into outgoing CAN messages, a periodic transmit task, and the top-level BMS state machine.

**States to implement (confirmed by Rugby):** Init → Precharge → Drive/Normal → Charging → Fault Detected → Shutdown. Other tasks (balancing, logging) read this state to decide what they're allowed to do.

**🔍 Not yet determined — talk to Levi and the telemetry team before finalizing:**

- CAN bus baud rate
- Message IDs and byte layout for each signal
- Required refresh rate (how often the rest of the car / the logging rig expects updates)

Until that conversation happens, this task can develop against a made-up baud rate and message layout — the packing logic and the transmit task's structure won't need to change much once the real numbers come in, just the constants.

**What to research first:**

- FreeRTOS queues, for passing data from other tasks into the CAN transmit task cleanly
- Basic CAN frame structure (ID, DLC, 8 data bytes) and bit-packing signals into those bytes (scale + offset, similar to how a DBC file describes it)
- State machine implementation patterns in embedded C (a simple `switch` on an enum, with one function per state, is usually enough — no need for anything fancier)

**Suggested build order:**

1. Define a first-draft message layout (even with guessed IDs/baud rate) and write the packing/unpacking functions
2. Build the periodic CAN transmit task, sending dummy data — this can start immediately since CAN hardware is already proven
3. Build the state machine skeleton with the six states above, with clear entry/exit conditions for each
4. Swap in real message layout details once Levi/telemetry confirms them

## Task 3: Balancing and Fault Detection (Joonhee)

**What this task owns:** deciding which cells to bleed through the 33 Ω balancing resistors, and detecting overvoltage, undervoltage, overtemperature, and lost communication with a slave board — feeding the result into Task 2's state machine.

This is the most rules-driven of the three tasks. Below, plain text is a real, confirmed FSAE rule; anything marked **🔍 AI-researched** is what Claude found or suggested and still needs verification against the actual current rulebook and datasheets before it's treated as final.

**Confirmed FSAE rules to design around:**

- Maximum cell temperature is 60°C, per the accumulator/AMS temperature rule ([reference: senior design report citing the FSAE limit](https://seniordesign.me.wisc.edu/2023/03/07/formula-e)) — build in a margin below this rather than triggering exactly at the limit
- The car must not draw more than 80 kW, or exceed the specified voltage, for more than 100 ms continuously (EV2.2.4 in the 2017–18 rules; rule numbering has likely shifted in the [current FSAE rulebook](https://www.fsaeonline.com) — confirm the current rule number before finalizing)

**🔍 AI-researched, needs verification — cell voltage limits:** Rugby's placeholder numbers (4.15 V max, 2.7 V min) were arbitrary starting points, not derived from the RS50 cell datasheet. Before finalizing, this task should pull the RS50 cell's actual charge/discharge voltage curve and safety margins from its datasheet, and check what similar FSAE teams use for margin below the cell's absolute 4.2 V / \~2.5 V limits.

**🔍 AI-researched, needs verification — thermal thresholds:** 60°C is the hard rule (above). A commonly seen pattern on other teams' public writeups is to start reducing power output around 55°C as an early warning before the hard cutoff, with a separate lower "warning only" threshold — but the specific numbers should be Task 3's own research + a conversation with Rugby, not copied from this doc.

**🔍 AI-researched, needs verification — fault debounce timing:** Rugby's understanding is that the 80 kW/100 ms rule (above) is the binding constraint, but the exact debounce time for latching a fault (how long a bad reading must persist before the BMS reacts) should come from reading the current FSAE rulebook's AMS/shutdown section directly and then confirming with Rugby — do not use a number from this document without that step. (For reference only, Formula Student Germany's own rules — a related but separate rule set — specify 500 ms persistence for voltage/current faults and 1 s for temperature faults; FSAE's own numbers may differ and should be looked up directly.)

**Confirm with Rugby before implementing balancing logic:** balancing should likely only be enabled while the shutdown circuit is fully closed (all interlocks/ILKs satisfied) — this needs to be verified against the specific rule text in the current FSAE rulebook covering AMS/BMS behavior during balancing, since the exact rule number wasn't confirmed during this research pass.

**What to research first:**

- The relevant sections of the current FSAE rules (Energy Storage / Accumulator, and the AMS-related shutdown system rules)
- Passive cell balancing strategies (bleeding the highest cells down toward the pack minimum is the common approach)
- Debounce / hysteresis patterns for fault detection in embedded systems, so a single noisy ADC reading doesn't trip a false fault

## Task 4 (Extra): SPI Flash Data Logging

Split off from Task 1 so it can go to whoever finishes their main task early, or to a fourth person if one joins the team. This task reads from the same shared cell-data struct Task 1 populates — it doesn't touch the isoSPI/LTC6811 side at all.

**What this task owns:** logging cell data to the SST26VF064B SPI NOR flash (8 MB) during drive events, and supporting reading that data back off afterward.

**Suggested build order:**

1. Get basic read/write/erase working against the SPI NOR flash over SPI
2. Write rows from the shared cell-data struct at 1–10 Hz (CSV is a reasonable default format, but this task owns picking the actual format)
3. Gate logging to run only during the drive state, once Task 2's state machine exists
4. Add a way to read the logged data back off the flash and clear it between drive events

**What to research first:**

- Basic SPI flash operations: page read/write, sector/block erase, and why erase has to happen before rewriting a page
- FreeRTOS queues or a mutex, for safely receiving data from Task 1's struct without racing it

## Open Items to Confirm with Rugby

Don't let these block starting the tasks — start against reasonable placeholders and update once answered.

- CAN baud rate and message IDs (Task 2 — talk to Levi and the telemetry team)
- Required CAN data refresh rate (Task 2 — telemetry team)
- Final cell voltage limits, based on the RS50 datasheet (Task 3)
- Final thermal warning/cutoff thresholds below the 60°C hard limit (Task 3)
- Fault debounce timing, based on the current FSAE rulebook (Task 3, with Rugby)
- Whether balancing is gated on the shutdown circuit being fully closed, and the exact rule reference (Task 3, with Rugby)
- Logging data format and which states trigger logging (Task 1, once Task 2's states exist)

## Resources

**FreeRTOS fundamentals**

- [FreeRTOS on STM32 tutorial series](https://controllerstech.com/stm32-hal/freertos-tutorials) — tasks, queues, semaphores, mutexes, with CubeIDE walkthroughs
- [STM32World FreeRTOS overview](https://autoconfig.stm32world.com/wiki/FreeRTOS) — concise conceptual intro

**Task 1 — isoSPI / LTC6811-1**

- [LTC6811-1 product page (Analog Devices)](https://www.analog.com/en/products/ltc6811-1.html) — datasheet, command set reference
- [Linear Technology's official Linduino LTC6811 driver source](https://os.mbed.com/users/roger5641/code/LTC6811/docs/tip/LTC6811_8h_source.html) — reference implementation of the command set, useful for understanding the register/command structure even though it targets a different MCU
- [UW Midsun's LTC6811-1 isoSPI notes](https://uwmidsun.atlassian.net/wiki/x/zIDHVQ) — another FSAE team's practical notes on bring-up and self-test commands

**Task 2 — CAN and DBC format**

- [DBC file format explained, with examples](https://lenord.me/posts/canbus_dbc_format/) — useful even if the team isn't using a formal DBC file yet, since it's the standard way to think about message/signal layout
- [cantools (Python)](https://github.com/cantools/cantools) — can generate/parse DBC files if the team wants tooling later

**Task 3 — Rules and balancing**

- [FSAE Online — official rules and resources](https://www.fsaeonline.com) — always confirm against the current year's rulebook here, not older PDFs found elsewhere
- Cell datasheet for the RS50 cells (pull from the manufacturer directly for voltage/thermal limits)

🔍 Note: the FreeRTOS, LTC6811, and DBC resources above are general references Claude found through web search — solid starting points, but not FSAE- or BYU-Racing-specific. The FSAE rulebook link is the one source in this list that should be treated as authoritative.

## Appendix

#### BMS rules 

[Formula SAE Rules 2027 *DRAFT* - BMS](https://www.fsaeonline.com/cdsweb/gen/DownloadDocument.aspx?DocumentID=9574fa97-c13e-4b90-9c75-044e30348cab) (pg. 109)

#### Syntax Guide

- STM32CubeMX will override code not included in user code comment brackets when changes are made to the hardware/firmware configuration. Do avoid code being overwritten when the hardware configuration file is updated (*BMS.ioc*), *Please*, only include code in vendor generated files as shown below: 

```
// User Code Begin --- // 

/* place your code here */

// User Code End --- // 
```

- Generally, most developers should not be editing the *main.c* file as it mostly initiates the hardware abstraction level and starts the freeRTOS scheduler. Each task is contained in a separate file and called within the *main.c* file. Any changes to the setup should be verified through the software architect first.

-  The suggested workflow for is to first clone the main/master branch and then...

