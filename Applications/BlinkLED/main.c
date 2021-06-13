/* Blink LED
Copyright (C) 2019 Ronald Sutherland

Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES 
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF 
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE 
FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY 
DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, 
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, 
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

https://en.wikipedia.org/wiki/BSD_licenses#0-clause_license_(%22Zero_Clause_BSD%22)
*/ 

#include <stdbool.h>
#include <stdlib.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include "../lib/uart0_bsd.h"
#include "../lib/io_enum_bsd.h"
#include "../lib/timers_bsd.h"
#include "../lib/twi.h"

#define BLINK_DELAY 1000UL
unsigned long blink_started_at;
unsigned long blink_delay;

static int got_a;

FILE *uart0;

// don't block (e.g. _delay_ms(1000) ), ckeck if time has elapsed to toggle 
void blink(void)
{
    unsigned long kRuntime = elapsed(&blink_started_at);
    if ( kRuntime > blink_delay)
    {
        ioToggle(MCU_IO_TX2);
        if(ioRead(MCU_IO_TX2)) // ping i2c every other toggle
        {
              uint8_t data[] = {'a'}; 
            twim_write( data, sizeof(data) ); // same address 41
            bool lastTwimResult = twim_waitUS( 3000 ); // wait for complettion or timeout (3ms)
            while (!uart0_availableForWrite());
            if (lastTwimResult == false) fprintf(uart0,"%lu:twi0 transaction good\r\n", blink_delay);
            if (lastTwimResult == true) fprintf(uart0,"%lu:twi0 transaction failed\r\n", blink_delay);
        }

        // next toggle 
        blink_started_at += blink_delay; 
    }
}

// abort++. 
void abort_safe(void)
{
    // make sure controled devices are safe befor waiting on UART 
    ioDir(MCU_IO_TX2,DIRECTION_OUTPUT);
    ioWrite(MCU_IO_TX2,LOGIC_LEVEL_LOW);
    // flush the UART befor halt
    uart0_flush();
    twim_off(); // need to clear the pins
    ioCntl(MCU_IO_SCL0, PORT_ISC_INTDISABLE_gc, PORT_PULLUP_DISABLE, PORT_INVERT_NORMAL);
    ioCntl(MCU_IO_SDA0, PORT_ISC_INTDISABLE_gc, PORT_PULLUP_DISABLE, PORT_INVERT_NORMAL);
    _delay_ms(20); // wait for last byte to send
    uart0_init(0, 0); // disable UART hardware
    // turn off interrupts and then spin loop a LED toggle
    cli();
    while(1)
    {
        _delay_ms(100);
        ioToggle(MCU_IO_TX2);
    }
}

void setup(void)
{
    ioCntl(MCU_IO_TX2, PORT_ISC_INTDISABLE_gc, PORT_PULLUP_DISABLE, PORT_INVERT_NORMAL);
    ioDir(MCU_IO_TX2, DIRECTION_OUTPUT);
    ioWrite(MCU_IO_TX2,LOGIC_LEVEL_HIGH);

    /* Initialize UART0 to 38.4kbps for streaming, it returns a pointer to a FILE structure */
    uart0 = uart0_init(38400UL, UART0_RX_REPLACE_CR_WITH_NL);

    //TCA0_HUNF used for timing, TCA0 split for 6 PWM's (WO0..WO5).
    initTimers();

    /* Initialize I2C*/
    twim_defaultPins();             //master pins (same as slave)
    twim_baud( F_CPU, 100000ul );   //100kHz

    sei(); // Enable global interrupts to start TIMER0

    // tick count is not milliseconds use cnvrt_milli() to convert time into ticks, thus tickAtomic()/cnvrt_milli(1000) gives seconds
    blink_started_at = tickAtomic();
    blink_delay = cnvrt_milli(BLINK_DELAY);

    got_a = 0;

    twim_on(41);                // turn on master and set slave address 41
    uint8_t data[] = {108};
    twim_write( data, sizeof(data) );
    bool lastTwimResult = twim_waitUS( 3000 ); // wait for complettion or timeout (3ms)
    while (!uart0_availableForWrite());
    if (lastTwimResult == false) fprintf(uart0,"twi0 transaction good\r\n");
    if (lastTwimResult == true) fprintf(uart0,"twi0 transaction failed\r\n");
}

int main(void)
{
    setup();

    while (1)
    {
        if(uart0_available())
        {
            // A standard libc streaming function used for input of one char.
            int input = fgetc(uart0);

            // A standard libc streaming function used for output.
            fprintf(uart0,"%c\r", input); 

            if (input == '$') 
            {
                // Variant of fprintf() that uses a format string which resides in flash memory.
                fprintf_P(uart0,PSTR("{\"abort\":\"'$' found\"}\r\n"));
                abort_safe();
            }

            // press 'a' to stop blinking.
            if(input == 'a') 
            {
                got_a = 1; 
            }
            else
            {
                got_a = 0;
            }
        }
        if (!got_a)
        {
            blink(); // also ping_i2c() at the toggle time
        }
    }
}

