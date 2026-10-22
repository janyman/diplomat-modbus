#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/twi.h>
#include <util/atomic.h>


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


#define REG_INPUT_BOOT_POLL_CNT_OFFSET      0
#define REG_INPUT_NORMAL_POLL_CNT_OFFSET    1
#define REG_INPUT_POLL_ITERATION            2
#define REG_INPUT_BUS_ERROR_CNT             3
#define REG_INPUT_BUS_STATE_GLITCH_CNT      4
#define REG_INPUT_EXT_REG_START_OFFSET      10

#define REG_INPUT_NREGS (REG_INPUT_EXT_REG_START_OFFSET + EXT_REG_MAX_REG_ADDRESS + 1)
#define NUM_EXT_REGS 31

static uint8_t poll_reg_cnt = 0;
static uint8_t polled_regs[] = { 
    EXT_REG_OUTDOOR_TEMP,
    EXT_REG_TARGET_TEMP,
    EXT_REG_SUPPLYLINE_TEMP,
    EXT_REG_RETURNLINE_TEMP,
    EXT_REG_HOTWATER_TEMP,
    EXT_REG_BRINEIN_TEMP,
    EXT_REG_BRINEOUT_TEMP,
    EXT_REG_SUPPLYLINE_SHUNT,
    EXT_REG_AUX_HEATER_STATUS_BITMAP,
    EXT_REG_SUPPLYLINE_TARGET_TEMP,
    EXT_REG_SUPPLYLINE_TARGET_TEMP_SHUNT,
    EXT_REG_STATUS1,
    EXT_REG_STATUS2,
    EXT_REG_PWM_OUT,
    EXT_REG_ALARM1,
    EXT_REG_ALARM2,
    EXT_REG_PRESSURE_PIPE_TEMP,
    EXT_REG_HOT_WATER_SUPPLY_LIME_TEMP,
    EXT_REG_INTEGRAL,
    EXT_REG_INTEGRAL_REACHED_A_LIMIT,
    EXT_REG_PROGRAM_VERSION,
    EXT_REG_FLOWLINE_PUMP_SPEED,
    EXT_REG_BRINE_PUMP_SPEED,
    EXT_REG_RUNTIME_COMPRESSOR,
    EXT_REG_RUNTIME_COMPRESSOR_LOW_RES,
    EXT_REG_RUNTIME_3KW,
    EXT_REG_RUNTIME_3KW_LOW_RES,
    EXT_REG_RUNTIME_HOTWATER,
    EXT_REG_RUNTIME_HOTWATER_LOW_RES,
    EXT_REG_RUNTIME_6KW,
    EXT_REG_RUNTIME_6KW_LOW_RES,
};
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
        break;
    case EXT_BUS_DATA_ACCESS_LO:
        ext_bus_reg_byte_lo = received_data;
        bus_state = EXT_BUS_DATA_ACCESS_HI;
        break;
    case EXT_BUS_DATA_ACCESS_HI:
        ext_bus_reg_byte_hi = received_data;
        bus_state = EXT_BUS_REG_ACCESS;

        if (ext_bus_current_reg <= EXT_REG_MAX_REG_ADDRESS) {
            usRegInputBuf[REG_INPUT_EXT_REG_START_OFFSET + ext_bus_current_reg] =
                ( ( USHORT )ext_bus_reg_byte_hi << 8 ) | ext_bus_reg_byte_lo;
        }
        
        break;
    }
}

void I2C_requested() {
  I2C_transmitByte(i2c_out_byte);
}

void I2C_transaction_end() {
    // Ensure coherent EXT BUS state, as the I2C transaction is now finished
    switch (bus_state) {
        case EXT_BUS_REG_ACCESS:
        case EXT_BUS_EXPECT_REG:
            // these are valid ext bus states states when i2c transaction ends
            break;
        case EXT_BUS_DATA_ACCESS_LO:
        case EXT_BUS_DATA_ACCESS_HI:
            // If transaction ends while we are in these states, then we have missed something and we have an incoherent state
            bus_state = EXT_BUS_REG_ACCESS;
            break;
    }
}

void I2C_transaction_abort() {
    /* A bus error invalidates even an otherwise valid pending query. */
    bus_state = EXT_BUS_REG_ACCESS;

    usRegInputBuf[REG_INPUT_BUS_ERROR_CNT]++;
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
    I2C_setTransactionEndCallback(I2C_transaction_end);
    I2C_setTransactionAbortCallback(I2C_transaction_abort);
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

    USHORT value;
    reg_index = (int)(usAddress - REG_INPUT_START);

    while( usNRegs > 0 )
    {
        // Ensure that the 16-bit entity is properly read, as it is accessed both from ISR context and main loop context.
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            value = usRegInputBuf[reg_index];
        }

        *pucRegBuffer++ = (UCHAR)(value >> 8);
        *pucRegBuffer++ = (UCHAR)(value & 0xff);
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
