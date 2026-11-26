/*
 * tca8418.c — TI TCA8418 keypad scanner / GPIO expander (see tca8418.h). MIT licence.
 *
 * UNTESTED ON HARDWARE.
 */
#include "tca8418.h"

int tca8418_read(tca8418_t *d, uint8_t reg, uint8_t *v)
{
    return d->bus->xfer(d->bus->ctx, d->addr, &reg, 1, v, 1) < 0 ? TCA8418_E_BUS : TCA8418_OK;
}

int tca8418_write(tca8418_t *d, uint8_t reg, uint8_t v)
{
    uint8_t b[2] = {reg, v};
    return d->bus->xfer(d->bus->ctx, d->addr, b, 2, NULL, 0) < 0 ? TCA8418_E_BUS : TCA8418_OK;
}

/* Pin n's register (of a group of three) and bit: ROW0-7 in the first, COL0-7 in the
 * second, COL8-9 in bits 0-1 of the third. */
static uint8_t pin_reg(uint8_t base, int pin)
{
    return (uint8_t)(base + pin / 8);
}

static uint8_t pin_bit(int pin)
{
    return (uint8_t)(1u << (pin % 8));
}

static int set_bit(tca8418_t *d, uint8_t base, int pin, bool on)
{
    uint8_t reg = pin_reg(base, pin), v;
    int e = tca8418_read(d, reg, &v);
    if (e) {
        return e;
    }
    uint8_t n = on ? (uint8_t)(v | pin_bit(pin)) : (uint8_t)(v & ~pin_bit(pin));
    return n == v ? TCA8418_OK : tca8418_write(d, reg, n);
}

static bool pin_ok(int pin)
{
    return pin >= 0 && pin < TCA8418_PINS;
}

int tca8418_init(tca8418_t *d, uint8_t cfg)
{
    if (cfg & TCA8418_CFG_AI) {
        return TCA8418_E_ARG; /* the driver writes registers one at a time */
    }
    /* CFG first: interrupts off while reconfiguring. */
    int e = tca8418_write(d, TCA8418_REG_CFG, 0);
    /* Back to reset values: GPIO_INT_EN through GPIO_PULL are all 0 after power-on (Table
     * 9), as are the outputs, the lock and its keys. */
    for (uint8_t r = TCA8418_REG_GPIO_DAT_OUT; !e && r <= TCA8418_REG_GPIO_PULL + 2; r++) {
        e = tca8418_write(d, r, 0);
    }
    for (uint8_t r = TCA8418_REG_KP_LCK_TIMER; !e && r <= TCA8418_REG_UNLOCK2; r++) {
        e = tca8418_write(d, r, 0);
    }
    if (!e) {
        e = tca8418_write(d, TCA8418_REG_KEY_LCK_EC, 0); /* unlocked */
    }
    if (!e) {
        uint8_t ev[TCA8418_FIFO];
        e = tca8418_events(d, ev, TCA8418_FIFO, NULL); /* left over from before */
        e = e < 0 ? e : TCA8418_OK;
    }
    if (!e) {
        uint32_t p;
        e = tca8418_gpi_changes(d, &p);
    }
    if (!e) {
        e = tca8418_write(d, TCA8418_REG_INT_STAT, 0x1F);
    }
    return e ? e : tca8418_write(d, TCA8418_REG_CFG, cfg);
}

int tca8418_keypad(tca8418_t *d, uint8_t rows, uint16_t cols)
{
    if (cols & ~0x3FFu) {
        return TCA8418_E_ARG;
    }
    int e = tca8418_write(d, TCA8418_REG_KP_GPIO, rows);
    if (!e) {
        e = tca8418_write(d, TCA8418_REG_KP_GPIO + 1, (uint8_t)cols);
    }
    if (!e) {
        e = tca8418_write(d, TCA8418_REG_KP_GPIO + 2, (uint8_t)(cols >> 8));
    }
    return e;
}

int tca8418_gpio_input(tca8418_t *d, int pin, unsigned flags)
{
    if (!pin_ok(pin) || (flags & ~0x1Fu)) {
        return TCA8418_E_ARG;
    }
    int e = set_bit(d, TCA8418_REG_KP_GPIO, pin, false);
    if (!e) {
        e = set_bit(d, TCA8418_REG_GPIO_DIR, pin, false);
    }
    if (!e) {
        e = set_bit(d, TCA8418_REG_GPIO_PULL, pin, flags & TCA8418_IN_NOPULL);
    }
    if (!e) {
        e = set_bit(d, TCA8418_REG_DEBOUNCE_DIS, pin, flags & TCA8418_IN_NODEBOUNCE);
    }
    if (!e) {
        e = set_bit(d, TCA8418_REG_GPIO_INT_LVL, pin, flags & TCA8418_IN_HIGH);
    }
    if (!e) {
        e = set_bit(d, TCA8418_REG_GPI_EM, pin, flags & TCA8418_IN_EVENT);
    }
    if (!e) {
        e = set_bit(d, TCA8418_REG_GPIO_INT_EN, pin, flags & TCA8418_IN_IRQ);
    }
    return e;
}

int tca8418_gpio_output(tca8418_t *d, int pin, bool high)
{
    if (!pin_ok(pin)) {
        return TCA8418_E_ARG;
    }
    int e = set_bit(d, TCA8418_REG_KP_GPIO, pin, false);
    if (!e) {
        e = set_bit(d, TCA8418_REG_GPIO_INT_EN, pin, false);
    }
    if (!e) {
        e = set_bit(d, TCA8418_REG_GPI_EM, pin, false);
    }
    if (!e) {
        e = set_bit(d, TCA8418_REG_GPIO_DAT_OUT, pin, high); /* level first */
    }
    if (!e) {
        e = set_bit(d, TCA8418_REG_GPIO_DIR, pin, true);
    }
    return e;
}

int tca8418_gpio_write(tca8418_t *d, int pin, bool high)
{
    return pin_ok(pin) ? set_bit(d, TCA8418_REG_GPIO_DAT_OUT, pin, high) : TCA8418_E_ARG;
}

int tca8418_gpio_read_all(tca8418_t *d, uint32_t *levels)
{
    uint8_t v[3];
    for (int i = 0; i < 3; i++) {
        int e = tca8418_read(d, (uint8_t)(TCA8418_REG_GPIO_DAT_STAT + i), &v[i]);
        if (e) {
            return e;
        }
    }
    *levels = v[0] | ((uint32_t)v[1] << 8) | ((uint32_t)(v[2] & 0x03u) << 16);
    return TCA8418_OK;
}

int tca8418_gpio_read(tca8418_t *d, int pin, bool *high)
{
    if (!pin_ok(pin)) {
        return TCA8418_E_ARG;
    }
    uint8_t v;
    int e = tca8418_read(d, pin_reg(TCA8418_REG_GPIO_DAT_STAT, pin), &v);
    if (!e) {
        *high = (v & pin_bit(pin)) != 0;
    }
    return e;
}

int tca8418_events(tca8418_t *d, uint8_t *ev, int max, bool *overflow)
{
    uint8_t st, ec;
    int e = tca8418_read(d, TCA8418_REG_INT_STAT, &st);
    if (!e) {
        e = tca8418_read(d, TCA8418_REG_KEY_LCK_EC, &ec);
    }
    if (e) {
        return e;
    }
    int count = ec & 0x0F, n = 0;
    while (n < max && n < count) {
        uint8_t v;
        e = tca8418_read(d, TCA8418_REG_KEY_EVENT_A, &v);
        if (e) {
            return e;
        }
        if (v == 0) {
            break; /* empty after all (8.3.1.3 step 4) */
        }
        ev[n++] = v;
    }
    if (overflow) {
        *overflow = (st & TCA8418_INT_OVR) != 0;
    }
    /* Clears K_INT once the FIFO is empty (it stays set while events remain). */
    e = tca8418_write(d, TCA8418_REG_INT_STAT, TCA8418_INT_K | TCA8418_INT_OVR);
    return e ? e : n;
}

int tca8418_gpi_changes(tca8418_t *d, uint32_t *pins)
{
    uint8_t v[3];
    for (int i = 0; i < 3; i++) {
        int e = tca8418_read(d, (uint8_t)(TCA8418_REG_GPIO_INT_STAT + i), &v[i]);
        if (e) {
            return e;
        }
    }
    *pins = v[0] | ((uint32_t)v[1] << 8) | ((uint32_t)(v[2] & 0x03u) << 16);
    return tca8418_write(d, TCA8418_REG_INT_STAT, TCA8418_INT_GPI);
}
