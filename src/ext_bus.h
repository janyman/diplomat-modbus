#pragma once

#define EXT_CMD_PING_QUERY_BOOT     0xfd
#define EXT_CMD_PING_QUERY          0xfe

#define EXT_REPLY_PING_ACK          0xff

#define EXT_REG_OUTDOOR_TEMP        0x00
#define EXT_REG_INDOOR_TEMP        0x01
#define EXT_REG_INDOOR_DEC_TEMP        0x02
#define EXT_REG_TARGET_TEMP        0x03
#define EXT_REG_TARGET_DEC_TEMP        0x04
#define EXT_REG_SUPPLYLINE_TEMP        0x05
#define EXT_REG_RETURNLINE_TEMP        0x06
#define EXT_REG_HOTWATER_TEMP        0x07
#define EXT_REG_BRINEOUT_TEMP        0x08
#define EXT_REG_BRINEIN_TEMP        0x09
#define EXT_REG_COOLING_TEMP        0x0a
#define EXT_REG_SUPPLYLINE_SHUNT    0x0b
#define EXT_REG_ELECTRICAL_CURRENT  0x0c
