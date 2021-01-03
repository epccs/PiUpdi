/*
digital is a library with Wiring like functions
Copyright (C) 2019 Ronald Sutherland

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#include <avr/pgmspace.h>
#include <util/atomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include "../lib/timers_bsd.h"
#include "../lib/parse.h"
#include "../lib/io_enum_bsd.h"
#include "digital.h"

#define SERIAL_PRINT_DELAY_MILSEC 10000

// pin number must be valid in arg[0] from parse
void echo_io_pin_in_json_rply(void)
{
    if (atoi(arg[0]) == 0) 
    {
        printf_P(PSTR("AIN0"));
    }
    if (atoi(arg[0]) == 1) 
    {
        printf_P(PSTR("AIN1"));
    }
    if (atoi(arg[0]) == 2)
    {
        printf_P(PSTR("AIN2"));
    }
    if (atoi(arg[0]) == 3) 
    {
        printf_P(PSTR("AIN3"));
    }
    if (atoi(arg[0]) == 4)
    {
        printf_P(PSTR("AIN4"));
    }
    if (atoi(arg[0]) == 5)
    {
        printf_P(PSTR("AIN5"));
    }
    if (atoi(arg[0]) == 6)
    {
        printf_P(PSTR("AIN6"));
    }
    if (atoi(arg[0]) == 7)
    {
        printf_P(PSTR("AIN7"));
    }
}

// set io direction (DIRECTION_INPUT or DIRECTION_OUTPUT)
// ioDir(MCU_IO_t io, DIRECTION_t dir)
void Direction(void)
{
    if ( (command_done == 10) )
    {
        // check that arg[0] is a digit 
        if ( ( !( isdigit(arg[0][0]) ) ) )
        {
            printf_P(PSTR("{\"err\":\"ioDirNaN\"}\r\n"));
            initCommandBuffer();
            return;
        }
        // and arg[0] value is AIN0..AIN7
        uint8_t a = atoi(arg[0]);
        if ( (a < MCU_IO_AIN0) || (a > MCU_IO_AIN7) )
        {
            printf_P(PSTR("{\"err\":\"ioDirOutOfRng\"}\r\n"));
            initCommandBuffer();
            return;
        }
        // also arg[1] is not ('INPUT' or 'OUTPUT')
        if ( !( (strcmp_P( arg[1], PSTR("INPUT")) == 0) || (strcmp_P( arg[1], PSTR("OUTPUT")) == 0) ) ) 
        {
            printf_P(PSTR("{\"err\":\"ioDirNaInOut\"}\r\n"));
            initCommandBuffer();
            return;
        }
        if (strcmp_P( arg[1], PSTR("OUTPUT")) == 0 ) 
        {
            ioDir( (MCU_IO_t) a, DIRECTION_OUTPUT);
        }
        else
        {
            ioDir( (MCU_IO_t) a, DIRECTION_INPUT);
        }
        
        printf_P(PSTR("{\""));
        command_done = 11;
    }
    else if ( (command_done == 11) )
    {  
        echo_io_pin_in_json_rply();
        printf_P(PSTR("\":\""));
        command_done = 12;
    }
    else if ( (command_done == 12) )
    {
        printf( arg[1] );
        printf_P(PSTR("\"}\r\n"));
        initCommandBuffer();
    }
    else
    {
        printf_P(PSTR("{\"err\":\"ioDirCmdDnWTF\"}\r\n"));
        initCommandBuffer();
    }
}

// Write( arg[0], arg[1] ) maps to ioWrite(MCU_IO_t io, LOGIC_LEVEL_t level)
// return value in JSON is from ioRead(MCU_IO_t io)
void Write(void)
{
    if ( (command_done == 10) )
    {
        // check that arg[0] is the io_enum value 
        if ( ( !( isdigit(arg[0][0]) ) ) )
        {
            printf_P(PSTR("{\"err\":\"ioWrtNaN\"}\r\n"));
            initCommandBuffer();
            return;
        }
        // and arg[0] value is AIN0..AIN7
        uint8_t a = atoi(arg[0]);
        if ( (a < MCU_IO_AIN0) || (a > MCU_IO_AIN7) )
        {
            printf_P(PSTR("{\"err\":\"ioWrtOutOfRng\"}\r\n"));
            initCommandBuffer();
            return;
        }
        // also arg[1] is not ('HIGH' or 'LOW')
        if ( !( (strcmp_P( arg[1], PSTR("HIGH")) == 0) || (strcmp_P( arg[1], PSTR("LOW")) == 0) ) ) 
        {
            printf_P(PSTR("{\"err\":\"ioWrtNaState\"}\r\n"));
            initCommandBuffer();
            return;
        }
        if (strcmp_P( arg[1], PSTR("HIGH")) == 0 ) 
        {
            ioWrite( (MCU_IO_t) a, LOGIC_LEVEL_HIGH);
        }
        else
        {
            ioWrite( (MCU_IO_t) a, LOGIC_LEVEL_LOW);
        }
        
        printf_P(PSTR("{\""));
        command_done = 11;
    }
    else if ( (command_done == 11) )
    {  
        echo_io_pin_in_json_rply();
        printf_P(PSTR("\":\""));
        command_done = 12;
    }
    else if ( (command_done == 12) )
    {
        uint8_t a = atoi(arg[0]);
        bool pin = ioRead( (MCU_IO_t) a);
        if (pin)
        {
            printf_P(PSTR("HIGH"));
        }
        else
        {
            printf_P(PSTR("LOW"));
        }
        printf_P(PSTR("\"}\r\n"));
        initCommandBuffer();
    }
    else
    {
        printf_P(PSTR("{\"err\":\"ioWrtCmdDnWTF\"}\r\n"));
        initCommandBuffer();
    }
}

// Toggle( arg[0] ) maps to ioToggle(MCU_IO_t io)
// return value in JSON is from ioRead(MCU_IO_t io)
void Toggle(void)
{
    if ( (command_done == 10) )
    {
        // check that arg[0] is a digit 
        if ( ( !( isdigit(arg[0][0]) ) ) )
        {
            printf_P(PSTR("{\"err\":\"ioTogNaN\"}\r\n"));
            initCommandBuffer();
            return;
        }
        // and arg[0] value is AIN0..AIN7
        uint8_t a = atoi(arg[0]);
        if ( (a < MCU_IO_AIN0) || (a > MCU_IO_AIN7) )
        {
            printf_P(PSTR("{\"err\":\"ioTogOutOfRng\"}\r\n"));
            initCommandBuffer();
            return;
        }
        ioToggle( (MCU_IO_t) a);
        
        printf_P(PSTR("{\""));
        command_done = 11;
    }
    else if ( (command_done == 11) )
    {  
        echo_io_pin_in_json_rply();
        printf_P(PSTR("\":\""));
        command_done = 12;
    }
    else if ( (command_done == 12) )
    {
        uint8_t a = atoi(arg[0]);
        bool pin = ioRead( (MCU_IO_t) a);
        if (pin)
        {
            printf_P(PSTR("HIGH"));
        }
        else
        {
            printf_P(PSTR("LOW"));
        }
        printf_P(PSTR("\"}\r\n"));
        initCommandBuffer();
    }
    else
    {
        printf_P(PSTR("{\"err\":\"ioTogCmdDnWTF\"}\r\n"));
        initCommandBuffer();
    }
}

//ioRead( arg[0] ) maps to ioRead(MCU_IO_t io)
void Read(void)
{
    if ( (command_done == 10) )
    {
        // check that arg[0] is a digit 
        if ( ( !( isdigit(arg[0][0]) ) ) )
        {
            printf_P(PSTR("{\"err\":\"ioRdNaN\"}\r\n"));
            initCommandBuffer();
            return;
        }
        // and arg[0] value is the AIN0..AIN7 enum value
        uint8_t a = atoi(arg[0]);
        if ( (a < MCU_IO_AIN0) || (a > MCU_IO_AIN7) )
        {
            printf_P(PSTR("{\"err\":\"ioRdOutOfRng\"}\r\n"));
            initCommandBuffer();
            return;
        }

        printf_P(PSTR("{\""));
        command_done = 11;
    }
    else if ( (command_done == 11) )
    {  
        echo_io_pin_in_json_rply();
        printf_P(PSTR("\":\""));
        command_done = 12;
    }
    else if ( (command_done == 12) )
    {
        uint8_t a = atoi(arg[0]);
        bool pin = ioRead( (MCU_IO_t) a);
        if (pin)
        {
            printf_P(PSTR("HIGH"));
        }
        else
        {
            printf_P(PSTR("LOW"));
        }
        printf_P(PSTR("\"}\r\n"));
        initCommandBuffer();
    }
    else
    {
        printf_P(PSTR("{\"err\":\"ioRdCmdDnWTF\"}\r\n"));
        initCommandBuffer();
    }
}


// Control( arg[0], arg[1], arg[2] ) maps to ioCntl(MCU_IO_t io, PORT_ISC_t isc, PORT_PULLUP_t pu, PORT_INVERT_t inv) 
// TBD
// set PORT_ISC_INTDISABLE_gc for Input/Sense Configuration select bits 
// use arg[1] for pullup
// and arg[2] for invert