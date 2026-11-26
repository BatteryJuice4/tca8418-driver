/*
 * pico_example.c — TCA8418 on a Raspberry Pi Pico (RP2040 / RP2350), Pico SDK: a 3 x 3 key
 * matrix on ROW0-2 x COL0-2 printed as it is pressed, plus an LED on COL9 that follows key
 * 1 and an input on COL5 reported when it changes. Link pico_stdlib and hardware_i2c.
 * MIT licence.
 *
 * Wiring: SDA GP4, SCL GP5 (with pull-ups), INT GP6 (open drain: pull-up needed; the
 * Pico's internal one is used here), VCC 3.3 V. Each key joins one ROW to one COL.
 */
#include <stdio.h>

#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "tca8418.h"

#define PIN_INT 6

static int pico_xfer(void *ctx, uint8_t addr, const uint8_t *tx, size_t ntx, uint8_t *rx,
                     size_t nrx)
{
    i2c_inst_t *i2c = ctx;
    if (i2c_write_timeout_us(i2c, addr, tx, ntx, nrx > 0, 10000) != (int)ntx) {
        return -1;
    }
    if (nrx && i2c_read_timeout_us(i2c, addr, rx, nrx, false, 10000) != (int)nrx) {
        return -1;
    }
    return 0;
}

static const tca8418_bus_t bus = {i2c0, pico_xfer};
static tca8418_t kp = {.bus = &bus, .addr = TCA8418_ADDR};

int main(void)
{
    stdio_init_all();
    i2c_init(i2c0, 400000);
    gpio_set_function(4, GPIO_FUNC_I2C);
    gpio_set_function(5, GPIO_FUNC_I2C);
    gpio_init(PIN_INT);
    gpio_pull_up(PIN_INT);

    if (tca8418_init(&kp, TCA8418_CFG_KE_IEN | TCA8418_CFG_GPI_IEN) != TCA8418_OK) {
        printf("no TCA8418 found\n");
        return 1;
    }
    tca8418_keypad(&kp, 0x07, 0x007);                       /* ROW0-2 x COL0-2 */
    tca8418_gpio_output(&kp, TCA8418_COL(9), false);        /* LED */
    tca8418_gpio_input(&kp, TCA8418_COL(5), TCA8418_IN_IRQ | TCA8418_IN_NODEBOUNCE);

    for (;;) {
        while (gpio_get(PIN_INT)) {
            sleep_ms(5); /* INT is low while there is news; use an interrupt in real code */
        }
        uint8_t ev[TCA8418_FIFO];
        int n = tca8418_events(&kp, ev, TCA8418_FIFO, NULL);
        for (int i = 0; i < n; i++) {
            uint8_t k = tca8418_ev_key(ev[i]);
            bool down = tca8418_ev_pressed(ev[i]);
            if (tca8418_key_is_matrix(k)) {
                printf("key row %d col %d %s\n", tca8418_key_row(k), tca8418_key_col(k),
                       down ? "down" : "up");
                if (k == tca8418_key(0, 0)) {
                    tca8418_gpio_write(&kp, TCA8418_COL(9), down);
                }
            }
        }
        uint32_t changed;
        if (tca8418_gpi_changes(&kp, &changed) == TCA8418_OK && changed) {
            bool hi;
            tca8418_gpio_read(&kp, TCA8418_COL(5), &hi);
            printf("COL5 is now %s\n", hi ? "high" : "low");
        }
    }
}
