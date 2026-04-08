#include "crc16.h"

uint16_t crc16(uint16_t crcValue, uint8_t newByte) 
{
	unsigned char i;

	for (i = 0; i < 8; i++) {

		if (((crcValue & 0x8000) >> 8) ^ (newByte & 0x80)){
			crcValue = (crcValue << 1)  ^ POLYNOM;
		}else{
			crcValue = (crcValue << 1);
		}

		newByte <<= 1;
	}
  
	return crcValue;
}

uint16_t crc16_block16(uint16_t *data, int length)
{
    uint16_t crc = 0xffff;

    for (int i = 0; i < length; i++)
    {
        uint16_t v = data[i];
        crc = crc16(crc, v & 0xff);
        crc = crc16(crc, (v >> 8) & 0xff);
    }
    return crc;
}

