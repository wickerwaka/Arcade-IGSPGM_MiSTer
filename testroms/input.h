#if !defined(INPUT_H)
#define INPUT_H 1

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    START = 0x0001,
    UP    = 0x0002,
    DOWN  = 0x0004,
    LEFT  = 0x0008,
    RIGHT = 0x0010,
    BTN1  = 0x0020,
    BTN2  = 0x0040,
    BTN3  = 0x0080,
} InputKey;

void input_init();
void input_update();
uint16_t input_state();
bool input_down(InputKey key);
bool input_released(InputKey key);
bool input_pressed(InputKey key);

uint16_t input_dsw();
#endif // !defined(INPUT_H)
