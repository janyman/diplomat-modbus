#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/twi.h>

#include "mb.h"
#include "mbport.h"
#include "ext_bus.h"

#ifndef I2C_SLAVE_ADDRESS
#define I2C_SLAVE_ADDRESS 0x2e
#endif

#ifndef MODBUS_BAUD_RATE
#define MODBUS_BAUD_RATE 9600UL
#endif

#define MODBUS_SLAVE_ADDRESS 0x0a

#define REG_INPUT_START 1000
#define REG_INPUT_NREGS 4

static volatile uint8_t i2c_register;
static volatile uint8_t i2c_response = 0xff;
static volatile uint8_t i2c_write_pending;

static USHORT usRegInputBuf[REG_INPUT_NREGS];

static void
i2c_slave_init(void)
{
    /* TWAR stores the seven-bit slave address in bits 7..1. */
    TWAR = (uint8_t)(I2C_SLAVE_ADDRESS << 1);

    /* Enable TWI, its interrupt, and ACK all addressed transactions. */
    TWCR = _BV(TWIE) | _BV(TWEA) | _BV(TWEN) | _BV(TWINT);
}

static void
i2c_handle_write_event(void)
{
    uint8_t reg;

    if( i2c_write_pending == 0 )
    {
        return;
    }

    /* Keep the critical section short: the TWI ISR may update these values. */
    cli();
    reg = i2c_register;
    i2c_write_pending = 0;
    sei();

    switch( reg )
    {
        //case 0xfd:
        case EXT_CMD_PING_QUERY:
            i2c_response = EXT_REPLY_PING_ACK;
            usRegInputBuf[1]++;
            break;

        default:
            /* Unknown registers currently return the same placeholder value. */
            //i2c_response = EXT_REPLY_PING_ACK;
            break;
    }
}

ISR(TWI_vect)
{
    switch( TW_STATUS )
    {
        case TW_SR_SLA_ACK:
        case TW_SR_DATA_ACK:
            /* The register address is the last byte written by the master. */
            i2c_register = TWDR;
            i2c_write_pending = 1;
            TWCR = _BV(TWIE) | _BV(TWEA) | _BV(TWEN) | _BV(TWINT);
            break;

        case TW_ST_SLA_ACK:
            TWDR = i2c_response;
            TWCR = _BV(TWIE) | _BV(TWEA) | _BV(TWEN) | _BV(TWINT);
            break;

        case TW_ST_DATA_ACK:
            /* If the master asks for more than one byte, keep returning 0xff. */
            TWDR = 0xff;
            TWCR = _BV(TWIE) | _BV(TWEA) | _BV(TWEN) | _BV(TWINT);
            break;

        case TW_SR_STOP:
        case TW_ST_DATA_NACK:
        case TW_ST_LAST_DATA:
        default:
            TWCR = _BV(TWIE) | _BV(TWEA) | _BV(TWEN) | _BV(TWINT);
            break;
    }
}

int
main(void)
{
    eMBErrorCode status;

    status = eMBInit(MB_RTU, MODBUS_SLAVE_ADDRESS, 0,
                     MODBUS_BAUD_RATE, MB_PAR_EVEN, 1);
    if( status != MB_ENOERR )
    {
        for( ;; )
        {
            /* Initialization failure is terminal until diagnostics are added. */
        }
    }

    i2c_slave_init();

    status = eMBEnable();
    if( status != MB_ENOERR )
    {
        for( ;; )
        {
            /* Initialization failure is terminal until diagnostics are added. */
        }
    }

    sei();

    for( ;; )
    {
        (void)eMBPoll();
        i2c_handle_write_event();
        usRegInputBuf[0]++;
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
