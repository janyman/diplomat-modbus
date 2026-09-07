# Modbus for Thermia Diplomat geothermal heat pumps

## The Ext.COM bus

The Ext.COM bus connector is labelled "EXT" on the Control unit board inside the heat pump enclosure. The connector is a 4-way pin header with 2.54 mm pitch.

Electrically, the EXT bus is I2C with 5-volt signal levels, and running at 400 kHz clock. The pin assignment is the following, starting from the topmost pin:

Vcc +5V
SCL
SDA
GND

In the I2C bus, the heat pump is the bus master. It periodically polls expansion cards on the bus by sendind commands, and the slave responds with a request byte or a place-holder value. For example, the master sends to slave 0x2e the command 0xfe, and the slave has the possibility to reply with register number, or the special value 0xff, if slave does not have a request.


## References

EXT bus secrets revealed: https://omakotikotitalomme.blogspot.com/2015/03/danfoss-lampopumpun-salaisuudet.html
Registers: https://thermiq.net/ThermIQ_MQTT_Installation.pdf