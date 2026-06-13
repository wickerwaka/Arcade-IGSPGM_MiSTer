/* Standalone MDFourier signal page.
 *
 * Plays the MDFourier test signal once on boot using the Z80 timer-IRQ
 * sequencer (the same path util/mdfourier/run.py drives over debug-link), so
 * the board can be captured without a host.  The script, voice template and
 * timer values are generated from util/mdfourier/signal.py into
 * mdfourier_script.h (regenerate with: make_profile.py --c-header).
 *
 * For the actual hardware-vs-sim comparison use PAGE=ics_remote + run.py; this
 * page is a self-contained smoke test / capture source. */
#include <stddef.h>

#include "../system.h"
#include "../page.h"
#include "../tilemap.h"
#include "../igs023.h"
#include "../color.h"
#include "../z80_ics_host.h"
#include "../z80_ics_protocol.h"
#include "../util.h"
#include "mdfourier_script.h"

static u8 started;
static u8 driver_ok;
static u16 driver_magic;

static void mdf_build_voice(z80_ics_voice_t *v)
{
    memset(v, 0, sizeof(*v));
    v->osc_conf = MDF_OSC_CONF;
    v->osc_fc = 0x4444;                 /* overwritten by script entry 0 */
    v->osc_start_hi = (u16)((MDF_LOOP_START >> 4) & 0xffff);
    v->osc_start_lo = (u8)((MDF_LOOP_START & 0xf) << 4);
    v->osc_end_hi = (u16)((MDF_LOOP_END >> 4) & 0xffff);
    v->osc_end_lo = (u8)((MDF_LOOP_END & 0xf) << 4);
    set_osc_acc(v, MDF_LOOP_START);     /* sets osc_acc_hi/lo + osc_saddr=0x40 */
    v->osc_saddr = MDF_OSC_SADDR;
    v->vol_acc = 0xffff;
    v->vol_start = 0xff;
    v->vol_end = 0xff;
    v->vol_incr = 0x00;
    v->vol_ctrl = 0x00;
    v->pan = MDF_PAN_CENTER;
    v->osc_ctl = 0x0f;                  /* start stopped; entry 0 keys it on */
    v->vmode = 0x00;
}

static void mdf_run(void)
{
    z80_ics_voice_t v;
    mdf_build_voice(&v);
    z80_ics_write_voice(0, &v);
    z80_ics_mdf_load_chunk(0, mdf_script_data, (u16)sizeof(mdf_script_data));
    z80_ics_mdf_load(MDF_SCRIPT_COUNT);
    z80_ics_mdf_start(MDF_TIMER_SCALE0, MDF_TIMER_PRESET0);
    started = 1;
}

static void init(void)
{
    igs023_init();
    text_reset();
    set_default_palette();
    IGS023_BG_CTRL_SET(0xffff);
    IGS023_FG_X_SET(8);
    IGS023_FG_Y_SET(8);

    started = 0;
    driver_magic = 0;
    z80_ics_init();
    driver_ok = z80_ics_ready() && z80_ics_ping(&driver_magic);
    if (driver_ok)
        mdf_run();
}

static void update(void)
{
    u8 running = 0;
    u16 index = 0;

    igs023_wait_vblank();
    if (started)
        z80_ics_mdf_status(&running, &index);

    text_color(1);
    text_cursor(2, 2);
    text("MDFOURIER\n");
    textf("DRV %04X RDY %04X\n", driver_magic, z80_ics_ready());
    textf("COUNT %04X\n", MDF_SCRIPT_COUNT);
    textf("RUNNING %02X INDEX %04X\n", running, index);
    text(running ? "PLAYING\n" : (started ? "DONE\n" : "IDLE\n"));
}

PAGE_REGISTER(mdfourier, init, update, NULL);
