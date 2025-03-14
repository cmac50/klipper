// GPIO functions optimized for stm32h7
//
// Copyright (C) 2019-2025  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // ffs
#include "board/irq.h" // irq_save
#include "command.h" // DECL_ENUMERATION_RANGE
#include "gpio.h" // gpio_out_setup
#include "internal.h" // gpio_peripheral
#include "sched.h" // sched_shutdown

DECL_ENUMERATION_RANGE("pin", "PA0", GPIO('A', 0), 16);
DECL_ENUMERATION_RANGE("pin", "PB0", GPIO('B', 0), 16);
DECL_ENUMERATION_RANGE("pin", "PC0", GPIO('C', 0), 16);
#ifdef GPIOD
DECL_ENUMERATION_RANGE("pin", "PD0", GPIO('D', 0), 16);
#endif
#ifdef GPIOE
DECL_ENUMERATION_RANGE("pin", "PE0", GPIO('E', 0), 16);
#endif
#ifdef GPIOF
DECL_ENUMERATION_RANGE("pin", "PF0", GPIO('F', 0), 16);
#endif
#ifdef GPIOG
DECL_ENUMERATION_RANGE("pin", "PG0", GPIO('G', 0), 16);
#endif
#ifdef GPIOH
DECL_ENUMERATION_RANGE("pin", "PH0", GPIO('H', 0), 16);
#endif
#ifdef GPIOI
DECL_ENUMERATION_RANGE("pin", "PI0", GPIO('I', 0), 16);
#endif

GPIO_TypeDef * const digital_regs[] = {
    ['A' - 'A'] = GPIOA, GPIOB, GPIOC,
#ifdef GPIOD
    ['D' - 'A'] = GPIOD,
#endif
#ifdef GPIOE
    ['E' - 'A'] = GPIOE,
#endif
#ifdef GPIOF
    ['F' - 'A'] = GPIOF,
#endif
#ifdef GPIOG
    ['G' - 'A'] = GPIOG,
#endif
#ifdef GPIOH
    ['H' - 'A'] = GPIOH,
#endif
#ifdef GPIOI
    ['I' - 'A'] = GPIOI,
#endif
};

// Convert a register and bit location back to an integer pin identifier
static int
regs_to_pin(GPIO_TypeDef *regs, uint32_t bit)
{
    int i;
    for (i=0; i<ARRAY_SIZE(digital_regs); i++)
        if (digital_regs[i] == regs)
            return GPIO('A' + i, ffs(bit)-1);
    return 0;
}

// Verify that a gpio is a valid pin
static int
gpio_valid(uint32_t pin)
{
    uint32_t port = GPIO2PORT(pin);
    return port < ARRAY_SIZE(digital_regs) && digital_regs[port];
}

// The stm32h7 has very slow read access to the gpio registers.  Cache
// the ODR register in memory to speed up toggle operations.
static struct bsrr_cache
{
    uint32_t cached_bsrr;
} BSRR_CACHE[9][16];

static uint32_t
lowest_bit_pos(uint32_t x)
{
    return __builtin_ctz(x);
}

struct gpio_out
gpio_out_setup(uint32_t pin, uint32_t val)
{
    if (!gpio_valid(pin))
        shutdown("Not an output pin");
    uint32_t bit = GPIO2BIT(pin);
    uint32_t port = GPIO2PORT(pin);
    int index = lowest_bit_pos(bit);
    GPIO_TypeDef *regs = digital_regs[port];
    gpio_clock_enable(regs);

    irqstatus_t flag = irq_save();
    BSRR_CACHE[port][index].cached_bsrr = regs->ODR & bit;
    irq_restore(flag);
    if (BSRR_CACHE[port][index].cached_bsrr == 0)
        BSRR_CACHE[port][index].cached_bsrr = bit << 16;

    struct bsrr_cache *ptr = &BSRR_CACHE[port][index];
    struct gpio_out g = {.regs=regs, .c=ptr, .bit=bit};
    gpio_out_reset(g, val);
    return g;
}

void
gpio_out_reset(struct gpio_out g, uint32_t val)
{
    GPIO_TypeDef *regs = g.regs;
    int pin = regs_to_pin(regs, g.bit);
    irqstatus_t flag = irq_save();
    if (val)
        g.c->cached_bsrr = g.bit;
    else
        g.c->cached_bsrr = g.bit << 16;
    regs->BSRR = g.c->cached_bsrr;
    gpio_peripheral(pin, GPIO_OUTPUT, 0);
    irq_restore(flag);
}

void
gpio_out_toggle_noirq(struct gpio_out g)
{
    GPIO_TypeDef *regs = g.regs;
    g.c->cached_bsrr = g.c->cached_bsrr << 16 | g.c->cached_bsrr >> 16;
    regs->BSRR = g.c->cached_bsrr;
}

void
gpio_out_toggle(struct gpio_out g)
{
    irqstatus_t flag = irq_save();
    gpio_out_toggle_noirq(g);
    irq_restore(flag);
}

void
gpio_out_write(struct gpio_out g, uint32_t val)
{
    GPIO_TypeDef *regs = g.regs;
    irqstatus_t flag = irq_save();
    if (val)
        g.c->cached_bsrr = g.bit;
    else
        g.c->cached_bsrr = g.bit << 16;
    regs->BSRR = g.c->cached_bsrr;
    irq_restore(flag);
}


struct gpio_in
gpio_in_setup(uint32_t pin, int32_t pull_up)
{
    if (!gpio_valid(pin))
        shutdown("Not a valid input pin");
    GPIO_TypeDef *regs = digital_regs[GPIO2PORT(pin)];
    struct gpio_in g = { .regs=regs, .bit=GPIO2BIT(pin) };
    gpio_in_reset(g, pull_up);
    return g;
}

void
gpio_in_reset(struct gpio_in g, int32_t pull_up)
{
    GPIO_TypeDef *regs = g.regs;
    int pin = regs_to_pin(regs, g.bit);
    irqstatus_t flag = irq_save();
    gpio_peripheral(pin, GPIO_INPUT, pull_up);
    irq_restore(flag);
}

uint8_t
gpio_in_read(struct gpio_in g)
{
    GPIO_TypeDef *regs = g.regs;
    return !!(regs->IDR & g.bit);
}
