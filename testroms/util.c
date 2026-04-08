#include <stddef.h>

// from linux kernel
void *memset(void *s, int c, size_t count)
{
	void *xs = s;
	size_t temp;
	if (!count)
		return xs;
	c &= 0xff;
	c |= c << 8;
	c |= c << 16;
	if ((long)s & 1) {
		char *cs = s;
		*cs++ = c;
		s = cs;
		count--;
	}
	if (count > 2 && (long)s & 2) {
		short *ss = s;
		*ss++ = c;
		s = ss;
		count -= 2;
	}
	temp = count >> 2;
	if (temp) {
		long *ls = s;
		for (; temp; temp--)
			*ls++ = c;
		s = ls;
	}
	if (count & 2) {
		short *ss = s;
		*ss++ = c;
		s = ss;
	}
	if (count & 1) {
		char *cs = s;
		*cs = c;
	}
	return xs;
}

// from linux kernel
void *memcpy(void *to, const void *from, size_t n)
{
	void *xto = to;
	size_t temp;
	if (!n)
		return xto;
	if ((long)to & 1) {
		char *cto = to;
		const char *cfrom = from;
		*cto++ = *cfrom++;
		to = cto;
		from = cfrom;
		n--;
	}
	if ((long)from & 1) {
		char *cto = to;
		const char *cfrom = from;
		for (; n; n--)
			*cto++ = *cfrom++;
		return xto;
	}
	if (n > 2 && (long)to & 2) {
		short *sto = to;
		const short *sfrom = from;
		*sto++ = *sfrom++;
		to = sto;
		from = sfrom;
		n -= 2;
	}
	temp = n >> 2;
	if (temp) {
		long *lto = to;
		const long *lfrom = from;
		for (; temp; temp--)
			*lto++ = *lfrom++;
		to = lto;
		from = lfrom;
	}
	if (n & 2) {
		short *sto = to;
		const short *sfrom = from;
		*sto++ = *sfrom++;
		to = sto;
		from = sfrom;
	}
	if (n & 1) {
		char *cto = to;
		const char *cfrom = from;
		*cto = *cfrom;
	}
	return xto;
}
