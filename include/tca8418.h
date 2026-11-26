/*
 * tca8418.h — TI TCA8418 I2C keypad scanner and GPIO expander, MCU-independent.
 * https://github.com/BatteryJuice4/tca8418-driver — MIT licence.
 *
 * Written from the TCA8418 datasheet (TI SCPS215G). The chip has 18 pins, ROW0-7 and
 * COL0-9; any of them can be part of a key matrix (up to 8 x 10 keys, scanned and
 * debounced by the chip, 25-40 ms per press), a general-purpose input (GPI) or an output.
 * Key presses and releases, and GPI changes if you ask for them, go into a 10-event FIFO
 * and pull INT low; the host reads the FIFO over I2C (address 0x34).
 *
 * Pin numbers in this driver: 0-7 = ROW0-7, 8-17 = COL0-9 (TCA8418_ROW(n), TCA8418_COL(n)).
 * Event key numbers are the datasheet's (Tables 1-3): matrix keys 1-80 (row * 10 + col + 1),
 * GPIs 97-104 (ROW0-7) and 105-114 (COL0-9).
 *
 * Datasheet notes this driver follows:
 *   - Registers are written one at a time (auto-increment, CFG.AI, stays off), and the FIFO
 *     is read from KEY_EVENT_A one byte per read (8.6.2.4).
 *   - Three section tables in SCPS215G print wrong addresses (GPIO_PULL2 "0x3D", GPI_EM3
 *     "0x23", DEBOUNCE_DIS2 "0x30"); the register map, Table 9, has 0x2D, 0x22, 0x2A.
 *   - A GPI's debounce is only 50 us (8.6.2.15): fine for logic signals, too short for a
 *     mechanical switch, which belongs in the matrix (or needs debouncing by the host).
 *   - With debounce on, GPIO_DAT_STAT reads the default value until an input changes;
 *     turn debounce off (TCA8418_IN_NODEBOUNCE) for a pin whose level you read directly.
 *   - Each input's internal pull-up is ~105 kΩ: a GPI held low draws ~30 uA through it.
 *
 * No allocation, no platform code: you provide one I2C callback.
 * UNTESTED ON HARDWARE: verified against the datasheet with a host unit test only.
 */
#ifndef TCA8418_H
#define TCA8418_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TCA8418_ADDR 0x34 /* 7-bit, fixed (Table 8) */

/* Registers (Table 9). */
#define TCA8418_REG_CFG           0x01
#define TCA8418_REG_INT_STAT      0x02
#define TCA8418_REG_KEY_LCK_EC    0x03
#define TCA8418_REG_KEY_EVENT_A   0x04
#define TCA8418_REG_KP_LCK_TIMER  0x0E
#define TCA8418_REG_UNLOCK1       0x0F
#define TCA8418_REG_UNLOCK2       0x10
#define TCA8418_REG_GPIO_INT_STAT 0x11 /* 0x11-0x13 */
#define TCA8418_REG_GPIO_DAT_STAT 0x14 /* 0x14-0x16 */
#define TCA8418_REG_GPIO_DAT_OUT  0x17 /* 0x17-0x19 */
#define TCA8418_REG_GPIO_INT_EN   0x1A /* 0x1A-0x1C */
#define TCA8418_REG_KP_GPIO       0x1D /* 0x1D-0x1F */
#define TCA8418_REG_GPI_EM        0x20 /* 0x20-0x22 */
#define TCA8418_REG_GPIO_DIR      0x23 /* 0x23-0x25 */
#define TCA8418_REG_GPIO_INT_LVL  0x26 /* 0x26-0x28 */
#define TCA8418_REG_DEBOUNCE_DIS  0x29 /* 0x29-0x2B */
#define TCA8418_REG_GPIO_PULL     0x2C /* 0x2C-0x2E */

/* CFG bits (8.6.2.1). */
#define TCA8418_CFG_AI           0x80
#define TCA8418_CFG_GPI_E_CFG    0x40 /* GPI events not tracked while the keypad is locked */
#define TCA8418_CFG_OVR_FLOW_M   0x20 /* a full FIFO drops its oldest event, not the new one */
#define TCA8418_CFG_INT_CFG      0x10 /* INT pulses high 50 us when cleared with events pending */
#define TCA8418_CFG_OVR_FLOW_IEN 0x08 /* needs OVR_FLOW_M too (Overflow Errata, 8.6.4) */
#define TCA8418_CFG_K_LCK_IEN    0x04
#define TCA8418_CFG_GPI_IEN      0x02
#define TCA8418_CFG_KE_IEN       0x01

/* INT_STAT bits (8.6.2.2); write 1 to clear. */
#define TCA8418_INT_CAD     0x10
#define TCA8418_INT_OVR     0x08
#define TCA8418_INT_K_LCK   0x04
#define TCA8418_INT_GPI     0x02
#define TCA8418_INT_K       0x01

#define TCA8418_PINS   18
#define TCA8418_ROW(n) (n)
#define TCA8418_COL(n) (8 + (n))
#define TCA8418_FIFO   10

/* Events from the FIFO. */
static inline bool tca8418_ev_pressed(uint8_t ev) { return (ev & 0x80u) != 0; }
static inline uint8_t tca8418_ev_key(uint8_t ev) { return (uint8_t)(ev & 0x7Fu); }
/* Matrix key number for a row and column (Table 1), and back. */
static inline uint8_t tca8418_key(int row, int col) { return (uint8_t)(row * 10 + col + 1); }
static inline bool tca8418_key_is_matrix(uint8_t key) { return key >= 1 && key <= 80; }
static inline int tca8418_key_row(uint8_t key) { return (key - 1) / 10; }
static inline int tca8418_key_col(uint8_t key) { return (key - 1) % 10; }
/* The pin (0-17) of a GPI event (keys 97-114), or -1. */
static inline int tca8418_key_gpi_pin(uint8_t key) { return key >= 97 && key <= 114 ? key - 97 : -1; }

typedef struct {
    void *ctx;
    /* One I2C transaction to 7-bit `addr`: write tx[0..ntx), then, if nrx > 0, a repeated
     * START and read rx[0..nrx), then STOP. Return 0 on success, negative on a NACK. */
    int (*xfer)(void *ctx, uint8_t addr, const uint8_t *tx, size_t ntx, uint8_t *rx, size_t nrx);
} tca8418_bus_t;

typedef struct {
    const tca8418_bus_t *bus;
    uint8_t addr; /* TCA8418_ADDR */
} tca8418_t;

enum {
    TCA8418_OK = 0,
    TCA8418_E_BUS = -1, /* the bus callback failed: no chip, or a NACK */
    TCA8418_E_ARG = -2,
};

int tca8418_read(tca8418_t *d, uint8_t reg, uint8_t *v);
int tca8418_write(tca8418_t *d, uint8_t reg, uint8_t v);

/* Puts every configuration register back to its reset value (all pins inputs with
 * pull-ups, no keypad), empties the FIFO, clears the interrupts, then writes `cfg`
 * (TCA8418_CFG_*; AI is refused). Also the probe: TCA8418_E_BUS if nothing answers. */
int tca8418_init(tca8418_t *d, uint8_t cfg);

/* Makes ROWs in `rows` (bit n = ROWn) and COLs in `cols` (bit n = COLn) the key matrix. */
int tca8418_keypad(tca8418_t *d, uint8_t rows, uint16_t cols);

/* Input options for tca8418_gpio_input. */
#define TCA8418_IN_NOPULL     0x01 /* internal pull-up off */
#define TCA8418_IN_EVENT      0x02 /* changes go into the event FIFO (keys 97-114) */
#define TCA8418_IN_IRQ        0x04 /* changes set INT_STAT.GPI (INT low with CFG.GPI_IEN) */
#define TCA8418_IN_HIGH       0x08 /* active high: rising edge / high level (else falling) */
#define TCA8418_IN_NODEBOUNCE 0x10 /* read the level directly (see the notes above) */

/* Configures `pin` (0-17) as an input; it leaves the matrix if it was in it. */
int tca8418_gpio_input(tca8418_t *d, int pin, unsigned flags);
/* Configures `pin` as an output driving `high`: the level is set before the direction,
 * so the pin never glitches the other way. */
int tca8418_gpio_output(tca8418_t *d, int pin, bool high);
/* Sets an output's level. */
int tca8418_gpio_write(tca8418_t *d, int pin, bool high);
/* Reads a pin's level (GPIO_DAT_STAT). */
int tca8418_gpio_read(tca8418_t *d, int pin, bool *high);
/* All 18 levels, bit n = pin n. */
int tca8418_gpio_read_all(tca8418_t *d, uint32_t *levels);

/* Empties the FIFO into ev[0..max) in order (oldest first) and clears the key and
 * overflow interrupts. Returns the number of events (events beyond `max` stay queued
 * and keep INT low), or a negative error. *overflow, if not NULL, says the FIFO overflowed
 * (with CFG.OVR_FLOW_M: events were lost). */
int tca8418_events(tca8418_t *d, uint8_t *ev, int max, bool *overflow);

/* The GPIs that raised an interrupt (bit n = pin n), from GPIO_INT_STAT, and clears
 * INT_STAT.GPI. */
int tca8418_gpi_changes(tca8418_t *d, uint32_t *pins);

#ifdef __cplusplus
}
#endif

#endif /* TCA8418_H */
