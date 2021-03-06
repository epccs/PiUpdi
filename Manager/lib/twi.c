/*
AVR Dx Interrupt-Driven Asynchronous I2C C library 
Copyright (C) 2020 Ronald Sutherland
Modified from https://github.com/cv007/Avr01Dx_Twi

Permission to use, copy, modify, and/or distribute this software for any 
purpose with or without fee is hereby granted.

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
#include <stdint.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/delay.h>

#include "twi.h"

/*------------------------------------------------------------------------------
    Twi0 pins

    master/slave - (not dual mode)

              Dx/mega0    tiny0/1     tiny0/1 XX2 (8pin)
default SCL      PA3          PB0         PA2
default SDA      PA2          PB1         PA1
    alt SCL      PC3          PA2         --
    alt SDA      PC2          PA1         --

TWI0.DUALCTRL = TWI_ENABLE_bm; \\ is used to split the functions so that master is on one pair and slave on the other
------------------------------------------------------------------------------*/

//mega0
#if defined(PORTMUX_TWISPIROUTEA)
    #define TWI_PULL_DEFAULT()      PORTA.PIN3CTRL |= 1<<3; PORTA.PIN2CTRL |= 1<<3
    #define TWI_PULL_ALT()          PORTC.PIN3CTRL |= 1<<3; PORTC.PIN2CTRL |= 1<<3
    #define TWI_PORTMUX_DEFAULT()   PORTMUX.TWISPIROUTEA &= ~PORTMUX_TWI0_gm;
    #define TWI_PORTMUX_ALT()       PORTMUX.TWISPIROUTEA = (PORTMUX.TWISPIROUTEA & ~PORTMUX_TWI0_gm) | PORTMUX_TWI0_ALT2_gc;
//avrDa or Db
#elif defined(PORTMUX_TWIROUTEA)
    #define TWI_PULL_DEFAULT()      PORTA.PIN3CTRL |= 1<<3; PORTA.PIN2CTRL |= 1<<3
    #define TWI_PULL_ALT()          PORTC.PIN3CTRL |= 1<<3; PORTC.PIN2CTRL |= 1<<3
    #define TWI_PORTMUX_DEFAULT()   PORTMUX.TWIROUTEA = (PORTMUX.TWIROUTEA & ~PORTMUX_TWI0_gm) | PORTMUX_TWI0_DEFAULT_gc;
    #define TWI_PORTMUX_ALT()       PORTMUX.TWIROUTEA = (PORTMUX.TWIROUTEA & ~PORTMUX_TWI0_gm) | PORTMUX_TWI0_ALT2_gc;
//tiny0/1 w/alternate pins
#elif defined(PORTMUX_CTRLB) && defined(PORTMUX_TWI0_bm)
    #define TWI_PULL_DEFAULT()      PORTB.PIN0CTRL |= 1<<3; PORTB.PIN1CTRL |= 1<<3
    #define TWI_PULL_ALT()          PORTA.PIN2CTRL |= 1<<3; PORTA.PIN1CTRL |= 1<<3
    #define TWI_PORTMUX_DEFAULT()   PORTMUX.CTRLB &= ~PORTMUX_TWI0_bm;
    #define TWI_PORTMUX_ALT()       PORTMUX.CTRLB |= PORTMUX_TWI0_bm;
//tiny0/1 no alternate pins
#elif defined(PORTMUX_CTRLB) && !defined(PORTMUX_TWI0_bm)
    #define TWI_PORTMUX_DEFAULT()
    #define TWI_PORTMUX_ALT()
    #define TWI_PULL_DEFAULT()      PORTA.PIN2CTRL |= 1<<3; PORTA.PIN1CTRL |= 1<<3
    #define TWI_PULL_ALT()          TWIS_PULL_DEFAULT()
//unknown
#else
    #error "Unknown portmux/pin settings for TWI0"
#endif

//======================================================================
//  twim - Twi0, master - avr mega0, tiny0/1, da
//          private:
//======================================================================

static twim_callbackT       twim_isrCallback_;
static const uint8_t*       txbuf_;
static const uint8_t*       txbufEnd_;
static const uint8_t*       txbuf2_;
static const uint8_t*       txbuf2End_;
static uint8_t*             rxbuf_;
static const uint8_t*       rxbufEnd_;
static volatile bool        twi_lastResult_; //1=ok,0=fail

//local enums
//MCTRLB flush3|ack2|cmd1:0
enum { ACK = 0, READ = 2, STOP = 3, NACK = 4,  FLUSH = 8 };
//MSTATUS RIF7|WIF6|CLKHOLD5|RXACK4|ARBLOST3|BUSERR2|BUSSTATE1:0
enum { RIF = 0x80, WIF = 0x40, CLKHOLD = 0x20, RXNACK = 0x10, ARBLOST = 0x8, BUSERR = 0x4 };
enum { ALLFLAGS = RIF|WIF|CLKHOLD|ARBLOST|BUSERR };
enum { ANYERR = ARBLOST|BUSERR }; //error bits
enum { RIEN = RIF, WIEN = WIF, RWIEN = RIEN|WIEN }; //irq bits
enum { RW = 1 }; //address bit0
enum { UNKNOWN = 0, IDLE, OWNER, BUSBUSY, BUSMASK = 3 }; //bus state
enum { READOK = RIF|CLKHOLD|OWNER, WRITEOK = WIF|CLKHOLD|OWNER };
enum { ENABLE = 1 }; //on/off

static void m_on            () { TWI0.MCTRLA |= ENABLE; }
static void m_off           () { TWI0.MCTRLA = 0; }
static void m_irqAllOn      () { TWI0.MCTRLA |=  RWIEN; }
static void m_irqAllOff     () { TWI0.MCTRLA &= ~RWIEN; }
static void m_toStateIdle   () { TWI0.MSTATUS = ALLFLAGS|IDLE; } //clear flags, set to IDLE
static void m_ackActionACK  () { TWI0.MCTRLB = ACK; }
static void m_ACKread       () { TWI0.MCTRLB = READ; }
static void m_NACKstop      () { TWI0.MCTRLB = NACK|STOP; }
static void m_address       (uint8_t v) { m_off(); TWI0.MADDR = v<<1; } //off so no start produced
static void m_startRead     () { m_ackActionACK(); TWI0.MADDR |= RW; } //reuse existing address
static void m_startWrite    () { TWI0.MADDR &= ~RW; }   //reuse existing address
static void m_write         (uint8_t v) { TWI0.MDATA = v; }
static uint8_t m_read       () { return TWI0.MDATA; }
static uint8_t m_status     () { return TWI0.MSTATUS; }
static bool m_isBusy        () { return TWI0.MCTRLA & RWIEN; }

                            //start a read or write, enable irq
static void m_startIrq      (bool wr)
                            {
                            if( wr ) m_startWrite(); else m_startRead();
                            twi_lastResult_ = false;
                            m_irqAllOn();
                            }

                            //for isr use
static void m_finished      (bool tf)
                            {
                            twi_lastResult_ = tf;
                            //NACKstop works for write also (nack not done, harmless)
                            m_NACKstop();
                            m_irqAllOff(); //do before callback in case call back starts another xfer
                            if( twim_isrCallback_ ) twim_isrCallback_();
                            }


ISR(TWI0_TWIM_vect)         {
                            uint8_t s = m_status();
                            //error
                            if( s & ANYERR ) return m_finished( false );
                            //read
                            if( s == READOK ){
                                *rxbuf_++ = m_read();
                                return rxbuf_ < rxbufEnd_ ? m_ACKread() : m_finished( true );
                                }
                            //write
                            if( s == WRITEOK ){
                                if( txbuf_ < txbufEnd_ ) return m_write( *txbuf_++ ); //more data
                                if( txbuf2_ < txbuf2End_ ) return m_write( *txbuf2_++ ); //more data
                                return rxbuf_ ? m_startRead() : m_finished( true ); //switch to read? or done
                                }
                            //unknown, or a write nack
                            m_finished( false );
                            }

    //==========
    // public:
    //==========

void twim_callback          (twim_callbackT cb) { twim_isrCallback_ = cb; } //optional, else use twim_waitUS
void twim_off               () { m_off(); }
void twim_on                (uint8_t addr) { m_address(addr); m_toStateIdle(); m_on(); }
bool twim_isBusy            () { return m_isBusy(); } //if irq on, is busy
bool twim_lastResultOK      () { return twi_lastResult_; }

                            //set default or alternate pins
void twim_defaultPins       () { TWI_PULL_DEFAULT(); TWI_PORTMUX_DEFAULT(); }
void twim_altPins           () { TWI_PULL_ALT(); TWI_PORTMUX_ALT(); }


                            //write+read (or write only, or read only)
void twim_writeRead         (const uint8_t* wbuf, uint16_t wn, uint8_t* rbuf, uint16_t rn)
                            {
                            txbuf_ = wbuf; txbufEnd_ = &wbuf[wn];
                            rxbuf_ = rbuf; rxbufEnd_ = &rbuf[rn];
                            txbuf2_ = 0; txbuf2End_ = 0;
                            m_startIrq( wn ); //if no write (wn==0), then will start a read irq
                            }

                            //write/write (such as a command, then a buffer)
void twim_writeWrite        (const uint8_t* wbuf, uint16_t wn, const uint8_t* wbuf2, uint16_t wn2)
                            {
                            txbuf_ = wbuf; txbufEnd_ = &wbuf[wn];
                            txbuf2_ = wbuf2; txbuf2End_ = &wbuf2[wn2];
                            rxbuf_ = 0; rxbufEnd_ = 0; //no read
                            m_startIrq( 1 ); //write only
                            }

                            //write only alias
void twim_write             (const uint8_t* wbuf, uint16_t wn) { twim_writeRead( wbuf, wn, 0, 0); }

                            //read only alias
void twim_read              (uint8_t* rbuf, uint16_t rn) { twim_writeRead( 0, 0, rbuf, rn); }

                            //blocking wait with timeout
bool twim_waitUS            (uint16_t us)
                            {
                            while( _delay_us(1), --us && twim_isBusy() ){}
                            return twim_lastResultOK();
                            //twi_lastResult_ is set to false at start of transaction
                            //will still be false if timeout
                            //check twim_isBusy() on your own to see if was a timeout (if returned false)
                            }

//======================================================================
//   twis.c - Twi slave
//          private:
//======================================================================

static volatile uint8_t     s_lastAddress_; //could also skip this, and simply make
                                      //the callback do a read in the TWIS_ADDRESSED
                                      //state if it needs to know the address when
                                      //multiple addresses are in use, but will just
                                      //store it here so callback does not need to
static twis_callback_t      twis_isrCallback_;


static void s_gencall       (uint8_t v) { TWI0.SADDR = (v<<1)|1; } //gencall enabled, so check address in callback
static void s_mask          (uint8_t v, bool nomask) { TWI0.SADDRMASK = (v<<1)|nomask; }
static void s_off           () { TWI0.SCTRLA &= ~1; }
static void s_on            () { TWI0.SCTRLA |= 1; }
static uint8_t s_read       () { return TWI0.SDATA; }
static void s_write         (uint8_t v) { TWI0.SDATA = v; }
static void s_irqAllOn      () { TWI0.SCTRLA |= 0xE0; } // TWI_DIEN_bm | TWI_APIEN_bm | TWI_PIEN_bm
static void s_irqAllOff     () { TWI0.SCTRLA &= ~0xE0; }
static uint8_t s_status     () { return TWI0.SSTATUS; }
static void s_clearFlags    () { TWI0.SSTATUS = 0xCC; }
static void s_nackComplete  () { TWI0.SCTRLB = 6; } //COMPTRANS, NACK
static void s_ack           () { TWI0.SCTRLB = 3; } //RESPONSE, ACK

//DIF:APIF:CLKHOLD:RXACK:COLL:BUSERR:DIR:AP
enum { DIF_DIRbm = 0x82, APIF_APbm = 0x41, RXNACKbm = 0x10, ERRbm = 0x0C,
       DIF_R = 0x82, DIF_W = 0x80, APIF_ADDR = 0x41, APIF_STOP = 0x40 };

                            //v = a copy of SSTATUS (used in isr)
static bool isDataRead      (uint8_t v) { return (v & DIF_DIRbm) == DIF_R; }         //DIF, DIR(1=R)
static bool isDataWrite     (uint8_t v) { return (v & DIF_DIRbm) == DIF_W; }         //DIF, DIR(0=W)
static bool isAddress       (uint8_t v) { return (v & APIF_APbm) == APIF_ADDR; }     //APIF, AP(1=addr)
static bool isStop          (uint8_t v) { return (v & APIF_APbm) == APIF_STOP; }     //APIF, AP(0=stop)
static bool isRxNack        (uint8_t v) { return (v & RXNACKbm); }                   //RXACK(0=ACK,1=NACK)
static bool isError         (uint8_t v) { return (v & ERRbm); }                      //COLL,BUSERR

                            //callback function returns true if want to proceed
ISR                         (TWI0_TWIS_vect)
                            {
                            static bool is1st;      //so can ignore rxack on first master read
                            uint8_t s = s_status();        //get a copy of status
                            twis_irqstate_t state = isError(s)     ? TWIS_ERROR : //do first
                                                    isStop(s)      ? TWIS_STOPPED :
                                                    isAddress(s)   ? TWIS_ADDRESSED :
                                                    isDataRead(s)  ? TWIS_MREAD :
                                                    isDataWrite(s) ? TWIS_MWRITE : TWIS_ERROR;
                            bool nacked = isRxNack(s);
                            bool done = false; //assume not done

                            if( state == TWIS_ADDRESSED ) {
                                s_lastAddress_ = s_read()>>1;
                                is1st = true;
                                }
                            else if( state == TWIS_MREAD ) {
                                if( is1st ) is1st = false; else done = nacked;
                                }
                            else if( state != TWIS_MWRITE ) done = true; //error or stopped

                            if( false == twis_isrCallback_(state, s) ) done = true;
                            if( done ) s_nackComplete(); else s_ack();
                            }

    //============
    // public:
    //============

void twis_defaultPins       () { /*TWI_PULL_DEFAULT();*/ TWI_PORTMUX_DEFAULT(); }
void twis_altPins           () { TWI_PULL_ALT(); TWI_PORTMUX_ALT(); }
void twis_off               () { s_irqAllOff(); s_off(); s_clearFlags(); }
void twis_write             (uint8_t v) { s_write(v); }
uint8_t twis_read           () { return s_read(); }
uint8_t twis_lastAddress    () { return s_lastAddress_; }    //last address we responded to
void twis_mask              (uint8_t v) { s_mask(v, true); }      //2nd address
void twis_addressMask       (uint8_t v) { s_mask(v, false); }     //address mask (no 2nd address)

void twis_init              (uint8_t addr, twis_callback_t cb)
                            {
                            if( ! cb ) return;          //assume everything other than 0 is valid
                            twis_off();                 //also clears flags
                            twis_isrCallback_ = cb;
                            s_gencall( addr );
                            s_irqAllOn();
                            s_on();
                            }

#if defined(TWI1)
// TWI0 has comments, they are removed for TWI1
// at this time only DA and DB have more than one TWI and the alt pins are only on 48 and 64 pin packages
    #define TWI1_PULL_DEFAULT()      PORTF.PIN3CTRL |= 1<<3; PORTF.PIN2CTRL |= 1<<3
    #define TWI1_PORTMUX_DEFAULT()   PORTMUX.TWIROUTEA = (PORTMUX.TWIROUTEA & ~PORTMUX_TWI1_gm) | PORTMUX_TWI1_DEFAULT_gc;
#if defined(PORTB)
    #define TWI1_PULL_ALT()          PORTB.PIN3CTRL |= 1<<3; PORTB.PIN2CTRL |= 1<<3
    #define TWI1_PORTMUX_ALT()       PORTMUX.TWIROUTEA = (PORTMUX.TWIROUTEA & ~PORTMUX_TWI1_gm) | PORTMUX_TWI1_ALT2_gc;
#endif

static twim_callbackT        twi1m_isrCallback_;
static const uint8_t*        tx1buf_;
static const uint8_t*        tx1bufEnd_;
static const uint8_t*        tx1buf2_;
static const uint8_t*        tx1buf2End_;
static uint8_t*              rx1buf_;
static const uint8_t*        rx1bufEnd_;
static volatile bool         twi1_lastResult_; 

static void m1_on              () { TWI1.MCTRLA |= ENABLE; }
static void m1_off             () { TWI1.MCTRLA = 0; }
static void m1_irqAllOn        () { TWI1.MCTRLA |=  RWIEN; }
static void m1_irqAllOff       () { TWI1.MCTRLA &= ~RWIEN; }
static void m1_toStateIdle     () { TWI1.MSTATUS = ALLFLAGS|IDLE; }
static void m1_ackActionACK    () { TWI1.MCTRLB = ACK; }
static void m1_ACKread         () { TWI1.MCTRLB = READ; }
static void m1_NACKstop        () { TWI1.MCTRLB = NACK|STOP; }
static void m1_address         (uint8_t v) { m1_off(); TWI1.MADDR = v<<1; }
static void m1_startRead       () { m1_ackActionACK(); TWI1.MADDR |= RW; }
static void m1_startWrite      () { TWI1.MADDR &= ~RW; }
static void m1_write           (uint8_t v) { TWI1.MDATA = v; }
static uint8_t   m1_read            () { return TWI1.MDATA; }
static uint8_t   m1_status          () { return TWI1.MSTATUS; }
static bool m1_isBusy          () { return TWI1.MCTRLA & RWIEN; }
static void m1_startIrq     (bool wr)
                            {
                            if( wr ) m1_startWrite(); else m1_startRead();
                            twi1_lastResult_ = false;
                            m1_irqAllOn();
                            }
static void m1_finished     (bool tf)
                            {
                            twi1_lastResult_ = tf;
                            m1_NACKstop();
                            m1_irqAllOff();
                            if( twi1m_isrCallback_ ) twi1m_isrCallback_();
                            }
ISR(TWI1_TWIM_vect)         {
                            uint8_t s = m1_status();
                            if( s & ANYERR ) return m1_finished( false );
                            if( s == READOK ){
                                *rx1buf_++ = m1_read();
                                return rx1buf_ < rx1bufEnd_ ? m1_ACKread() : m1_finished( true );
                                }
                            if( s == WRITEOK ){
                                if( tx1buf_ < tx1bufEnd_ ) return m1_write( *tx1buf_++ );
                                if( tx1buf2_ < tx1buf2End_ ) return m1_write( *tx1buf2_++ );
                                return rx1buf_ ? m1_startRead() : m1_finished( true );
                                }
                            m1_finished( false );
                            }

void    twi1m_callback       (twim_callbackT cb) { twi1m_isrCallback_ = cb; }
void    twi1m_off            () { m1_off(); }
void    twi1m_on             (uint8_t addr) { m1_address(addr); m1_toStateIdle(); m1_on(); }
bool    twi1m_isBusy         () { return m1_isBusy(); }
bool    twi1m_lastResultOK   () { return twi1_lastResult_; }
void    twi1m_defaultPins    () { /*TWI1_PULL_DEFAULT(); */ TWI1_PORTMUX_DEFAULT(); }
#if defined(PORTB)
   void    twi1m_altPins        () { TWI1_PULL_ALT(); TWI1_PORTMUX_ALT(); }
#endif
void    twi1m_writeRead     (const uint8_t* wbuf, uint16_t wn, uint8_t* rbuf, uint16_t rn)
                            {
                            tx1buf_ = wbuf; tx1bufEnd_ = &wbuf[wn];
                            rx1buf_ = rbuf; rx1bufEnd_ = &rbuf[rn];
                            tx1buf2_ = 0; tx1buf2End_ = 0;
                            m1_startIrq( wn );
                            }
void    twi1m_writeWrite    (const uint8_t* wbuf, uint16_t wn, const uint8_t* wbuf2, uint16_t wn2)
                            {
                            tx1buf_ = wbuf; tx1bufEnd_ = &wbuf[wn];
                            tx1buf2_ = wbuf2; tx1buf2End_ = &wbuf2[wn2];
                            rx1buf_ = 0; rx1bufEnd_ = 0;
                            m1_startIrq( 1 );
                            }
void    twi1m_write         (const uint8_t* wbuf, uint16_t wn) { twi1m_writeRead( wbuf, wn, 0, 0); }
void    twi1m_read          (uint8_t* rbuf, uint16_t rn) { twi1m_writeRead( 0, 0, rbuf, rn); }
bool    twi1m_waitUS        (uint16_t us)
                            {
                            while( _delay_us(1), --us && twi1m_isBusy() ){}
                            return twi1m_lastResultOK();
                            }

static volatile uint8_t     s1_lastAddress_; 
static twis_callback_t      twi1s_isrCallback_;
static void s1_gencall      (uint8_t v) { TWI1.SADDR = (v<<1)|1; }
static void s1_mask         (uint8_t v, bool nomask) { TWI1.SADDRMASK = (v<<1)|nomask; }
static void s1_off          () { TWI1.SCTRLA &= ~1; }
static void s1_on           () { TWI1.SCTRLA |= 1; }
static uint8_t s1_read      () { return TWI1.SDATA; }
static void s1_write        (uint8_t v) { TWI1.SDATA = v; }
static void s1_irqAllOn     () { TWI1.SCTRLA |= 0xE0; }
static void s1_irqAllOff    () { TWI1.SCTRLA &= ~0xE0; }
static uint8_t s1_status    () { return TWI1.SSTATUS; }
static void s1_clearFlags   () { TWI1.SSTATUS = 0xCC; }
static void s1_nackComplete () { TWI1.SCTRLB = 6; }
static void s1_ack          () { TWI1.SCTRLB = 3; }
ISR                         (TWI1_TWIS_vect)
                            {
                            static bool is1st;
                            uint8_t s = s1_status(); 
                            twis_irqstate_t state = isError(s)     ? TWIS_ERROR :
                                                    isStop(s)      ? TWIS_STOPPED :
                                                    isAddress(s)   ? TWIS_ADDRESSED :
                                                    isDataRead(s)  ? TWIS_MREAD :
                                                    isDataWrite(s) ? TWIS_MWRITE : TWIS_ERROR;
                            bool nacked = isRxNack(s);
                            bool done = false;

                            if( state == TWIS_ADDRESSED ) {
                                s1_lastAddress_ = s1_read()>>1;
                                is1st = true;
                                }
                            else if( state == TWIS_MREAD ) {
                                if( is1st ) is1st = false; else done = nacked;
                                }
                            else if( state != TWIS_MWRITE ) done = true;

                            if( false == twi1s_isrCallback_(state, s) ) done = true;
                            if( done ) s1_nackComplete(); else s1_ack();
                            }
void twi1s_defaultPins      () { TWI1_PULL_DEFAULT(); TWI1_PORTMUX_DEFAULT(); }
#if defined(PORTB)
void    twi1s_altPins       () { TWI1_PULL_ALT(); TWI1_PORTMUX_ALT(); }
#endif
void twi1s_off              () { s1_irqAllOff(); s1_off(); s1_clearFlags(); }
void twi1s_write            (uint8_t v) { s1_write(v); }
uint8_t twi1s_read          () { return s1_read(); }
uint8_t twi1s_lastAddress   () { return s1_lastAddress_; }
void twi1s_mask             (uint8_t v) { s1_mask(v, true); }
void twi1s_addressMask      (uint8_t v) { s1_mask(v, false); }

void twi1s_init             (uint8_t addr, twis_callback_t cb)
                            {
                            if( ! cb ) return;
                            twi1s_off();
                            twi1s_isrCallback_ = cb;
                            s1_gencall( addr );
                            s1_irqAllOn();
                            s1_on();
                            }
#endif