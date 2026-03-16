#if !defined( DIS68K_H )
#define DIS68K_H 1

#include <stdint.h>
#include <stdlib.h>

class Dis68k
{
public:
	Dis68k(const void *_begin, const void *_end, uint32_t _address)
	{
		begin = (const uint8_t *)_begin;
		end = (const uint8_t *)_end;
		cur = begin;
		overflow = false;
		address = _address;
	}

	bool disasm(uint32_t *inst_address, char *decoded_str, size_t decoded_len);

	uint32_t getaddress() { return address; }

private:
	uint8_t getbyte()
	{
		if( cur < end )
		{
			uint8_t res = 0;
			res = *cur;
			cur++;
			address++;
			return res;
		}

		overflow = true;
		return 0;
	}

	uint16_t getword()
	{
		if( cur < ( end - 1 ) )
		{
			uint16_t res = (cur[1] << 8) | cur[0];
			cur += 2;
			address += 2;
			return res;
		}

		overflow = true;

		return 0;
	}


	void sprintmode(unsigned int mode, unsigned int reg, unsigned int size, char *out_s, int out_sz);

	const uint8_t *begin;
	const uint8_t *end;
	const uint8_t *cur;
	uint32_t address;
	bool overflow;
};

#endif // DIS68K_H
