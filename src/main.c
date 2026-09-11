#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/twi.h>

#include "mb.h"
#include "mbport.h"
#include "ext_bus.h"
#include "sys_timer.h"
#include "../avr-i2c-slave/I2CSlave.h"


#ifndef I2C_SLAVE_ADDRESS
#define I2C_SLAVE_ADDRESS 0x2e
#endif

#ifndef MODBUS_BAUD_RATE
#define MODBUS_BAUD_RATE 9600UL
#endif

#define MODBUS_SLAVE_ADDRESS 0x0a

#define REG_INPUT_START 1000
#define REG_INPUT_NREGS 40

#define REG_INPUT_BOOT_POLL_CNT_OFFSET      0
#define REG_INPUT_NORMAL_POLL_CNT_OFFSET    1
#define REG_INPUT_POLL_ITERATION            2
#define REG_INPUT_EXT_REG_START_OFFSET      10

#define NUM_EXT_REGS 4

static uint8_t poll_reg_cnt = 0;
static uint8_t polled_regs[] = { EXT_REG_OUTDOOR_TEMP, EXT_REG_BRINEIN_TEMP,EXT_REG_BRINEOUT_TEMP, EXT_REG_HOTWATER_TEMP };
static uint8_t ext_bus_current_reg;

enum ext_bus_state { EXT_BUS_REG_ACCESS, EXT_BUS_EXPECT_REG, EXT_BUS_DATA_ACCESS_LO, EXT_BUS_DATA_ACCESS_HI };
enum ext_bus_state bus_state = EXT_BUS_REG_ACCESS;

static volatile USHORT usRegInputBuf[REG_INPUT_NREGS];

volatile uint8_t i2c_out_byte;
volatile uint8_t ext_bus_reg_byte_lo;
volatile uint8_t ext_bus_reg_byte_hi;

void I2C_received(uint8_t received_data) {

    switch (bus_state) {
    default:
    case EXT_BUS_REG_ACCESS:
        switch (received_data) {
        case EXT_CMD_PING_QUERY_BOOT:
            i2c_out_byte = EXT_REPLY_PING_ACK;
            usRegInputBuf[REG_INPUT_BOOT_POLL_CNT_OFFSET]++;
            break;
        case EXT_CMD_PING_QUERY:
            if (poll_reg_cnt < NUM_EXT_REGS) {
                ext_bus_current_reg = polled_regs[poll_reg_cnt];
                i2c_out_byte = ext_bus_current_reg;
                poll_reg_cnt++;
                bus_state = EXT_BUS_EXPECT_REG;
            }
            else {
                i2c_out_byte = EXT_REPLY_PING_ACK;
                usRegInputBuf[REG_INPUT_NORMAL_POLL_CNT_OFFSET]++;
            }
            break;
        default:
            // This is entered when a normal EXT BUS register is written to
            ext_bus_current_reg = received_data;
            bus_state = EXT_BUS_DATA_ACCESS_LO;
            break;
        }
        break;
    case EXT_BUS_EXPECT_REG:
        if( received_data == ext_bus_current_reg )
        {
            bus_state = EXT_BUS_DATA_ACCESS_LO;
        }
        else
        {
            /* An unexpected selector aborts this advertised data transfer. */
            bus_state = EXT_BUS_REG_ACCESS;
        }
        bus_state = EXT_BUS_DATA_ACCESS_LO;
        break;
    case EXT_BUS_DATA_ACCESS_LO:
        ext_bus_reg_byte_lo = received_data;
        bus_state = EXT_BUS_DATA_ACCESS_HI;
        break;
    case EXT_BUS_DATA_ACCESS_HI:
        ext_bus_reg_byte_hi = received_data;
        bus_state = EXT_BUS_REG_ACCESS;

        usRegInputBuf[REG_INPUT_EXT_REG_START_OFFSET + ext_bus_current_reg] =
            ( ( USHORT )ext_bus_reg_byte_hi << 8 ) | ext_bus_reg_byte_lo;
        
        break;
    }
}

void I2C_requested() {
  I2C_transmitByte(i2c_out_byte);
}

int main(void)
{
    eMBErrorCode status;

    sys_timer_init();

    status = eMBInit(MB_RTU, MODBUS_SLAVE_ADDRESS, 0,
                     MODBUS_BAUD_RATE, MB_PAR_EVEN, 1);
    
    if( status != MB_ENOERR )
    {
        for( ;; )
        {
            /* Initialization failure is terminal until diagnostics are added. */
        }
    }

   
    I2C_setCallbacks(I2C_received, I2C_requested);
    I2C_init(I2C_SLAVE_ADDRESS);

    status = eMBEnable();
    if( status != MB_ENOERR )
    {
        for( ;; )
        {
            /* Initialization failure is terminal until diagnostics are added. */
        }
    }

    for( ;; )
    {
        (void)eMBPoll();
        static sys_time_t last_poll_time;
        if (sys_timer_now() > last_poll_time + 10000) {
            last_poll_time = sys_timer_now();
            poll_reg_cnt = 0;

            static int iteration;
            usRegInputBuf[REG_INPUT_POLL_ITERATION] = ++iteration;
        }
    }
}

eMBErrorCode
eMBRegInputCB(UCHAR *pucRegBuffer, USHORT usAddress, USHORT usNRegs)
{
    int reg_index;

    if( ( usAddress < REG_INPUT_START ) ||
        ( usAddress + usNRegs > REG_INPUT_START + REG_INPUT_NREGS ) )
    {
        return MB_ENOREG;
    }

    reg_index = (int)(usAddress - REG_INPUT_START);
    while( usNRegs > 0 )
    {
        *pucRegBuffer++ = (UCHAR)(usRegInputBuf[reg_index] >> 8);
        *pucRegBuffer++ = (UCHAR)(usRegInputBuf[reg_index] & 0xff);
        reg_index++;
        usNRegs--;
    }

    return MB_ENOERR;
}

eMBErrorCode
eMBRegHoldingCB(UCHAR *pucRegBuffer, USHORT usAddress, USHORT usNRegs,
                eMBRegisterMode eMode)
{
    (void)pucRegBuffer;
    (void)usAddress;
    (void)usNRegs;
    (void)eMode;
    return MB_ENOREG;
}

eMBErrorCode
eMBRegCoilsCB(UCHAR *pucRegBuffer, USHORT usAddress, USHORT usNCoils,
              eMBRegisterMode eMode)
{
    (void)pucRegBuffer;
    (void)usAddress;
    (void)usNCoils;
    (void)eMode;
    return MB_ENOREG;
}

eMBErrorCode
eMBRegDiscreteCB(UCHAR *pucRegBuffer, USHORT usAddress, USHORT usNDiscrete)
{
    (void)pucRegBuffer;
    (void)usAddress;
    (void)usNDiscrete;
    return MB_ENOREG;
}
