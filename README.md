# tca8418-driver

A small, MCU-independent C driver for the **Texas Instruments TCA8418**, an I²C keypad
scanner and GPIO expander: up to an 8 × 10 key matrix, scanned and debounced by the chip,
with any leftover pins as inputs or outputs. Written from the TCA8418 datasheet (SCPS215G).

- **No platform code.** You give it one I²C callback; the same code runs on a PC (the unit
  test) and any microcontroller.
- **Keys:** set the matrix rows and columns, then empty the 10-event FIFO when INT goes
  low. Events use the datasheet's key numbers, with helpers for row/column.
- **GPIOs:** inputs (pull-up, edge, debounce, interrupt, or into the FIFO) and outputs
  that never glitch the wrong way while being set up (level before direction).
- **Clean start:** `tca8418_init` puts every configuration register back to its reset
  value and empties the FIFO, so a warm restart of your MCU starts from a known state.
- No allocation, C11, MIT licence.

## Status

> ⚠️ **Untested on real hardware.** The author does not have the chip yet. The driver is
> checked against a fake chip written from the datasheet (and the test was itself
> checked by breaking the driver on purpose in 16 ways).

**Not included:** the keypad lock/unlock sequence and the CTRL-ALT-DEL detection (see the
datasheet's CAD errata before using it). Both are register writes you can do with
`tca8418_write`.

## Using it

```c
#include "tca8418.h"

static const tca8418_bus_t bus = {NULL, my_i2c_xfer};
static tca8418_t kp = {.bus = &bus, .addr = TCA8418_ADDR};

tca8418_init(&kp, TCA8418_CFG_KE_IEN);    /* INT low on key events */
tca8418_keypad(&kp, 0x07, 0x007);         /* ROW0-2 x COL0-2: 9 keys */
tca8418_gpio_output(&kp, TCA8418_COL(9), true);

/* when INT goes low: */
uint8_t ev[TCA8418_FIFO];
int n = tca8418_events(&kp, ev, TCA8418_FIFO, NULL);
for (int i = 0; i < n; i++) {
    uint8_t key = tca8418_ev_key(ev[i]);   /* 1-80 matrix, 97-114 GPI */
    bool down = tca8418_ev_pressed(ev[i]);
}
```

Pins are numbered 0–17: `TCA8418_ROW(0..7)` and `TCA8418_COL(0..9)`. The `xfer` callback
writes, then (for reads) does a repeated START and reads. `examples/pico_example.c` is a
complete Raspberry Pi Pico program.

### Notes from the datasheet

- Matrix keys are debounced by scanning (25–40 ms to see a press). A GPI's debounce is
  only 50 µs: right for logic signals, too short for a mechanical button.
- With debounce on, `GPIO_DAT_STAT` shows the default value until an input changes; use
  `TCA8418_IN_NODEBOUNCE` for a pin whose level you read directly (a status line).
- The internal pull-ups are about 105 kΩ, so an input held low draws ~30 µA. Turn the
  pull-up off (`TCA8418_IN_NOPULL`) on inputs that sit low for long, if the source drives
  them.
- Three section tables in SCPS215G print wrong register addresses (GPIO_PULL2 "0x3D",
  GPI_EM3 "0x23", DEBOUNCE_DIS2 "0x30"). This driver uses the register map (Table 9):
  0x2D, 0x22, 0x2A.
- The FIFO's overflow interrupt only works with `OVR_FLOW_M` set as well as
  `OVR_FLOW_IEN` (Overflow errata).
- How `GPIO_INT_STAT` clears is not stated; this driver reads it, and the fake chip in
  the test assumes a read clears it. Check this on a real chip.

## Building the test

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

## Licence

MIT; see `LICENSE`. TI's datasheet is not included; it is available from ti.com.
