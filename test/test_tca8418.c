/*
 * test_tca8418.c — the TCA8418 driver (src/tca8418.c) against a fake chip.
 * https://github.com/BatteryJuice4/tca8418-driver — MIT licence.
 *
 * The fake follows SCPS215G: registers 0x01-0x2E (0x00 and 0x2F are not acknowledged),
 * one register per write unless CFG.AI is set, a 10-event FIFO read through KEY_EVENT_A
 * with its count in KEY_LCK_EC[3:0], INT_STAT bits cleared by writing 1 (K_INT stays while
 * events remain), overflow per CFG.OVR_FLOW_M, GPIO_DAT_STAT giving input levels (outputs:
 * their driven level). It records any output that is driven to the wrong level while it
 * is being configured.
 */
#include <string.h>

#include "tca8418.h"
#include "test.h"

static bool g_present = true;
static uint8_t g_reg[0x30];
static uint8_t g_ptr;
static uint8_t g_fifo[TCA8418_FIFO];
static int g_count;
static uint32_t g_in;       /* levels on the pins (inputs) */
static uint32_t g_int_stat; /* GPIO_INT_STAT, cleared when read */
static bool g_multi;        /* more than one data byte without AI */
static bool g_bad_reg;
static bool g_glitch;
static int g_watch = -1;    /* an output pin that must never be driven low */
static int g_fifo_reads;    /* reads of KEY_EVENT_A */

static uint32_t reg3(uint8_t base)
{
    return g_reg[base] | ((uint32_t)g_reg[base + 1] << 8) | ((uint32_t)(g_reg[base + 2] & 3) << 16);
}

static void check_watch(void)
{
    if (g_watch < 0) {
        return;
    }
    uint32_t b = 1u << g_watch;
    if ((reg3(TCA8418_REG_GPIO_DIR) & b) && !(reg3(TCA8418_REG_GPIO_DAT_OUT) & b)) {
        g_glitch = true;
    }
}

static void push_event(uint8_t ev)
{
    if (g_count == TCA8418_FIFO) {
        g_reg[TCA8418_REG_INT_STAT] |= TCA8418_INT_OVR;
        if (!(g_reg[TCA8418_REG_CFG] & TCA8418_CFG_OVR_FLOW_M)) {
            return; /* lost */
        }
        memmove(g_fifo, g_fifo + 1, TCA8418_FIFO - 1);
        g_count--;
    }
    g_fifo[g_count++] = ev;
    g_reg[TCA8418_REG_INT_STAT] |= TCA8418_INT_K;
}

static uint8_t read_reg(uint8_t r)
{
    switch (r) {
    case TCA8418_REG_KEY_LCK_EC:
        return (uint8_t)((g_reg[r] & 0x70) | g_count);
    case TCA8418_REG_KEY_EVENT_A: {
        g_fifo_reads++;
        if (g_count == 0) {
            return 0;
        }
        uint8_t v = g_fifo[0];
        memmove(g_fifo, g_fifo + 1, TCA8418_FIFO - 1);
        g_count--;
        return v;
    }
    case TCA8418_REG_GPIO_INT_STAT:
    case TCA8418_REG_GPIO_INT_STAT + 1:
    case TCA8418_REG_GPIO_INT_STAT + 2: {
        int sh = (r - TCA8418_REG_GPIO_INT_STAT) * 8;
        uint8_t v = (uint8_t)(g_int_stat >> sh);
        g_int_stat &= ~((uint32_t)0xFF << sh);
        return v;
    }
    case TCA8418_REG_GPIO_DAT_STAT:
    case TCA8418_REG_GPIO_DAT_STAT + 1:
    case TCA8418_REG_GPIO_DAT_STAT + 2: {
        uint32_t dir = reg3(TCA8418_REG_GPIO_DIR);
        uint32_t lv = (g_in & ~dir) | (reg3(TCA8418_REG_GPIO_DAT_OUT) & dir);
        return (uint8_t)(lv >> ((r - TCA8418_REG_GPIO_DAT_STAT) * 8));
    }
    default:
        return g_reg[r];
    }
}

static void write_reg(uint8_t r, uint8_t v)
{
    if (r == TCA8418_REG_INT_STAT) {
        uint8_t clr = v & 0x1F;
        if (g_count) {
            clr &= (uint8_t)~TCA8418_INT_K; /* stays while events remain */
        }
        g_reg[r] &= (uint8_t)~clr;
        return;
    }
    if (r == TCA8418_REG_KEY_LCK_EC) {
        g_reg[r] = v & 0x40;
        return;
    }
    g_reg[r] = v;
    check_watch();
}

static int f_xfer(void *ctx, uint8_t addr, const uint8_t *tx, size_t ntx, uint8_t *rx, size_t nrx)
{
    (void)ctx;
    if (!g_present || addr != TCA8418_ADDR || ntx < 1) {
        return -1;
    }
    if (tx[0] == 0x00 || tx[0] >= 0x2F) {
        g_bad_reg = true;
        return -1; /* reserved or no such register: not acknowledged */
    }
    g_ptr = tx[0];
    if (ntx > 2 && !(g_reg[TCA8418_REG_CFG] & TCA8418_CFG_AI)) {
        g_multi = true;
    }
    for (size_t i = 1; i < ntx; i++) {
        write_reg(g_ptr, tx[i]);
    }
    for (size_t i = 0; i < nrx; i++) {
        rx[i] = read_reg(g_ptr);
    }
    return 0;
}

static const tca8418_bus_t g_bus = {NULL, f_xfer};

int main(void)
{
    tca8418_t d = {.bus = &g_bus, .addr = TCA8418_ADDR};

    /* ---- helpers: the datasheet's key numbering ---- */
    CHECK_EQ(tca8418_key(0, 0), 1);
    CHECK_EQ(tca8418_key(2, 2), 23);
    CHECK_EQ(tca8418_key(7, 9), 80);
    CHECK_EQ(tca8418_key_row(45), 4);
    CHECK_EQ(tca8418_key_col(45), 4);
    CHECK_EQ(tca8418_key_col(10), 9);
    CHECK(tca8418_key_is_matrix(80) && !tca8418_key_is_matrix(97) && !tca8418_key_is_matrix(0));
    CHECK_EQ(tca8418_key_gpi_pin(97), TCA8418_ROW(0));
    CHECK_EQ(tca8418_key_gpi_pin(114), TCA8418_COL(9));
    CHECK_EQ(tca8418_key_gpi_pin(23), -1);
    CHECK(tca8418_ev_pressed(0x97) && !tca8418_ev_pressed(0x17));
    CHECK_EQ(tca8418_ev_key(0x97), 23);

    /* ---- init: no chip ---- */
    g_present = false;
    CHECK_EQ(tca8418_init(&d, 0), TCA8418_E_BUS);
    g_present = true;
    CHECK_EQ(tca8418_init(&d, TCA8418_CFG_AI), TCA8418_E_ARG);

    /* ---- init: a chip left configured, with events queued ---- */
    memset(g_reg, 0xA5, sizeof(g_reg));
    g_reg[TCA8418_REG_CFG] = 0; /* (AI off, or the junk would make writes increment) */
    g_reg[TCA8418_REG_INT_STAT] = 0;
    g_reg[TCA8418_REG_KEY_LCK_EC] = 0x40; /* locked */
    push_event(0x81);
    push_event(0x01);
    g_int_stat = 0x3;
    CHECK_EQ(tca8418_init(&d, TCA8418_CFG_KE_IEN | TCA8418_CFG_GPI_IEN), TCA8418_OK);
    bool clean = true;
    for (int r = TCA8418_REG_GPIO_DAT_OUT; r <= TCA8418_REG_GPIO_PULL + 2; r++) {
        clean &= g_reg[r] == 0;
    }
    CHECK(clean);
    CHECK_EQ(g_reg[TCA8418_REG_KP_LCK_TIMER], 0);
    CHECK_EQ(g_reg[TCA8418_REG_UNLOCK1], 0);
    CHECK_EQ(g_reg[TCA8418_REG_KEY_LCK_EC] & 0x40, 0);
    CHECK_EQ(g_count, 0);
    CHECK_EQ(g_int_stat, 0);
    CHECK_EQ(g_reg[TCA8418_REG_INT_STAT], 0);
    CHECK_EQ(g_reg[TCA8418_REG_CFG], TCA8418_CFG_KE_IEN | TCA8418_CFG_GPI_IEN);

    /* ---- a 3 x 3 matrix on ROW0-2 x COL0-2 ---- */
    CHECK_EQ(tca8418_keypad(&d, 0x07, 0x007), TCA8418_OK);
    CHECK_EQ(g_reg[TCA8418_REG_KP_GPIO], 0x07);
    CHECK_EQ(g_reg[TCA8418_REG_KP_GPIO + 1], 0x07);
    CHECK_EQ(g_reg[TCA8418_REG_KP_GPIO + 2], 0x00);
    CHECK_EQ(tca8418_keypad(&d, 0, 0x400), TCA8418_E_ARG);
    CHECK_EQ(tca8418_keypad(&d, 0x01, 0x300), TCA8418_OK); /* COL8, COL9 */
    CHECK_EQ(g_reg[TCA8418_REG_KP_GPIO + 2], 0x03);
    CHECK_EQ(tca8418_keypad(&d, 0x07, 0x007), TCA8418_OK);

    /* ---- events: in order, then INT cleared ---- */
    push_event(0x80 | tca8418_key(0, 0));
    push_event(tca8418_key(0, 0));
    push_event(0x80 | 23);
    uint8_t ev[TCA8418_FIFO];
    bool ovr = true;
    g_fifo_reads = 0;
    CHECK_EQ(tca8418_events(&d, ev, TCA8418_FIFO, &ovr), 3);
    CHECK_EQ(g_fifo_reads, 3); /* as many reads as the count says, no more */
    CHECK_EQ(ev[0], 0x81);
    CHECK_EQ(ev[1], 0x01);
    CHECK_EQ(ev[2], 0x97);
    CHECK(!ovr);
    CHECK_EQ(g_reg[TCA8418_REG_INT_STAT] & TCA8418_INT_K, 0);
    CHECK_EQ(tca8418_events(&d, ev, TCA8418_FIFO, NULL), 0);

    /* fewer than queued: the rest stay, and so does K_INT */
    push_event(0x82);
    push_event(0x02);
    push_event(0x83);
    CHECK_EQ(tca8418_events(&d, ev, 2, NULL), 2);
    CHECK_EQ(ev[1], 0x02);
    CHECK_EQ(g_count, 1);
    CHECK(g_reg[TCA8418_REG_INT_STAT] & TCA8418_INT_K);
    CHECK_EQ(tca8418_events(&d, ev, TCA8418_FIFO, NULL), 1);
    CHECK_EQ(ev[0], 0x83);
    CHECK_EQ(g_reg[TCA8418_REG_INT_STAT] & TCA8418_INT_K, 0);

    /* overflow, keeping the newest */
    tca8418_write(&d, TCA8418_REG_CFG, TCA8418_CFG_KE_IEN | TCA8418_CFG_OVR_FLOW_M);
    for (int i = 1; i <= 11; i++) {
        push_event((uint8_t)(0x80 | i));
    }
    CHECK_EQ(tca8418_events(&d, ev, TCA8418_FIFO, &ovr), 10);
    CHECK(ovr);
    CHECK_EQ(ev[0], 0x82);
    CHECK_EQ(ev[9], 0x8B);
    CHECK_EQ(g_reg[TCA8418_REG_INT_STAT] & TCA8418_INT_OVR, 0);

    /* ---- outputs: COL9 high (an enable line), no glitch low on the way ---- */
    g_watch = TCA8418_COL(9);
    CHECK_EQ(tca8418_gpio_output(&d, TCA8418_COL(9), true), TCA8418_OK);
    CHECK(!g_glitch);
    CHECK_EQ(g_reg[TCA8418_REG_GPIO_DIR + 2], 0x02);
    CHECK_EQ(g_reg[TCA8418_REG_GPIO_DAT_OUT + 2], 0x02);
    bool hi = false;
    CHECK_EQ(tca8418_gpio_read(&d, TCA8418_COL(9), &hi), TCA8418_OK);
    CHECK(hi);
    g_watch = -1;
    CHECK_EQ(tca8418_gpio_write(&d, TCA8418_COL(9), false), TCA8418_OK);
    CHECK_EQ(tca8418_gpio_read(&d, TCA8418_COL(9), &hi), TCA8418_OK);
    CHECK(!hi);
    CHECK_EQ(tca8418_gpio_output(&d, TCA8418_COL(8), false), TCA8418_OK);
    CHECK_EQ(g_reg[TCA8418_REG_GPIO_DIR + 2], 0x03); /* COL9 left alone */
    CHECK_EQ(tca8418_gpio_output(&d, TCA8418_ROW(4), true), TCA8418_OK);
    CHECK_EQ(g_reg[TCA8418_REG_GPIO_DIR], 0x10);
    CHECK_EQ(tca8418_gpio_write(&d, 18, true), TCA8418_E_ARG);
    CHECK_EQ(tca8418_gpio_output(&d, -1, true), TCA8418_E_ARG);

    /* an input with interrupts and events turned into an output loses them */
    CHECK_EQ(tca8418_gpio_input(&d, TCA8418_COL(6), TCA8418_IN_IRQ | TCA8418_IN_EVENT), TCA8418_OK);
    CHECK_EQ(g_reg[TCA8418_REG_GPIO_INT_EN + 1], 0x40);
    CHECK_EQ(tca8418_gpio_output(&d, TCA8418_COL(6), false), TCA8418_OK);
    CHECK_EQ(g_reg[TCA8418_REG_GPIO_INT_EN + 1], 0x00);
    CHECK_EQ(g_reg[TCA8418_REG_GPI_EM + 1], 0x00);
    CHECK_EQ(tca8418_gpio_input(&d, TCA8418_COL(6), 0), TCA8418_OK);

    /* an output taken from the matrix leaves it */
    CHECK_EQ(tca8418_gpio_output(&d, TCA8418_COL(2), true), TCA8418_OK);
    CHECK_EQ(g_reg[TCA8418_REG_KP_GPIO + 1], 0x03);

    /* ---- inputs: status lines on COL5-7 ---- */
    CHECK_EQ(tca8418_gpio_input(&d, TCA8418_COL(5), TCA8418_IN_NODEBOUNCE | TCA8418_IN_IRQ),
             TCA8418_OK);
    CHECK_EQ(g_reg[TCA8418_REG_GPIO_DIR + 1] & 0x20, 0);
    CHECK_EQ(g_reg[TCA8418_REG_DEBOUNCE_DIS + 1], 0x20);
    CHECK_EQ(g_reg[TCA8418_REG_GPIO_INT_EN + 1], 0x20);
    CHECK_EQ(g_reg[TCA8418_REG_GPIO_PULL + 1], 0x00); /* pull-up on */
    CHECK_EQ(g_reg[TCA8418_REG_GPI_EM + 1], 0x00);
    CHECK_EQ(g_reg[TCA8418_REG_GPIO_INT_LVL + 1], 0x00); /* falling */
    CHECK_EQ(tca8418_gpio_input(&d, TCA8418_COL(7),
                                TCA8418_IN_NOPULL | TCA8418_IN_HIGH | TCA8418_IN_EVENT),
             TCA8418_OK);
    CHECK_EQ(g_reg[TCA8418_REG_GPIO_PULL + 1], 0x80);
    CHECK_EQ(g_reg[TCA8418_REG_GPIO_INT_LVL + 1], 0x80);
    CHECK_EQ(g_reg[TCA8418_REG_GPI_EM + 1], 0x80);
    CHECK_EQ(g_reg[TCA8418_REG_DEBOUNCE_DIS + 1], 0x20); /* COL5's kept */
    /* an output turned back into an input */
    CHECK_EQ(tca8418_gpio_input(&d, TCA8418_COL(9), 0), TCA8418_OK);
    CHECK_EQ(g_reg[TCA8418_REG_GPIO_DIR + 2], 0x01);
    /* a matrix pin turned into an input leaves the matrix */
    CHECK_EQ(tca8418_gpio_input(&d, TCA8418_ROW(2), 0), TCA8418_OK);
    CHECK_EQ(g_reg[TCA8418_REG_KP_GPIO], 0x03);
    CHECK_EQ(tca8418_gpio_input(&d, TCA8418_COL(5), 0x20), TCA8418_E_ARG);

    g_in = (1u << TCA8418_COL(5)) | (1u << TCA8418_COL(9)) | (1u << TCA8418_ROW(1));
    uint32_t all = 0;
    CHECK_EQ(tca8418_gpio_read_all(&d, &all), TCA8418_OK);
    CHECK(all & (1u << TCA8418_COL(5)));
    CHECK(all & (1u << TCA8418_COL(9)));
    CHECK(all & (1u << TCA8418_ROW(1)));
    CHECK(all & (1u << TCA8418_ROW(4))); /* the output driven high */
    CHECK(!(all & (1u << TCA8418_COL(6))));
    CHECK(!(all & (1u << TCA8418_COL(8)))); /* output driven low */
    CHECK_EQ(tca8418_gpio_read(&d, TCA8418_COL(5), &hi), TCA8418_OK);
    CHECK(hi);
    CHECK_EQ(tca8418_gpio_read(&d, TCA8418_COL(6), &hi), TCA8418_OK);
    CHECK(!hi);

    /* ---- GPI interrupts ---- */
    g_int_stat = 1u << TCA8418_COL(5) | 1u << TCA8418_COL(8);
    g_reg[TCA8418_REG_INT_STAT] |= TCA8418_INT_GPI;
    uint32_t ch = 0;
    CHECK_EQ(tca8418_gpi_changes(&d, &ch), TCA8418_OK);
    CHECK_EQ(ch, (1u << TCA8418_COL(5)) | (1u << TCA8418_COL(8)));
    CHECK_EQ(g_reg[TCA8418_REG_INT_STAT] & TCA8418_INT_GPI, 0);

    /* ---- bus discipline ---- */
    CHECK(!g_multi);
    CHECK(!g_bad_reg);

    TEST_MAIN_END();
}
