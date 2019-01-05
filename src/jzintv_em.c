/*
 * ============================================================================
 *  Title:    MAIN
 *  Author:   J. Zbiciak
 * ============================================================================
 *  Main Simulator Driver File
 * ============================================================================
 *  This doesn't do much yet.  :-)
 * ============================================================================
 */

#include "config.h"

#include <signal.h>
#include "plat/plat.h"
#include "lzoe/lzoe.h"
#include "file/file.h"
#include "periph/periph.h"
#include "cp1600/cp1600.h"
#include "mem/mem.h"
#include "icart/icart.h"
#include "bincfg/bincfg.h"
#include "bincfg/legacy.h"
#include "pads/pads.h"
#include "pads/pads_cgc.h"
#include "pads/pads_intv2pc.h"
#include "avi/avi.h"
#include "gfx/gfx.h"
#include "snd/snd.h"
#include "ay8910/ay8910.h"
#include "demo/demo.h"
#include "stic/stic.h"
#include "speed/speed.h"
#include "debug/debug_.h"
#include "event/event.h"
#include "ivoice/ivoice.h"
#include "jlp/jlp.h"
#include "locutus/locutus_adapt.h"
#include "cfg/mapping.h"
#include "cfg/cfg.h"
#include "emscripten/web_files.h"

#include <emscripten/html5.h>

cfg_t intv;

double elapsed(int);

/*
 * ============================================================================
 *  RELEASE      -- Program name / release
 * ============================================================================
 */
static char *release(void)
{
    static char buf[16];

    snprintf(buf, 16, "%d.%d", JZINTV_VERSION_MAJOR, JZINTV_VERSION_MINOR);

    return buf;
}

#if 1
/*
 * ============================================================================
 *  CART_NAME    -- Look for a game name in a cartridge image.
 * ============================================================================
 */
static const char *cart_name(void)
{
    static char name_buf[64];
    uint_16 title_addr, lo, hi, ch;
    int year;
    int i, ps;
    const char *base_name;
    char *s1, *s2;

    if ((base_name = intv.cart_name) != NULL)
    {
        year = intv.cart_year;
        i = 0;
        goto got_name;
    }

    if ((base_name = strrchr(intv.fn_game, '/')) == NULL &&
        (base_name = strrchr(intv.fn_game, '\\')) == NULL)
        base_name = intv.fn_game;
    else
        base_name++;


    lo = periph_peek((periph_p)intv.intv, (periph_p)intv.intv, 0x500A, ~0);
    hi = periph_peek((periph_p)intv.intv, (periph_p)intv.intv, 0x500B, ~0);

    if ((lo | hi) & 0xFF00)
        return base_name;

    title_addr = ((hi << 8) | lo);

    year = 1900 + periph_peek((periph_p)intv.intv,
                              (periph_p)intv.intv, title_addr, ~0);

    if (year < 1977 || year > 2050)
        return base_name;

    for (i = 0; i < 64 - 8; i++)
    {
        ch = periph_peek((periph_p)intv.intv,
                         (periph_p)intv.intv, title_addr + i + 1, ~0);

        name_buf[i] = ch;

        if (ch == 0)
            break;

        if (ch < 32 || ch > 126)
            return base_name;
    }

    ps = 1;
    i  = 0;
    for (s1 = s2 = name_buf; *s1; s1++)
        if (!isspace(*s1) || !ps)
        {
            *s2++ = *s1;
            ps = isspace(*s1);
            if (!ps)
                i = s2 - name_buf;
        }

got_name:
    if (i == 0)
    {
        strncpy(name_buf, base_name, 64-8);
        name_buf[64-8] = 0;
        i = strlen(name_buf);
    }

    if (year >= 1977 && year <= 2050)
        snprintf(name_buf + i, 8, " (%4d)", year);

    return name_buf;
}
#endif

/*
 * ============================================================================
 *  ELAPSED      -- Returns amount of time that's elapsed since the program
 *                  started, in CP1610 clock cycles (895kHz)
 * ============================================================================
 */
double elapsed(int restart)
{
    static double start;
    static int init = 0;
    double now;

    if (!init || restart)
    {
        start = get_time();
        init = 1;
    }

    now = get_time();

    return (now - start) * (intv.pal_mode ? 1000000. : 894886.25);
}

LOCAL char *fixed_argv[] =
{
    "jzintv",
    "-z640x480x32",
    "-e", "miniexec.bin",
    "-g", "minigrom.bin",
    "-E", "fake_ecs.bin",
    "-s1", 
    "-r0",  /* Cannot use jzIntv's typical rate control */
    "-a44100",
    "game.bin",
    NULL
};

LOCAL int fixed_argc = sizeof(fixed_argv) / sizeof(fixed_argv[0]);

enum jzintv_state
{
    JZINTV_WAITCLICK,
    JZINTV_RELOAD,
    JZINTV_RESTART,
    JZINTV_RUNNING
};

LOCAL int saw_click = 0;
LOCAL enum jzintv_state jzintv_state = JZINTV_WAITCLICK;
LOCAL void jzintv_state_machine(void);
LOCAL EM_BOOL start_on_click_mouse(int eventType, 
                                   const EmscriptenMouseEvent *mouseEvent, 
                                   void *userData);
LOCAL EM_BOOL start_on_click_touch(int eventType, 
                                   const EmscriptenTouchEvent *touchEvent, 
                                   void *userData);

/*
 * ============================================================================
 *  In the beginning, there was a main....
 * ============================================================================
 */
int jzintv_entry_point(int argc, char *argv[])
{
    UNUSED(argc);
    UNUSED(argv);

    lzoe_register("$LZOE$/", lzoe_web_files);

    /* -------------------------------------------------------------------- */
    /*  Platform-specific initialization.                                   */
    /* -------------------------------------------------------------------- */
    if (plat_init())
    {
        fprintf(stderr, "Error initializing.\n");
        exit(1);
    }

    jzintv_state = JZINTV_WAITCLICK;
    saw_click = 0;
    emscripten_set_mousedown_callback(0, 0, 0, start_on_click_mouse);
    emscripten_set_touchstart_callback(0, 0, 0, start_on_click_touch);
    emscripten_set_main_loop(jzintv_state_machine, -60, 0);


    return 0;
}

void jzintv_state_machine(void)
{
    static int iter = 0;
    static double cycles = 0, rate, irate, then, now, icyc;
    static double next_cycles = 14934;
    static double disp_time, reset_time, curr_time;
    static uint_32 s_cnt = 0;
    static int paused = 0;
    static char title[128];
    static int inited = 0;

    if (!inited)
    {
        jzp_printf("Click window to start.\n");
        jzp_flush();
        inited = 1;
    }

    /* -------------------------------------------------------------------- */
    /*  Parse our arguments and go get an Intellivision!                    */
    /* -------------------------------------------------------------------- */
    switch (jzintv_state)
    {
      case JZINTV_WAITCLICK:
      {
        if (saw_click)
          jzintv_state = JZINTV_RELOAD;
        return;
      }
      case JZINTV_RELOAD:
      {
        disp_time = get_time();
        reset_time = disp_time;

        memset(&intv, 0, sizeof(intv));

        cfg_init(&intv, fixed_argc, fixed_argv);
        init_disp_width(0);
        jzp_flush();

        /* ---------------------------------------------------------------- */
        /*  Set the window title. If we recognize a standard Intellivision  */
        /*  ROM header at 0x5000, then also include the cartridge name.     */
        /* ---------------------------------------------------------------- */
        #if 1
        snprintf(title, 128, "jzintv %.32s : %.64s", release(), cart_name());
        title[127] = 0;
        gfx_set_title(&intv.gfx,title);
        #endif

        /* ---------------------------------------------------------------- */
        /*  Run the simulator.                                              */
        /* ---------------------------------------------------------------- */
        if (intv.debugging)
            debug_tk((periph_p)&(intv.debug),1);

        jzp_printf("Starting jzIntv...\n");
        jzp_flush();

        if (intv.start_dly > 0)
            plat_delay(intv.start_dly);

        jzintv_state = JZINTV_RESTART;
        return;
      }

      case JZINTV_RESTART:
      {
        iter = 1;
        now = elapsed(1);
        now = elapsed(0); 

        icyc   = 0;
        s_cnt  = 0;
        cycles = 0;
        speed_resync(&(intv.speed));

        if (!intv.debugging)
            intv.debug.step_count = ~0U;

        paused = 0;
        jzintv_state = JZINTV_RUNNING;
        return;
      }

      case JZINTV_RUNNING:
      {
        if (cycles >= next_cycles && cycles - next_cycles > 5)
        {
            jzp_printf("cycles ahead: %lld %lld\n", (long long)cycles,
                        (long long)next_cycles);
        }
            
        while (cycles >= next_cycles)
            next_cycles += 14934;

        while (cycles < next_cycles) {
            uint_64 max_step = 5;
            int do_reset = intv.do_reset;

            if (intv.do_reload)
            {
                jzintv_state = JZINTV_RELOAD;
                return;
            }

            if (do_reset)
            {
                if (intv.do_reset == 2)
                    intv.do_reset = 0;
                max_step = 1000; /* arbitrary */
                intv.gfx.scrshot |= GFX_RESET;
            } else
            {
                if (s_cnt)
                {
                    s_cnt = 0;
                    periph_reset(intv.intv);
                }
                /* This is incredibly hackish, and is an outgrowth of my
                 * decoupled tick architecture.  */
                if (intv.cp1600.req_q.horizon > intv.cp1600.periph.now)
                    max_step = intv.cp1600.req_q.horizon - intv.cp1600.periph.now;
                else
                    max_step = 5;
                if (max_step > 20000) max_step = 20000;
                if (cycles + max_step > next_cycles)
                    max_step = next_cycles - cycles;
                if (max_step < 5) max_step = 5;
            }

    #if 0
    jzp_printf("cpu.now = %-8d  stic.now = %-8d diff = %-8d step = %-8d\n", (int)intv.cp1600.periph.now, (int)intv.stic.stic_cr.now, (int)intv.cp1600.periph.now-(int)intv.stic.stic_cr.now, (int)max_step);
    #endif

            if (intv.do_pause)
            {
                paused = !paused; intv.do_pause = 0;
                if (!paused)
                    speed_resync(&(intv.speed));
                return;
            }

            if (intv.event.change_kbd)
            {
                if (intv.event.change_kbd == 5)
                {
                    intv.event.cur_kbd = (intv.event.cur_kbd + 1) & 3;
                } else
                if (intv.event.change_kbd == 6)
                {
                    intv.event.cur_kbd = (intv.event.cur_kbd - 1) & 3;
                } else
                if (intv.event.change_kbd == 7)
                {
                    if (intv.event.prv_kbd)
                    {
                        intv.event.cur_kbd = intv.event.prv_kbd - 1;
                        intv.event.prv_kbd = 0;
                    }
                } else
                if (intv.event.change_kbd >= 8 && intv.event.change_kbd < 12)
                {
                    intv.event.prv_kbd = intv.event.cur_kbd + 1;
                    intv.event.cur_kbd = (intv.event.change_kbd - 8) & 3;
                } else
                {
                    intv.event.cur_kbd = (intv.event.change_kbd - 1) & 3;
                }

                jzp_clear_and_eol(
                    jzp_printf("Change keyboard to %d", intv.event.cur_kbd));
                jzp_flush();
                intv.event.change_kbd = 0;
                memset(intv.pad0.l, 0, sizeof(intv.pad0.l));
                memset(intv.pad0.r, 0, sizeof(intv.pad0.r));
                memset(intv.pad0.k, 0, sizeof(intv.pad0.k));
                memset(intv.pad1.l, 0, sizeof(intv.pad1.l));
                memset(intv.pad1.r, 0, sizeof(intv.pad1.r));
                memset(intv.pad1.k, 0, sizeof(intv.pad1.k));
            }


            if (paused)
            {
                intv.gfx.dirty = 1;
                gfx_stic_tick(&intv.gfx);
                intv.event.periph.tick((periph_p)&(intv.event), 0);
                next_cycles = cycles + 14934;
                return;
            } else if (do_reset)
            {
                intv.gfx.dirty = 1;
                gfx_stic_tick(&intv.gfx);
                intv.event.periph.tick((periph_p)&(intv.event), 10000);
                cycles += 10000;
                next_cycles = cycles + 14934;
                return;
            } else
            {
                intv.gfx.scrshot &= ~GFX_RESET;
                cycles += periph_tick((periph_p)(intv.intv), max_step);
            }

            if (!intv.debugging && intv.debug.step_count == 0)
                intv.debug.step_count = ~0U;

            curr_time = get_time();

            if (!intv.debugging && !do_reset && (curr_time > disp_time + 1.0))
            {
                disp_time = curr_time;
                then  = now;
                now   = elapsed(0);
                rate  = (cycles / now);
                if (now - then > 0.01)
                {
                    irate = (cycles - icyc) / (now - then);
                    icyc  = cycles;

    #if 0
                    jzp_printf("Rate: [%6.2f%% %6.2f%%]  Drop Gfx:[%6.2f%% %6d] "
                           "Snd:[%6.2f%% %2d %6.3f]\n",
                            rate * 100., irate * 100.,
                            100. * intv.gfx.tot_dropped_frames / intv.gfx.tot_frames,
                            (int)intv.gfx.tot_dropped_frames,
                            100. * intv.snd.mixbuf.tot_drop / intv.snd.tot_frame,
                            (int)intv.snd.mixbuf.tot_drop,
                            (double)intv.snd.tot_dirty / intv.snd.tot_frame);
                    jzp_flush();
    #endif
                }

                if (curr_time > (reset_time + 60.0))
                {
                    reset_time = curr_time;
                    then = elapsed(1);
                    cycles = icyc = 0;
                }
            }

            if (do_reset)
            {
                intv.cp1600.r[7] = 0x1000;
                gfx_vid_enable(&(intv.gfx), 0);
                s_cnt++;
            } else
            {
                if (s_cnt > 140) break;
                s_cnt = 0;
            }
        }
        return;
      }
    }

    jzintv_state = JZINTV_RUNNING;
}


/* ------------------------------------------------------------------------ */
/*  START_ON_CLICK   -- Set a flag to allow emulator to start on a click.   */
/*                                                                          */
/*  In recent browsers, WebAudio can only be initialized from a direct      */
/*  interaction event.  Also, in general it seems nice to not start the     */
/*  until we get a click from the user.                                     */
/*                                                                          */
/*  See:  https://paulbakaus.com/tutorials/html5/web-audio-on-ios/          */
/* ------------------------------------------------------------------------ */
LOCAL void start_on_click(void)
{
    if (saw_click) return;

    EM_ASM(
        SDL.openAudioContext();
        var buffer = SDL.audioContext.createBuffer(1, 1, 44100);
        var source = SDL.audioContext.createBufferSource();
        source.buffer = buffer;
        source.connect(SDL.audioContext.destination);
        source.start(0);
    );

    saw_click = 1;

    return;
}

LOCAL EM_BOOL start_on_click_mouse(int eventType, 
                                   const EmscriptenMouseEvent *mouseEvent, 
                                   void *userData)
{
    UNUSED(eventType);
    UNUSED(mouseEvent);
    UNUSED(userData);
    start_on_click();
    return 0;
}
LOCAL EM_BOOL start_on_click_touch(int eventType, 
                                   const EmscriptenTouchEvent *touchEvent, 
                                   void *userData)
{
    UNUSED(eventType);
    UNUSED(touchEvent);
    UNUSED(userData);
    start_on_click();
    return 0;
}

/* "Needed" elsewhere... */
void dump_state(void)
{
    return;
}

/* ======================================================================== */
/*  This program is free software; you can redistribute it and/or modify    */
/*  it under the terms of the GNU General Public License as published by    */
/*  the Free Software Foundation; either version 2 of the License, or       */
/*  (at your option) any later version.                                     */
/*                                                                          */
/*  This program is distributed in the hope that it will be useful,         */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of          */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU       */
/*  General Public License for more details.                                */
/*                                                                          */
/*  You should have received a copy of the GNU General Public License       */
/*  along with this program; if not, write to the Free Software             */
/*  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.               */
/* ======================================================================== */
/*                 Copyright (c) 2018-+Inf, Joseph Zbiciak                  */
/* ======================================================================== */
