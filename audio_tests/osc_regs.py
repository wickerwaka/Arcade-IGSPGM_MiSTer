from util.ics2115_remote import (ICS2115Remote, WIDTH_UPPER8, WIDTH_LOWER8, WIDTH_16)

ics = ICS2115Remote.open("pgm", reset="l")

print(ics.read_voice(0))
print(ics.read_voice(1))
print(ics.read_voice(2))

ics.write_reg(0, "vol_start", 0xf0, WIDTH_UPPER8)
print(hex(ics.read_reg(0, "vol_start", WIDTH_UPPER8)))

