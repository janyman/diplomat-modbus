#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/atomic.h>

#include "sys_timer.h"

#define TIMER0_PRESCALER 64UL
#define TIMER0_FREQUENCY 1000UL       /* 1 kHz = 1 ms */

#if 0
#define TIMER0_OCR0A \
      ((F_CPU / TIMER0_PRESCALER / TIMER0_FREQUENCY) - 1UL)
#else
#define TIMER0_OCR0A \
    (((F_CPU + (TIMER0_PRESCALER * TIMER0_FREQUENCY / 2UL)) / \
        (TIMER0_PRESCALER * TIMER0_FREQUENCY)) - 1UL)
#endif

void sys_timer_init(void) {
    TCCR0A = _BV(WGM01);                  /* CTC mode */
    TCCR0B = _BV(CS01) | _BV(CS00);       /* prescaler 64 (TIMER0_PRESCALER) */
    OCR0A = (uint8_t)TIMER0_OCR0A;
    TIMSK0 = _BV(OCIE0A);
}

static volatile uint32_t system_millis;

ISR(TIMER0_COMPA_vect) {
    system_millis++;
}


sys_time_t sys_timer_now(void) {
    sys_time_t now = 0;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
          now = system_millis;
    }
    return now;
}