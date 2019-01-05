/*
 * ============================================================================
 *  Title:    Graphics Interface Routines
 *  Author:   J. Zbiciak, J. Tanner
 * ============================================================================
 *  GFX_INIT         -- Initializes a gfx_t object.
 *  GFX_TICK         -- Services a gfx_t tick.
 *  GFX_VID_ENABLE   -- Alert gfx that video has been enabled or blanked
 *  GFX_SET_BORD     -- Set the border / offset parameters for the display
 * ============================================================================
 *  GFX_T            -- Graphics subsystem object.
 *  GFX_PVT_T        -- Private internal state to gfx_t structure.
 *  GFX_STIC_PALETTE -- The STIC palette.
 * ============================================================================
 *  The graphics subsystem provides an abstraction layer between the
 *  emulator and the graphics library being used.  Theoretically, this
 *  should allow easy porting to other graphics libraries.
 *
 *  TODO:
 *   -- Make use of dirty rectangle updating for speed.
 * ============================================================================
 */

#include "config.h"
#include "periph/periph.h"
#include "gfx.h"
//#include "file/file.h"
#include "mvi/mvi.h"
#include "avi/avi.h"
#include "gif/gif_enc.h"
#include "lzoe/lzoe.h"
#include "file/file.h"

LOCAL void gfx_scrshot(uint_8 *scr);

/*
 * ============================================================================
 *  GFX_PVT_T        -- Private internal state to gfx_t structure.
 * ============================================================================
 */
typedef struct gfx_pvt_t
{
    int         vid_enable;         /*  Video enable flag.                  */
    int         fps;                /*  Frame rate.                         */

    int         movie_init;         /*  Is movie structure initialized?     */
    mvi_t       *movie;             /*  Pointer to mvi_t to reduce deps     */

    avi_writer_t *avi;
    int         audio_rate;         /*  Ugh... only needed for AVI.         */

} gfx_pvt_t;

LOCAL void gfx_dtor(periph_p p);
LOCAL void gfx_movieupd(gfx_t *gfx);
LOCAL void gfx_aviupd(gfx_t *gfx);

/*
 * ============================================================================
 *  GFX_STIC_PALETTE -- The STIC palette.
 * ============================================================================
 */
LOCAL uint_8 gfx_stic_palette[32][3] =
{
#if 1
    /* -------------------------------------------------------------------- */
    /*  I generated these colors by directly eyeballing my television       */
    /*  while it was next to my computer monitor.  I then tweaked each      */
    /*  color until it was pretty close to my TV.  Bear in mind that        */
    /*  NTSC (said to mean "Never The Same Color") is highly susceptible    */
    /*  to Tint/Brightness/Contrast settings, so your mileage may vary      */
    /*  with this particular palette setting.                               */
    /* -------------------------------------------------------------------- */
    { 0x00, 0x00, 0x00 },
    { 0x00, 0x2D, 0xFF },
    { 0xFF, 0x3D, 0x10 },
    { 0xC9, 0xCF, 0xAB },
    { 0x38, 0x6B, 0x3F },
    { 0x00, 0xA7, 0x56 },
    { 0xFA, 0xEA, 0x50 },
    { 0xFF, 0xFC, 0xFF },
    { 0xBD, 0xAC, 0xC8 },
    { 0x24, 0xB8, 0xFF },
    { 0xFF, 0xB4, 0x1F },
    { 0x54, 0x6E, 0x00 },
    { 0xFF, 0x4E, 0x57 },
    { 0xA4, 0x96, 0xFF },
    { 0x75, 0xCC, 0x80 },
    { 0xB5, 0x1A, 0x58 },
#else
    { 0x00, 0x00, 0x00 },
    { 0x00, 0x16, 0xFE },
    { 0xFE, 0x5A, 0x02 },
    { 0xC0, 0xD8, 0x63 },
    { 0x00, 0xB7, 0x00 },
    { 0x00, 0xE6, 0x18 },
    { 0xF0, 0xFF, 0x56 },
    { 0xFD, 0xFD, 0xFF },
    { 0xBF, 0xC3, 0xCA },
    { 0x00, 0xC8, 0xF0 },
    { 0xFC, 0xCA, 0x23 },
    { 0x20, 0x80, 0x00 },
    { 0xFF, 0x5E, 0xA8 },
    { 0xA0, 0x90, 0xFF },
    { 0x90, 0xFF, 0x60 },
    { 0xC0, 0x10, 0x7A },
#endif

    /* -------------------------------------------------------------------- */
    /*  This pink color is used for drawing rectangles around sprites.      */
    /*  It's a temporary hack.                                              */
    /* -------------------------------------------------------------------- */
    { 0xFF, 0x80, 0x80 },
    /* -------------------------------------------------------------------- */
    /*  Grey shades used for misc tasks (not currently used).               */
    /* -------------------------------------------------------------------- */
    { 0x11, 0x11, 0x11 },
    { 0x22, 0x22, 0x22 },
    { 0x33, 0x33, 0x33 },
    { 0x44, 0x44, 0x44 },
    { 0x55, 0x55, 0x55 },
    { 0x66, 0x66, 0x66 },
    { 0x77, 0x77, 0x77 },
    { 0x88, 0x88, 0x88 },
    { 0x99, 0x99, 0x99 },
    { 0xAA, 0xAA, 0xAA },
    { 0xBB, 0xBB, 0xBB },
    { 0xCC, 0xCC, 0xCC },
    { 0xDD, 0xDD, 0xDD },
    { 0xEE, 0xEE, 0xEE },
    { 0xFF, 0xFF, 0xFF },
};


/*  01234567890123
**  ###  ####  ###
**  #  # #    #
**  ###  ###  #
**  #  # #    #
**  #  # ####  ###
*/

/*  Currently unused... need to add it back when movie recording is on.
LOCAL const char* gfx_rec_bmp[5] =
{
   "###  ####  ###",
   "#  # #    #   ",
   "###  ###  #   ",
   "#  # #    #   ",
   "#  # ####  ###"
};
*/


/* ======================================================================== */
/*  GFX_LOAD_PALETTE -- Load an alternate palette from a file.              */
/* ======================================================================== */
void gfx_load_palette(const char *const fname)
{
    FILE *f;
    int i;
    unsigned r, g, b;
    char buf[256], *s1, *s2, *const end = buf + sizeof(buf);

    if (!file_exists(fname))
    {
        fprintf(stderr, "gfx: Could not find palette file '%s'\n", fname);
        exit(1);
    }

    if (!(f = fopen(fname, "r")))
    {
        perror("fopen()");
        fprintf(stderr, "gfx: Error opening palette file '%s'\n", fname);
        exit(1);
    }

    /* -------------------------------------------------------------------- */
    /*  Format:                                                             */
    /*   -- 16 non-empty lines, one for each of the 16 INTV colors.         */
    /*   -- Empty lines ignored.                                            */
    /*   -- Anything after a ';' ignored                                    */
    /*   -- Each line must either have:                                     */
    /*       #rrggbb        HTML-style hexadecimal color triple             */
    /*       dec dec dec    Three decimal numbers separated by spaces       */
    /*   -- Lines must be less than 256 characters.                         */
    /* -------------------------------------------------------------------- */

    i = 0;
    while (i < 16 && fgets(buf, sizeof(buf), f) != NULL)
    {
        int prev_was_ws = 1, curr_is_ws;

        s1 = s2 = buf;

        while (s1 != end && *s1 && *s1 != ';')
        {
            curr_is_ws = isspace(*s1);

            if (prev_was_ws && curr_is_ws)
            {
                s1++;
                continue;
            }
            *s2++ = *s1++;
            prev_was_ws = curr_is_ws;
        }
        *s2 = 0;

        if (s2 == buf)
            continue;

        if (buf[0] == '#') sscanf(buf + 1, "%2x%2x%2x", &r, &g, &b);
        else               sscanf(buf,     "%d %d %d",  &r, &g, &b);

        r &= 0xFF;
        g &= 0xFF;
        b &= 0xFF;

        jzp_printf("gfx: Setting palette %d to #%02X%02X%02X (%d,%d,%d)\n",
                    i, r, g, b, r, g, b);

        gfx_stic_palette[i][0] = r;
        gfx_stic_palette[i][1] = g;
        gfx_stic_palette[i][2] = b;
        i++;
    }

    if (i != 16)
        fprintf(stderr, "gfx: Warning: only %d palette entries loaded\n", i);

    fclose(f);
}

/* ======================================================================== */
/*  GFX_CHECK        -- Validates gfx parameters                            */
/* ======================================================================== */
int gfx_check(int desire_x, int desire_y, int desire_bpp, int prescaler)
{
    UNUSED(desire_x);
    UNUSED(desire_y);
    UNUSED(desire_bpp);
    UNUSED(prescaler);
    return 0;
}

/* ======================================================================== */
/*  GFX_INIT         -- Initializes a gfx_t object.                         */
/* ======================================================================== */
int gfx_init(gfx_t *gfx, int desire_x, int desire_y, int desire_bpp,
                         int flags,    int verbose,  int prescaler,
                         int border_x, int border_y, int pal_mode,
                         struct avi_writer_t *const avi, int audio_rate)
{
    UNUSED(desire_x);
    UNUSED(desire_y);
    UNUSED(desire_bpp);
    UNUSED(flags);
    UNUSED(verbose);
    UNUSED(prescaler);
    UNUSED(border_x);
    UNUSED(border_y);
    UNUSED(pal_mode);

    /* -------------------------------------------------------------------- */
    /*  Sanity checks and cleanups.                                         */
    /* -------------------------------------------------------------------- */
    assert(gfx);
    memset((void*)gfx, 0, sizeof(gfx_t));

    /* -------------------------------------------------------------------- */
    /*  Allocate memory for the gfx_t.                                      */
    /* -------------------------------------------------------------------- */
    gfx->vid = CALLOC(uint_8,    160 * 200);
    gfx->pvt = CALLOC(gfx_pvt_t, 1);

    if (gfx->pvt)
    {
        gfx->pvt->fps           = pal_mode ? 50 : 60;
        gfx->pvt->avi           = avi;
        gfx->pvt->audio_rate    = audio_rate;  // ugh
    }

    if (!gfx->vid || !gfx->pvt)
    {

        fprintf(stderr, "gfx:  Panic:  Could not allocate memory.\n");

        goto die;
    }

    gfx->pvt->vid_enable = 0;
    gfx->dirty = 3;
    gfx->b_dirty = 3;

    /* -------------------------------------------------------------------- */
    /*  Set up the gfx_t's internal structures.                             */
    /* -------------------------------------------------------------------- */
    gfx->periph.read        = NULL;
    gfx->periph.write       = NULL;
    gfx->periph.peek        = NULL;
    gfx->periph.poke        = NULL;
    gfx->periph.tick        = NULL;
    gfx->periph.min_tick    = 0;
    gfx->periph.max_tick    = INT_MAX;
    gfx->periph.addr_base   = 0;
    gfx->periph.addr_mask   = 0;
    gfx->periph.dtor        = gfx_dtor;

#ifdef BENCHMARK_GFX
    atexit(gfx_dr_hist_dump);
#endif

    return 0;

die:
    CONDFREE(gfx->pvt);
    CONDFREE(gfx->vid);
    return -1;
}

/* ======================================================================== */
/*  GFX_DTOR     -- Tear down the gfx_t                                     */
/* ======================================================================== */
LOCAL void gfx_dtor(periph_p p)
{
    gfx_t *gfx = (gfx_t *)p;

    if (gfx->pvt)
    {
        if (gfx->pvt->movie)
        {
            if (gfx->pvt->movie->f)
                fclose(gfx->pvt->movie->f);

            CONDFREE(gfx->pvt->movie);
        }

        if (avi_is_active(gfx->pvt->avi))
            avi_end_video(gfx->pvt->avi);
    }
    CONDFREE(gfx->pvt);
    CONDFREE(gfx->vid);
}

/* ======================================================================== */
/*  GFX_TOGGLE_WINDOWED -- Try to toggle windowed vs. full-screen.          */
/* ======================================================================== */
void gfx_toggle_windowed(gfx_t *gfx, int quiet)
{
    UNUSED(gfx);
    UNUSED(quiet);
}

/* ======================================================================== */
/*  GFX_FORCE_WINDOWED -- Force display to be windowed mode; Returns 1 if   */
/*                        display was previously full-screen.               */
/* ======================================================================== */
int gfx_force_windowed(gfx_t *gfx, int quiet)
{
    UNUSED(gfx);
    UNUSED(quiet);
    return 0;
}

/* ======================================================================== */
/*  GFX_SET_TITLE    -- Sets the window title                               */
/* ======================================================================== */
int gfx_set_title(gfx_t *gfx, const char *title)
{
    UNUSED(gfx);
    UNUSED(title);
    return 0;
}

/* ======================================================================== */
/*  GFX_STIC_TICK    -- Called directly from STIC emulation.                */
/* ======================================================================== */
void gfx_stic_tick(gfx_t *const gfx)
{
    gfx->tot_frames++;

    /* -------------------------------------------------------------------- */
    /*  Update a movie if one's active, or user requested toggle in movie   */
    /*  state.  We do this prior to dropping frames so that movies always   */
    /*  have a consistent frame rate.                                       */
    /* -------------------------------------------------------------------- */
    if (gfx->scrshot & (GFX_MOVIE | GFX_MVTOG))
        gfx_movieupd(gfx);

    /* -------------------------------------------------------------------- */
    /*  Update an AVI if one's active, or if user requested a toggle.       */
    /* -------------------------------------------------------------------- */
    if (gfx->scrshot & (GFX_AVI | GFX_AVTOG))
        gfx_aviupd(gfx);

    /* -------------------------------------------------------------------- */
    /*  Drop a frame if we need to.                                         */
    /* -------------------------------------------------------------------- */
    if (gfx->drop_frame)
    {
        gfx->drop_frame--;
        if (gfx->dirty) gfx->dropped_frames++;
        return;
    }

    /* -------------------------------------------------------------------- */
    /*  Every ~0.5 second, force a dirty frame, in case there is a static   */
    /*  image.  On some systems (OS X in my case), the window will not      */
    /*  refresh properly unless we send *something* occasionally.           */
    /*                                                                      */
    /*  Where I saw it:  Dragging a window from the Retina display to an    */
    /*  external monitor caused the window to go all white.                 */
    /* -------------------------------------------------------------------- */
    if ((gfx->tot_frames & 31) == 0)
    {
        gfx->dirty |= 3;
        gfx->b_dirty |= 3;
    }

    /* -------------------------------------------------------------------- */
    /*  Don't bother if display isn't dirty or if we're iconified.          */
    /* -------------------------------------------------------------------- */
    if (!gfx->scrshot && (!gfx->dirty || gfx->hidden))
    {
        return;
    }

    /* -------------------------------------------------------------------- */
    /*  DEBUG: Report blocks of dropped frames.                             */
    /* -------------------------------------------------------------------- */
    if (gfx->dropped_frames)
    {
        gfx->tot_dropped_frames += gfx->dropped_frames;
        gfx->dropped_frames = 0;
    }

    gfx->dirty = 0;
    gfx->b_dirty = 0;

    /* -------------------------------------------------------------------- */
    /*  If a screen-shot was requested, go write out a GIF file of the      */
    /*  screen right now.  Screen-shot GIFs are always 320x200.             */
    /* -------------------------------------------------------------------- */
    if (gfx->scrshot & GFX_SHOT)
    {
        gfx_scrshot(gfx->vid);
        gfx->scrshot &= ~GFX_SHOT;
    }

    return;
}

/* ======================================================================== */
/*  GFX_VID_ENABLE   -- Alert gfx that video has been enabled or blanked    */
/* ======================================================================== */
void gfx_vid_enable(gfx_t *gfx, int enabled)
{
    /* -------------------------------------------------------------------- */
    /*  Force 'enabled' to be 0 or 1.                                       */
    /* -------------------------------------------------------------------- */
    enabled = enabled == VID_ENABLED;

    /* -------------------------------------------------------------------- */
    /*  If enabled state changed, schedule a palette update.                */
    /* -------------------------------------------------------------------- */
    if ((gfx->pvt->vid_enable ^ enabled) & 1)
    {
        gfx->pvt->vid_enable |= 2;
        gfx->dirty |= 2;
    } else
    {
        gfx->pvt->vid_enable = enabled;
    }
}

/* ======================================================================== */
/*  GFX_SET_BORD     -- Set the border color for the display                */
/* ======================================================================== */
void gfx_set_bord
(
    gfx_t *gfx,         /*  Graphics object.                        */
    int b_color
)
{
    int dirty = 0;

    /* -------------------------------------------------------------------- */
    /*  Set up the display parameters.                                      */
    /* -------------------------------------------------------------------- */
    if (gfx->b_color != b_color) { gfx->b_color = b_color; dirty = 3; }

    if (dirty)     { gfx->dirty   |= 1; }
    if (dirty & 2) { gfx->b_dirty |= 2; }
}

/* ======================================================================== */
/*  GFX_SCRSHOT      -- Write a 320x200 screen shot to a GIF file.          */
/* ======================================================================== */
LOCAL uint_8 scrshot_buf[320*200];
LOCAL void gfx_scrshot(uint_8 *scr)
{
    static int last = -1;
    FILE * f;
    char f_name[32];
    int num = last, i, len;


    /* -------------------------------------------------------------------- */
    /*  Search for an unused screen-shot file name.                         */
    /* -------------------------------------------------------------------- */
    do
    {
        num = (num + 1) % 10000;

        snprintf(f_name, sizeof(f_name), "shot%.4d.gif", num);

        if (!file_exists(f_name))
            break;

    } while (num != last);

    /* -------------------------------------------------------------------- */
    /*  Warn the user if we wrapped all 10000 screen shots...               */
    /* -------------------------------------------------------------------- */
    if (num == last)
    {
        num = (num + 1) % 10000;
        snprintf(f_name, sizeof(f_name), "shot%.4d.gif", num);
        fprintf(stderr, "Warning:  Overwriting %s...\n", f_name);
    }

    /* -------------------------------------------------------------------- */
    /*  Update our 'last' pointer and open the file and dump the PPM.       */
    /* -------------------------------------------------------------------- */
    last = num;
    f    = fopen(f_name, "wb");

    if (!f)
    {
        fprintf(stderr, "Error:  Could not open '%s' for screen dump.\n",
                f_name);
        return;
    }

    /* -------------------------------------------------------------------- */
    /*  Do the screen dump.  Write it as a nice GIF.  We need to pixel      */
    /*  double the image ahead of time.                                     */
    /* -------------------------------------------------------------------- */
    for (i = 0; i < 200*160; i++)
        scrshot_buf[i*2 + 0] = scrshot_buf[i*2 + 1] = scr[i];

    len = gif_write(f, scrshot_buf, 320, 200, (gif_pal_t)gfx_stic_palette, 16);
    if (len > 0)
    {
        jzp_printf("\nWrote screen shot to '%s', %d bytes\n", f_name, len);
    } else
    {
        jzp_printf("\nError writing screen shot to '%s'\n", f_name);
    }
    jzp_flush();
    fclose(f);

    return;
}

/* ======================================================================== */
/*  GFX_MOVIEUPD     -- Start/Stop/Update a movie in progress               */
/* ======================================================================== */
LOCAL void gfx_movieupd(gfx_t *gfx)
{
    gfx_pvt_t *pvt = gfx->pvt;

    /* -------------------------------------------------------------------- */
    /*  Toggle current movie state if user requested.                       */
    /* -------------------------------------------------------------------- */
    if (gfx->scrshot & GFX_MVTOG)
    {
        static int last = -1;
        int num = last;
        char f_name[32];

        /* ---------------------------------------------------------------- */
        /*  Whatever happens, clear the toggle.                             */
        /* ---------------------------------------------------------------- */
        gfx->scrshot &= ~GFX_MVTOG;

        /* ---------------------------------------------------------------- */
        /*  Make sure movie subsystem initialized.  We only init this if    */
        /*  someone tries to take a movie.                                  */
        /* ---------------------------------------------------------------- */
        if (!pvt->movie_init)
        {
            if (!pvt->movie) pvt->movie = CALLOC(mvi_t, 1);
            if (!pvt->movie)
            {
                fprintf(stderr, "No memory for movie structure\n");
                return;
            }

            mvi_init(pvt->movie, 160, 200);
            pvt->movie_init = 1;
        }

        /* ---------------------------------------------------------------- */
        /*  If a movie's open, close it.                                    */
        /* ---------------------------------------------------------------- */
        if ((gfx->scrshot & GFX_MOVIE) != 0)
        {
            if (pvt->movie->f)
            {
                fclose(pvt->movie->f);
                jzp_printf("\nDone writing movie:\n"
                       "    Total frames:        %10d\n"
                       "    Total size:          %10d\n"
                       "    Bytes/frame:         %10d\n"
#ifndef NO_LZO
                       "    Bytes saved LZO:     %10d\n"
#endif
                       "    Dupe frames:         %10d\n"
                       "    Dupe rows:           %10d\n"
                       "    Compression ratio:   %8.2f:1\n",
                       pvt->movie->fr,
                       pvt->movie->tot_bytes,
                       pvt->movie->tot_bytes / pvt->movie->fr,
#ifndef NO_LZO
                       pvt->movie->tot_lzosave,
#endif
                       pvt->movie->rpt_frames,
                       pvt->movie->rpt_rows,
                       (16032.*pvt->movie->fr) / pvt->movie->tot_bytes);
                jzp_flush();
            }

            gfx->scrshot &= ~GFX_MOVIE;
            pvt->movie->f  = NULL;
            pvt->movie->fr = 0;

            return;
        }

        /* ---------------------------------------------------------------- */
        /*  Otherwise, open a new movie.                                    */
        /*  Search for an unused movie file name.                           */
        /* ---------------------------------------------------------------- */
        do
        {
            num = (num + 1) % 10000;

            snprintf(f_name, sizeof(f_name), "mvi_%.4d.imv", num);

            if (!file_exists(f_name))
                break;

        } while (num != last);

        /* ---------------------------------------------------------------- */
        /*  Warn the user if we wrapped all 10000 movie slots...            */
        /* ---------------------------------------------------------------- */
        if (num == last)
        {
            num = (num + 1) % 10000;
            snprintf(f_name, sizeof(f_name), "mvi_%.4d.imv", num);
            fprintf(stderr, "Warning:  Overwriting %s...\n", f_name);
        }

        /* ---------------------------------------------------------------- */
        /*  Update our 'last' pointer, and start the movie.                 */
        /* ---------------------------------------------------------------- */
        last = num;
        pvt->movie->f = fopen(f_name, "wb");

        if (!pvt->movie->f)
        {
            fprintf(stderr, "Error:  Could not open '%s' for movie.\n",
                    f_name);
            return;
        }

        jzp_printf("\nStarted movie file '%s'\n", f_name); jzp_flush();

        /* ---------------------------------------------------------------- */
        /*  Success:  Turn on the movie.                                    */
        /* ---------------------------------------------------------------- */
        gfx->scrshot |= GFX_MOVIE;
        pvt->movie->fr = 0;
    }

    if ((gfx->scrshot & GFX_RESET) == 0)
        mvi_wr_frame(pvt->movie, gfx->vid, gfx->bbox);
}

/* ======================================================================== */
/*  GFX_AVIUPD       -- Start/Stop/Update an AVI in progress                */
/* ======================================================================== */
LOCAL void gfx_aviupd(gfx_t *gfx)
{
    gfx_pvt_t *pvt = gfx->pvt;
    avi_writer_t *avi = pvt->avi;

    /* -------------------------------------------------------------------- */
    /*  Toggle current AVI state if user requested.                         */
    /* -------------------------------------------------------------------- */
    if ((gfx->scrshot & (GFX_AVTOG | GFX_RESET)) == GFX_AVTOG)
    {
        static int last = -1;
        int num = last;
        char f_name[32];
        FILE *avi_file;

        /* ---------------------------------------------------------------- */
        /*  Whatever happens, clear the toggle.                             */
        /* ---------------------------------------------------------------- */
        gfx->scrshot &= ~GFX_AVTOG;

        /* ---------------------------------------------------------------- */
        /*  If a AVI's open, close it.                                      */
        /* ---------------------------------------------------------------- */
        if ((gfx->scrshot & GFX_AVI) != 0)
        {
            if (avi_is_active(avi))
            {
                const avi_info_t *info = avi_info(avi);
                avi_end_video(avi);     // does not invalidate 'info'

                jzp_printf("\nDone writing AVI\n"
                       "    Total frames:        %10d\n",
                       info->total_frames);
                jzp_flush();
            }

            gfx->scrshot &= ~GFX_AVI;
            return;
        }

        /* ---------------------------------------------------------------- */
        /*  Otherwise, open a new movie.                                    */
        /*  Search for an unused movie file name.                           */
        /* ---------------------------------------------------------------- */
        do
        {
            num = (num + 1) % 10000;

            snprintf(f_name, sizeof(f_name), "avi_%.4d.avi", num);

            if (!file_exists(f_name))
                break;

        } while (num != last);

        /* ---------------------------------------------------------------- */
        /*  Warn the user if we wrapped all 10000 movie slots...            */
        /* ---------------------------------------------------------------- */
        if (num == last)
        {
            num = (num + 1) % 10000;
            snprintf(f_name, sizeof(f_name), "avi_%.4d.avi", num);
            fprintf(stderr, "Warning:  Overwriting %s...\n", f_name);
        }

        /* ---------------------------------------------------------------- */
        /*  Update our 'last' pointer, and start the movie.                 */
        /* ---------------------------------------------------------------- */
        last = num;
        avi_file = fopen(f_name, "wb");

        if (!avi_file)
        {
            fprintf(stderr, "Error:  Could not open '%s' for AVI.\n",
                    f_name);
            return;
        }

        jzp_printf("\nStarted AVI file '%s'\n", f_name); jzp_flush();

        /* ---------------------------------------------------------------- */
        /*  Success:  Turn on the movie.                                    */
        /* ---------------------------------------------------------------- */
        gfx->scrshot |= GFX_AVI;
        avi_start_video( avi, avi_file, pvt->fps, pvt->audio_rate, 1,
                         gfx->periph.now);
        avi_set_palette( avi, gfx_stic_palette, 32 );
    }

    if ((gfx->scrshot & GFX_RESET) == 0)
        avi_record_video(pvt->avi, gfx->vid + 4*160, gfx->vid[0]);
}

/* ======================================================================== */
/*  GFX_RESYNC   -- Resynchronize GFX after a load.                         */
/* ======================================================================== */
void gfx_resync(gfx_t *gfx)
{
    UNUSED(gfx);
}

/* ======================================================================== */
/*  GFX_HIDDEN       -- Returns true if the graphics window is hidden.      */
/* ======================================================================== */
int gfx_hidden(const gfx_t *gfx)
{
    return (gfx->scrshot & (GFX_MOVIE | GFX_AVI | GFX_SHOT)) == 0;
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
/*          Copyright (c) 1998-2017, Joseph Zbiciak, John Tanner            */
/* ======================================================================== */
