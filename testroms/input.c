#include "input.h"

#include "system.h"

static uint16_t s_prev = 0;
static uint16_t s_cur = 0;
static uint16_t s_dsw = 0;
static uint16_t s_count = 0;

volatile u16 *IN_P1P2 = (volatile u16 *)0xc08000;
volatile u16 *IN_DSW  = (volatile u16 *)0xc08006;

void input_init()
{
    *IN_DSW  = 0;
}

void input_update()
{
    if (s_cur == s_prev && s_cur != 0xffff)
    {
        s_count++;
    }
    else
    {
        s_count = 0;
    }

    s_prev = s_cur;
    s_cur = *IN_P1P2;
    s_dsw = *IN_DSW;

    if (s_count == 5)
    {
        s_cur = 0xffff;
        s_count = 0;
    }
}

uint16_t input_state()
{
    return s_cur;
}

bool input_down(InputKey key)
{
    return (s_cur & key) != key;
}

bool input_released(InputKey key)
{
    return ((s_cur & key) != 0) && (((s_prev ^ s_cur) & key) != 0);
}

bool input_pressed(InputKey key)
{
    return ((s_cur & key) != key) && (((s_prev ^ s_cur) & key) != 0);
}

uint16_t input_dsw()
{
    return s_dsw;
}
