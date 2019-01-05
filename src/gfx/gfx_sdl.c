//#define BENCHMARK_GFX
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

#include "sdl_jzintv.h"
#include "config.h"
#include "periph/periph.h"
#include "gfx.h"
#include "gfx_prescale.h"
#include "gfx_scale.h"
//#include "file/file.h"
#include "mvi/mvi.h"
#include "avi/avi.h"
#include "gif/gif_enc.h"
#include "lzoe/lzoe.h"
#include "file/file.h"

LOCAL void gfx_scrshot(uint_8 *scr);

const double frame_delta = 0.0166;  /* Slightly faster than 60Hz.           */

/*
 * ============================================================================
 *  GFX_PVT_T        -- Private internal state to gfx_t structure.
 * ============================================================================
 */
typedef struct gfx_pvt_t
{
    SDL_Surface *scr;               /*  Screen surface.                     */
    SDL_Color   pal_on [32];        /*  Palette when video is enabled.      */
    SDL_Color   pal_off[32];        /*  Palette when video is blanked.      */
    int         vid_enable;         /*  Video enable flag.                  */
    int         border_x, border_y; /*  X/Y border padding.                 */
    int         dim_x, dim_y;       /*  X/Y dimensions of window.           */
    int         ofs_x, ofs_y;       /*  X/Y offsets for centering img.      */
    int         bpp;                /*  Actual color depth.                 */
    int         flags;              /*  Flags for current display surf.     */
    int         fps;                /*  Frame rate.                         */

    /* For GFX_DROP_EXTRA only: */
    double      last_frame;         /*  Wallclock time of next frame.       */

    int         movie_init;         /*  Is movie structure initialized?     */
    mvi_t       *movie;             /*  Pointer to mvi_t to reduce deps     */

    avi_writer_t *avi;
    int         audio_rate;         /*  Ugh... only needed for AVI.         */

    uint_8  *RESTRICT inter_vid;    /*  Intermediate video after prescaler  */
    uint_8  *RESTRICT prev;         /*  previous frame for dirty-rect       */

    gfx_prescaler_t      prescaler; /*  Scale 160x200 to an intermediate    */
    gfx_prescaler_dtor_t ps_dtor;   /*  Destructor for prescaler, if any.   */
    void                *ps_opaque; /*  Prescaler opaque structure          */
    gfx_scale_spec_t     scaler;

    gfx_dirtyrect_spec  dr_spec;    /*  Dirty-rectangle control spec.       */


    uint_32     *dirty_rows;        /*  dirty-row bitmap for scaler         */
    int         dirty_rows_sz;

    int         num_rects;
    SDL_Rect    *dirty_rects;

    SDL_Rect    bord[4];            /* Border rectangles                    */
} gfx_pvt_t;

LOCAL void gfx_dtor(periph_p p);
LOCAL void gfx_tick_generic(gfx_t *gfx);
LOCAL void gfx_find_dirty_rects(gfx_t *gfx);
LOCAL void gfx_movieupd(gfx_t *gfx);
LOCAL void gfx_aviupd(gfx_t *gfx);

/*
 * ============================================================================
 *  GFX_STIC_PALETTE -- The STIC palette.
 * ============================================================================
 */
LOCAL uint_8 gfx_stic_palette[32][3] =
{
    /* -------------------------------------------------------------------- */
    /*  This is an NTSC palette derived from a combination of screen cap    */
    /*  and eyeballing.  It's somewhat better in practice so far.           */
    /* -------------------------------------------------------------------- */
    { 0x00, 0x00, 0x00 },
    { 0x14, 0x38, 0xF7 },
    { 0xE3, 0x5B, 0x0E },
    { 0xCB, 0xF1, 0x68 },
    { 0x00, 0x94, 0x28 },
    { 0x07, 0xC2, 0x00 },
    { 0xFF, 0xFF, 0x01 },
    { 0xFF, 0xFF, 0xFF },
    { 0xC8, 0xC8, 0xC8 },
    { 0x23, 0xCE, 0xC3 },
    { 0xFD, 0x99, 0x18 },
    { 0x3A, 0x8A, 0x00 },
    { 0xF0, 0x46, 0x3C },
    { 0xD3, 0x83, 0xFF },
    { 0x48, 0xF6, 0x01 },
    { 0xB8, 0x11, 0x78 },

#if 0
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
#endif
#if 0
    /* This is an older jzIntv palette */
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

/* ------------------------------------------------------------------------ */
/*  This is a PAL pallete (used in PAL mode) derived with help from Oscar   */
/*  Toledo G. (nanochess).                                                  */
/* ------------------------------------------------------------------------ */
LOCAL uint_8 gfx_stic_palette_pal[16][3] =
{
    { 0x00, 0x00, 0x00 },
    { 0x00, 0x75, 0xFF },
    { 0xFF, 0x4C, 0x39 },
    { 0xD1, 0xB9, 0x51 },
    { 0x09, 0xB9, 0x00 },
    { 0x30, 0xDF, 0x10 },
    { 0xFF, 0xE5, 0x01 },
    { 0xFF, 0xFF, 0xFF },
    { 0x8C, 0x8C, 0x8C },
    { 0x28, 0xE5, 0xC0 },
    { 0xFF, 0xA0, 0x2E },
    { 0x64, 0x67, 0x00 },
    { 0xFF, 0x29, 0xFF },
    { 0x8C, 0x8F, 0xFF },
    { 0x7C, 0xED, 0x00 },
    { 0xC4, 0x2B, 0xFC },
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


LOCAL int non_default_palette = 0;

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

    non_default_palette = 1;

    fclose(f);
}

/* ======================================================================== */
/*  GFX_SDL_ABORT    -- Abort due to SDL errors.                            */
/* ======================================================================== */
LOCAL void gfx_sdl_abort(const char *context)
{
    fprintf(stderr, "gfx: %s\ngfx/SDL Error:%s\n", context, SDL_GetError());
    exit(1);
}

/* ======================================================================== */
/*  GFX_SET_SCALER_PALETTE                                                  */
/* ======================================================================== */
LOCAL void gfx_set_scaler_palette
(
    SDL_Surface         *scr,
    gfx_scale_spec_t    *scaler,
    SDL_Color           pal[32]
)
{
    int i;
    uint_32 t;

    for (i = 0; i < 32; i++)
    {
        t = SDL_MapRGB(scr->format, pal[i].r, pal[i].g, pal[i].b);
        gfx_scale_set_palette(scaler, i, t);
    }
}

/* ======================================================================== */
/*  GFX_SETUP_SDL_SURFACE:  Do all the dirty SDL dirty work for setting up  */
/*                          the display.  This gets called during init, or  */
/*                          when toggling between full-screen and windowed  */
/* ======================================================================== */
LOCAL int gfx_setup_sdl_surface
(
    gfx_t *gfx, int flags, int quiet
)
{
    int i;
    int border_x   = gfx->pvt->border_x;
    int border_y   = gfx->pvt->border_y;
    int scaler_x   = gfx->pvt->scaler.actual_x;
    int scaler_y   = gfx->pvt->scaler.actual_y;
    int desire_x   = scaler_x + border_x;
    int desire_y   = scaler_y + border_y;
    int desire_bpp = gfx->pvt->scaler.bpp;
    int actual_x = desire_x, actual_y = desire_y;
    uint_32 sdl_flags = 0;
    SDL_Surface *scr;

    /* -------------------------------------------------------------------- */
    /*  Set up the SDL video flags from our flags.                          */
    /* -------------------------------------------------------------------- */
    if (desire_bpp != 8)
        flags &= ~GFX_HWPAL;     /* ignore if not 8 bpp */

    if ((flags & GFX_DRECTS) != 0)
        flags &= ~GFX_DBLBUF;
again:

    sdl_flags  = flags & GFX_SWSURF ? SDL_SWSURFACE  : SDL_HWSURFACE;
    sdl_flags |= flags & GFX_DBLBUF ? SDL_DOUBLEBUF  : 0;
    sdl_flags |= flags & GFX_ASYNCB ? SDL_ASYNCBLIT  : 0;
    sdl_flags |= flags & GFX_HWPAL  ? SDL_HWPALETTE  : 0;
    sdl_flags |= flags & GFX_FULLSC ? SDL_FULLSCREEN : 0;

    /* -------------------------------------------------------------------- */
    /*  Try to allocate a screen surface at the desired size, etc.          */
    /* -------------------------------------------------------------------- */
    /*  NOTE:  This eventually should do better things about finding        */
    /*  resolutions / color depths that we like, etc.  For now just be      */
    /*  braindead, even if it means SDL will run our video in "emulation."  */
    /* -------------------------------------------------------------------- */
    if (!quiet)
    {
        jzp_printf("gfx:  Searching for video modes near %dx%dx%d with:\n"
           "gfx:      %s surf, %s buf, %s blit, %s pal, %s\n",
           desire_x, desire_y, desire_bpp,
           flags & GFX_SWSURF ? "Software" : "Hardware",
           flags & GFX_DBLBUF ? "Double"   : "Single",
           flags & GFX_ASYNCB ? "Async"    : "Sync",
           flags & GFX_HWPAL  ? "Hardware" : desire_bpp!=8 ? "No" : "Software",
           flags & GFX_FULLSC ? "Full screen" : "Windowed");

        jzp_flush();
    }

    /* -------------------------------------------------------------------- */
    /*  JJT: First, the program must check that the video hardware          */
    /*  actually supports the requested resolution.  For instance, some     */
    /*  Macs cannot handle 320x200 fullscreen.                              */
    /*                                                                      */
    /*  While SDL can try to emulate a low resolution, this feature is      */
    /*  currently broken on SDL for Mac OS X.  This program must handle     */
    /*  such emulation itself.                                              */
    /*                                                                      */
    /*  For now, the program assumes if it can get a mode with the proper   */
    /*  resolution, that mode will suport 8 bits-per-pixel.                 */
    /*  Play this on a EGA machine at your own risk. ;-)                    */
    /* -------------------------------------------------------------------- */
#ifdef GP2X
    actual_x = 320;
    actual_y = 240;
#else
    {
        SDL_Rect **available_modes;
        available_modes = SDL_ListModes(NULL, sdl_flags);

        /* No available mode! */
        if (available_modes == NULL)
        {
            /* ------------------------------------------------------------ */
            /*  Allow falling back to a software palette if we were trying  */
            /*  for a hardware palette.  Hardware palettes are so 1980s.    */
            /* ------------------------------------------------------------ */
            if (flags & GFX_HWPAL)
            {
                jzp_printf("gfx:  No suitable modes found; "
                           "Trying again w/software palette.\n");
                flags &= ~GFX_HWPAL;
                goto again;
            }

            /* ------------------------------------------------------------ */
            /*  If "failure is OK" (typically when toggling fullsc/window)  */
            /*  just return quietly and gracefully.  Caller should fall     */
            /*  back gracefully to a known-working mode.                    */
            /* ------------------------------------------------------------ */
            if (flags & GFX_FAILOK)
                return -1;

            gfx_sdl_abort("No available video modes?");
        }
        else
        /* All modes are available for a windowed display. */
        if (available_modes == (SDL_Rect **)-1)
        {
            actual_x = desire_x;
            actual_y = desire_y;
        }
        else
        /* ListModes returns a list sorted largest to smallest. */
        /* Find the smallest mode >= the size requested.        */
        {
            int best = -1, area_diff, best_area_diff = INT_MAX;

            i = 0;
            while (available_modes[i])
            {
                if (!quiet)
                    jzp_printf("gfx:  Considering %dx%d... ",
                               available_modes[i]->w, available_modes[i]->h);
                if (available_modes[i]->w >= desire_x &&
                    available_modes[i]->h >= desire_y)
                {
                    area_diff = available_modes[i]->w * available_modes[i]->h -
                                desire_x * desire_y;

                    if (best_area_diff > area_diff)
                    {
                        best_area_diff = area_diff;
                        best = i;
                        if (!quiet)
                            jzp_printf("New best fit.  Diff = %d\n", area_diff);
                        if (best_area_diff == 0)
                            break;
                    } else
                        if (!quiet)
                            jzp_printf("Poorer fit.    Diff = %d\n", area_diff);
                } else
                {
                    if (!quiet)
                        jzp_printf("Too small.\n");
                }
                i++;
            }

            /* No suitable mode available. */
            if (best == -1)
                gfx_sdl_abort("No suitable video mode.");

            actual_x = available_modes[best]->w;
            actual_y = available_modes[best]->h;
        }
    }
#endif

    scr = SDL_SetVideoMode(actual_x, actual_y, desire_bpp, sdl_flags);

    if (scr)
        gfx->pvt->scr = scr;
    else
        return -1;

    gfx->pvt->dim_x = actual_x;
    gfx->pvt->dim_y = actual_y;
    gfx->pvt->ofs_x = ((actual_x - scaler_x) >> 1) & (~3);
    gfx->pvt->ofs_y =  (actual_y - scaler_y) >> 1;
    gfx->pvt->bpp   = gfx->pvt->scr->format->BitsPerPixel;
    gfx->pvt->flags = flags;
    sdl_flags       = gfx->pvt->scr->flags;

    gfx->pvt->last_frame = get_time();

    /* -------------------------------------------------------------------- */
    /*  Compute border rectangles.  Don't include empty rectangles.         */
    /* -------------------------------------------------------------------- */
    {
        SDL_Rect *bord = gfx->pvt->bord;
        /* top and bot trim the rectangles that get drawn at left/right     */
        /* if we happen to draw top/bottom rectangles.                      */
        int top = 0, bot = actual_y;

        memset((void*)gfx->pvt->bord, 0, sizeof(gfx->pvt->bord));

        /* Top */
        if (gfx->pvt->ofs_y > 0)
        {
            bord->x = 0;
            bord->y = 0;
            bord->w = actual_x;
            bord->h = gfx->pvt->ofs_y;
            bord++;
            top = gfx->pvt->ofs_y;
        }

        /* Bottom */
        if (gfx->pvt->ofs_y + scaler_y < actual_y)
        {
            bord->x = 0;
            bord->y = gfx->pvt->ofs_y + scaler_y;
            bord->w = actual_x;
            bord->h = actual_y - bord->y;
            bord++;
            bot = bord->y;
        }

        /* Left */
        if (gfx->pvt->ofs_x > 0)
        {
            bord->x = 0;
            bord->y = top;
            bord->w = gfx->pvt->ofs_x;
            bord->h = bot - top;
            bord++;
        }

        /* Right */
        if (gfx->pvt->ofs_x + scaler_x < actual_x)
        {
            bord->x = gfx->pvt->ofs_x + scaler_x;
            bord->y = top;
            bord->w = actual_x - gfx->pvt->ofs_x - scaler_x;
            bord->h = bot - top;
            bord++;
        }
    }

    if (!quiet)
    {
        jzp_printf("gfx:  Selected:  %dx%dx%d with:\n"
           "gfx:      %s surf, %s buf, %s blit, %s pal, %s\n",
           actual_x, actual_y, gfx->pvt->bpp,
           sdl_flags & SDL_HWSURFACE  ? "Hardware"      : "Software",
           sdl_flags & SDL_DOUBLEBUF  ? "Double"        : "Single",
           sdl_flags & SDL_ASYNCBLIT  ? "Async"         : "Sync",
           sdl_flags & SDL_HWPALETTE  ? "Hardware"      : "Software/No",
           sdl_flags & SDL_FULLSCREEN ? "Full screen"   : "Windowed");
    }

    /* -------------------------------------------------------------------- */
    /*  TEMPORARY: Verify that the surface's format is as we expect.  This  */
    /*  is just a temporary bit of paranoia to ensure that scr->pixels      */
    /*  is in the format I _think_ it's in.                                 */
    /* -------------------------------------------------------------------- */
    if ((desire_bpp == 8 && (gfx->pvt->scr->format->BitsPerPixel  !=  8   ||
                             gfx->pvt->scr->format->BytesPerPixel !=  1))   ||
        (desire_bpp ==16 && (gfx->pvt->scr->format->BitsPerPixel  != 16   ||
                             gfx->pvt->scr->format->BytesPerPixel !=  2))   ||
        (desire_bpp ==32 && (gfx->pvt->scr->format->BitsPerPixel  != 32   ||
                             gfx->pvt->scr->format->BytesPerPixel !=  4)))
    {
        fprintf(stderr,"gfx panic: BitsPerPixel = %d, BytesPerPixel = %d\n",
                gfx->pvt->scr->format->BitsPerPixel,
                gfx->pvt->scr->format->BytesPerPixel);
        return -1;
    }

    /* -------------------------------------------------------------------- */
    /*  New surface will may need palette initialization.                   */
    /* -------------------------------------------------------------------- */
    if (gfx->pvt->bpp == 8)
    {
        SDL_SetColors(gfx->pvt->scr,
                      gfx->pvt->vid_enable ? gfx->pvt->pal_on
                                           : gfx->pvt->pal_off, 0, 32);
    } else
    {
        gfx_set_scaler_palette( gfx->pvt->scr,
                               &gfx->pvt->scaler,
                                gfx->pvt->vid_enable ? gfx->pvt->pal_on
                                                     : gfx->pvt->pal_off);
    }

    gfx->b_dirty = 3;

    return 0;
}

#ifdef BENCHMARK_GFX
LOCAL int dr_hist[244];   /* histogram of number of dirty rects   */
LOCAL int drw_hist[21];   /* histogram of dirty rectangle widths  */

LOCAL void gfx_dr_hist_dump(void);
#endif

/* ======================================================================== */
/*  GFX_CHECK        -- Validates gfx parameters                            */
/* ======================================================================== */
int gfx_check(int desire_x, int desire_y, int desire_bpp, int prescaler)
{
    int i;

    if (desire_x < 320)
    {
        fprintf(stderr, "Minimum X resolution is 320\n");
        return -1;
    }

    if (desire_y < 200)
    {
        fprintf(stderr, "Minimum Y resolution is 200\n");
        return -1;
    }

    if (!(desire_bpp == 8 || desire_bpp == 16 ||
          desire_bpp == 24 || desire_bpp == 32))
    {
        fprintf(stderr, "Bits per pixel must be 8, 16, 24 or 32\n");
        return -1;
    }

    if (prescaler < 0 || prescaler > gfx_prescaler_registry_size)
    {
        if (prescaler > gfx_prescaler_registry_size)
        {
            fprintf(stderr, "gfx:  Prescaler number %d out of range\n",
                    prescaler);
        }
        fprintf(stderr, "Supported prescalers:\n");

        for (i = 0; i < gfx_prescaler_registry_size; i++)
            jzp_printf("    %d: %s\n", i, gfx_prescaler_registry[i].name);

        return -1;
    }

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
    int  inter_x = 160, inter_y = 200;
    int  i, need_inter_vid = 0;
    void *prescaler_opaque;
    gfx_dirtyrect_spec dr_spec;

    /* -------------------------------------------------------------------- */
    /*  Set up prescaler (ie. Scale2X/3X/4X or similar)                     */
    /* -------------------------------------------------------------------- */
    if (prescaler > 0)
    {
        jzp_printf("gfx:  Configuring prescaler %s\n",
                    gfx_prescaler_registry[prescaler].name);
    }

    prescaler_opaque = gfx_prescaler_registry[prescaler].prescaler_init
                       (
                            160,      200,
                            &inter_x, &inter_y, &need_inter_vid,
                            &dr_spec
                       );

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
        int dr_count, dr_x_dim, dr_y_dim;

        dr_x_dim = (dr_spec.active_last_x - dr_spec.active_first_x + 1);
        dr_y_dim = (dr_spec.active_last_y - dr_spec.active_first_y + 1);

        dr_count = ((dr_x_dim + dr_spec.x_step - 1) / dr_spec.x_step) *
                   ((dr_y_dim + dr_spec.y_step - 1) / dr_spec.y_step);

printf("active x: %d, %d, %d active y: %d %d, %d\n", 
dr_spec.active_first_x, dr_spec.active_last_x, dr_spec.x_step,
dr_spec.active_first_y, dr_spec.active_last_y, dr_spec.y_step);

        if (need_inter_vid)
            gfx->pvt->inter_vid = CALLOC(uint_8, inter_x * inter_y);
        else
            gfx->pvt->inter_vid = gfx->vid;

        gfx->pvt->prescaler = gfx_prescaler_registry[prescaler].prescaler;
        gfx->pvt->ps_opaque = prescaler_opaque;
        gfx->pvt->ps_dtor   = gfx_prescaler_registry[prescaler].prescaler_dtor;

        gfx->pvt->prev          = CALLOC(uint_8,   inter_x * inter_y);
        gfx->pvt->dirty_rects   = CALLOC(SDL_Rect, dr_count);

        gfx->pvt->dirty_rows    = CALLOC(uint_32,  ((inter_y+31) >> 5));
        gfx->pvt->dirty_rows_sz = 4 * ((inter_y+31) >> 5);

        gfx->pvt->dr_spec       = dr_spec;

        gfx->pvt->fps           = pal_mode ? 50 : 60;
        gfx->pvt->avi           = avi;
        gfx->pvt->audio_rate    = audio_rate;  // ugh

        gfx->pvt->border_x      = border_x;
        gfx->pvt->border_y      = border_y;
    }

    if (!gfx->vid || !gfx->pvt || !gfx->pvt->prev || !gfx->pvt->dirty_rows ||
        !gfx->pvt->dirty_rects || !gfx->pvt->inter_vid)
    {

        fprintf(stderr, "gfx:  Panic:  Could not allocate memory.\n");

        goto die;
    }

    /* -------------------------------------------------------------------- */
    /*  Select the appropriate tick function based on our display res.      */
    /*  For now, only support 320x200x8bpp or 640x480x8bpp.                 */
    /* -------------------------------------------------------------------- */
    if (gfx->tick_core == NULL)
    {
        if (desire_bpp == 24)
            desire_bpp = 32;

        gfx->tick_core = gfx_tick_generic;
        if (gfx_scale_init_spec(&(gfx->pvt->scaler),
                                 inter_x,  inter_y,
                                 desire_x, desire_y, desire_bpp))
        {
            fprintf(stderr,
                    "Could not configure scaler for %d x %d @ %d bpp\n",
                    desire_x, desire_y, desire_bpp);
            goto die;
        }
    }

    /* -------------------------------------------------------------------- */
    /*  If we're in PAL mode, and the user has not overridden the palette,  */
    /*  load the default PAL palette.                                       */
    /* -------------------------------------------------------------------- */
    if (pal_mode && !non_default_palette)
    {
        jzp_printf("gfx:  Loading default PAL palette.\n");
        for (i = 0; i < 16; i++)
        {
            gfx_stic_palette[i][0] = gfx_stic_palette_pal[i][0];
            gfx_stic_palette[i][1] = gfx_stic_palette_pal[i][1];
            gfx_stic_palette[i][2] = gfx_stic_palette_pal[i][2];
        }
    }

    /* -------------------------------------------------------------------- */
    /*  Set up our color palette.  We start with video blanked.             */
    /* -------------------------------------------------------------------- */
    for (i = 0; i < 16; i++)
    {
        gfx->pvt->pal_on [i].r = gfx_stic_palette[i][0];
        gfx->pvt->pal_on [i].g = gfx_stic_palette[i][1];
        gfx->pvt->pal_on [i].b = gfx_stic_palette[i][2];
        gfx->pvt->pal_off[i].r = gfx_stic_palette[i][0] >> 1;
        gfx->pvt->pal_off[i].g = gfx_stic_palette[i][1] >> 1;
        gfx->pvt->pal_off[i].b = gfx_stic_palette[i][2] >> 1;
    }
    for (i = 16; i < 32; i++)
    {
        gfx->pvt->pal_on [i].r = gfx_stic_palette[i][0];
        gfx->pvt->pal_on [i].g = gfx_stic_palette[i][1];
        gfx->pvt->pal_on [i].b = gfx_stic_palette[i][2];
        gfx->pvt->pal_off[i].r = gfx_stic_palette[i][0];
        gfx->pvt->pal_off[i].g = gfx_stic_palette[i][1];
        gfx->pvt->pal_off[i].b = gfx_stic_palette[i][2];
    }
    gfx->pvt->vid_enable = 0;
    gfx->dirty = 3;
    gfx->b_dirty = 3;

    /* -------------------------------------------------------------------- */
    /*  Set up initial graphics mode.                                       */
    /* -------------------------------------------------------------------- */
    if (gfx_setup_sdl_surface(gfx, flags, !verbose) < 0)
        gfx_sdl_abort("Could not initialize video surface");

    /* -------------------------------------------------------------------- */
    /*  Ok, see if we succeeded in setting our initial video mode, and do   */
    /*  some minor tidying.                                                 */
    /* -------------------------------------------------------------------- */
    if (!gfx->pvt->scr || SDL_Flip(gfx->pvt->scr) == -1)
        gfx_sdl_abort("Could not flip video surface");

    /* -------------------------------------------------------------------- */
    /*  Hide the mouse.                                                     */
    /* -------------------------------------------------------------------- */
    SDL_ShowCursor(0);

    /* -------------------------------------------------------------------- */
    /*  Set up the gfx_t's internal structures.                             */
    /* -------------------------------------------------------------------- */
    gfx->periph.read        = NULL;
    gfx->periph.write       = NULL;
    gfx->periph.peek        = NULL;
    gfx->periph.poke        = NULL;
    gfx->periph.tick        = NULL;  /* STIC ticks us directly */
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
    if (gfx->pvt)
    {
        CONDFREE(gfx->pvt->dirty_rows);
        CONDFREE(gfx->pvt->dirty_rects);
        CONDFREE(gfx->pvt->prev);
        if (gfx->pvt->inter_vid != gfx->vid) CONDFREE(gfx->pvt->inter_vid);
    }
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

        /* destruct the prescaler;
           prescaler should also free opaque struct if needed */
        if (gfx->pvt->ps_dtor)
            gfx->pvt->ps_dtor(gfx->pvt->ps_opaque);

        /* destruct the scaler */
        gfx_scale_dtor(&(gfx->pvt->scaler));

        CONDFREE(gfx->pvt->dirty_rows);
        CONDFREE(gfx->pvt->dirty_rects);
        CONDFREE(gfx->pvt->prev);
        if (gfx->pvt->inter_vid != gfx->vid) CONDFREE(gfx->pvt->inter_vid);
    }
    CONDFREE(gfx->pvt);
    CONDFREE(gfx->vid);
}

/* ======================================================================== */
/*  GFX_TOGGLE_WINDOWED -- Try to toggle windowed vs. full-screen.          */
/* ======================================================================== */
void gfx_toggle_windowed(gfx_t *gfx, int quiet)
{
    uint_32 old_flags = gfx->pvt->flags;
    uint_32 new_flags = (gfx->pvt->flags ^ GFX_FULLSC) | GFX_FAILOK;

    if (!quiet)
        jzp_printf("\n");

    gfx->toggle = 0;
    if (gfx_setup_sdl_surface(gfx, new_flags, quiet) < 0)
        gfx_setup_sdl_surface(gfx, old_flags, quiet);

    gfx->b_dirty |= 2;
    gfx->dirty   |= 2;
    gfx->drop_frame = 0;
}

/* ======================================================================== */
/*  GFX_FORCE_WINDOWED -- Force display to be windowed mode; Returns 1 if   */
/*                        display was previously full-screen.               */
/* ======================================================================== */
int gfx_force_windowed(gfx_t *gfx, int quiet)
{
    if (gfx->pvt->flags & GFX_FULLSC)
    {
        gfx_toggle_windowed(gfx, quiet);
        return 1;
    }

    return 0;
}

/* ======================================================================== */
/*  GFX_SET_TITLE    -- Sets the window title                               */
/* ======================================================================== */
int gfx_set_title(gfx_t *gfx, const char *title)
{
    UNUSED(gfx);
    SDL_WM_SetCaption(title, title);
    return 0;
}

/* ======================================================================== */
/*  GFX_DRAW_BORD    -- Draw the border.  Requires surface already locked.  */
/* ======================================================================== */
LOCAL void gfx_draw_bord(gfx_t *gfx)
{
    int i;
    SDL_Color pal = gfx->pvt->pal_on[gfx->b_color];
    uint32_t bc = SDL_MapRGB(gfx->pvt->scr->format, pal.r, pal.g, pal.b);

    for (i = 0; i < 4; i++)
        if (gfx->pvt->bord[i].w && gfx->pvt->bord[i].h)
            SDL_FillRect(gfx->pvt->scr, &gfx->pvt->bord[i], bc);
}

#ifdef BENCHMARK_GFX
LOCAL double bm_max = 0, bm_min = 1e30, bm_tot = 0;
LOCAL int bm_cnt = 0;
#endif
/* ======================================================================== */
/*  GFX_STIC_TICK    -- Get ticked directly by STIC to fix gfx pipeline.    */
/* ======================================================================== */
void gfx_stic_tick(gfx_t *const gfx)
{
#ifdef BENCHMARK_GFX
    double start, end, diff;

    start = get_time();
#endif
    int delay_at_end = 0;
    int deferred_set_color = 0;

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
    /*  Toggle full-screen/windowed if req'd.                               */
    /* -------------------------------------------------------------------- */
    if (gfx->toggle)
    {
        gfx_toggle_windowed(gfx, 0);
        delay_at_end = 1;
    }

    /* -------------------------------------------------------------------- */
    /*  If we've been asked to drop 'extra' frames (ie. limit to max 60Hz   */
    /*  according to wall-clock), do so.                                    */
    /* -------------------------------------------------------------------- */
    if (gfx->dirty && !gfx->drop_frame && 
        (gfx->pvt->flags & GFX_SKIP_EXTRA) != 0)
    {
        const double now  = get_time();
        const double elapsed = now - gfx->pvt->last_frame;

        if (elapsed < frame_delta)
        {
            gfx->drop_frame = 1;
            gfx->dropped_frames--;  /* Don't count this dropped frame */
        }
    }

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
#if 0
        jzp_printf("Dropped %d frames.\n", gfx->dropped_frames);
        jzp_flush();
#endif
        gfx->tot_dropped_frames += gfx->dropped_frames;
        gfx->dropped_frames = 0;
    }

    /* -------------------------------------------------------------------- */
    /*  Update the palette if there's been a change in blanking state or    */
    /*  border color.                                                       */
    /* -------------------------------------------------------------------- */
    if ((gfx->pvt->vid_enable & 2) || gfx->b_dirty)
    {
        if (gfx->pvt->vid_enable & 2)
        {
            gfx->pvt->vid_enable &= 1;
            gfx->pvt->vid_enable ^= 1;
            gfx->b_dirty = 3;
        }

        if (gfx->pvt->scaler.bpp == 8)
        {
            /* Defer unblanking via palette until after rendering. */
            deferred_set_color = 1;
        } else
        {
            gfx_set_scaler_palette( gfx->pvt->scr,
                                   &gfx->pvt->scaler,
                                    gfx->pvt->vid_enable ? gfx->pvt->pal_on
                                                         : gfx->pvt->pal_off);

        }
        gfx->dirty |= 2;
    }

    /* -------------------------------------------------------------------- */
    /*  If dirty-rectangle disabled, force a dirty frame to a full flip.    */
    /* -------------------------------------------------------------------- */
    if ((gfx->pvt->flags & GFX_DRECTS) == 0 &&
        (gfx->dirty || gfx->b_dirty))
    {
        gfx->dirty |= 3;
    }

    /* -------------------------------------------------------------------- */
    /*  Run the prescaler if any part of the frame is dirty.                */
    /* -------------------------------------------------------------------- */
    if (gfx->dirty)
        gfx->pvt->prescaler(gfx->vid, gfx->pvt->inter_vid, gfx->pvt->ps_opaque);

    /* -------------------------------------------------------------------- */
    /*  Push whole frame if dirty == 2, else do dirty-rectangle update.     */
    /* -------------------------------------------------------------------- */
    if (gfx->dirty >= 2)
    {
        memset(gfx->pvt->dirty_rows, 0xFF, gfx->pvt->dirty_rows_sz);
        gfx->tick_core(gfx);
        /* Apply deferred color-set just before drawing border, due to buggy */
        /* software palette in SDL. */
        if (deferred_set_color)
        {
            SDL_SetColors(gfx->pvt->scr,
                          gfx->pvt->vid_enable ? gfx->pvt->pal_on
                                               : gfx->pvt->pal_off, 0, 16);
        }
        gfx_draw_bord(gfx);    /* Draw after tick_core in case single-buf'd */
        SDL_Flip(gfx->pvt->scr);
    } else if (gfx->dirty || gfx->b_dirty)
    {
        /* ---------------------------------------------------------------- */
        /*  Compute dirty rectangles based on the intermediate bitmap       */
        /* ---------------------------------------------------------------- */
        gfx_find_dirty_rects(gfx);

        if (gfx->pvt->num_rects > 0)
        {
            /* ------------------------------------------------------------ */
            /*  Expand the source bitmap to final display resolution.       */
            /* ------------------------------------------------------------ */
            gfx->tick_core(gfx);

            /* ------------------------------------------------------------ */
            /*  Actually update the display.                                */
            /* ------------------------------------------------------------ */
            SDL_UpdateRects(gfx->pvt->scr,
                            gfx->pvt->num_rects,
                            gfx->pvt->dirty_rects);
        }
        /* Apply deferred color-set just before drawing border, due to buggy */
        /* software palette in SDL. */
        if (deferred_set_color)
        {
            SDL_SetColors(gfx->pvt->scr,
                          gfx->pvt->vid_enable ? gfx->pvt->pal_on
                                               : gfx->pvt->pal_off, 0, 16);
        }
        gfx_draw_bord(gfx);    /* Draw after tick_core in case single-buf'd */
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

    /* -------------------------------------------------------------------- */
    /*  If rate-limiting our display, record the time /after/ the flip, as  */
    /*  some OSes *cough*OSX*cough* wait for vertical retrace, while other  */
    /*  OSes *cough*Linux*cough* do not.                                    */
    /* -------------------------------------------------------------------- */
    if (gfx->pvt->flags & GFX_SKIP_EXTRA)
    {
        gfx->pvt->last_frame = get_time();
    }

#ifdef BENCHMARK_GFX
    end = get_time();
    diff = end - start;
    if (diff > bm_max) bm_max = diff;
    if (diff < bm_min) bm_min = diff;
    bm_tot += diff;

    if (++bm_cnt == 120)
    {
        jzp_printf("gfx_tick: min = %8.3f max = %8.3f avg = %8.3f\n",
                   bm_min * 1000., bm_max * 1000., bm_tot * 1000. / 120);
        bm_max = bm_tot = 0;
        bm_cnt = 0;
        bm_min = 1e30;
    }
#endif

    /* -------------------------------------------------------------------- */
    /*  If we just toggled between windowed and full-screen, delay a bit.   */
    /* -------------------------------------------------------------------- */
    if (delay_at_end)
        plat_delay(2000);  /* Let monitor come up to speed w/ new res. */
}

/* ======================================================================== */
/*  GFX_TICK_GENERIC -- Services a gfx_t tick in any graphics format        */
/* ======================================================================== */
LOCAL void gfx_tick_generic(gfx_t *gfx)
{
    uint_8 *scr;

    if (SDL_MUSTLOCK(gfx->pvt->scr))
        SDL_LockSurface(gfx->pvt->scr);

    scr = gfx->pvt->ofs_x * gfx->pvt->scr->format->BytesPerPixel +
          gfx->pvt->ofs_y * gfx->pvt->scr->pitch +
          (uint_8 *) gfx->pvt->scr->pixels;

    gfx_scale
    (
        &gfx->pvt->scaler,
        gfx->pvt->inter_vid,
        scr,
        gfx->pvt->scr->pitch,
        gfx->pvt->dirty_rows
    );

    if (SDL_MUSTLOCK(gfx->pvt->scr))
        SDL_UnlockSurface(gfx->pvt->scr);
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

    /* -------------------------------------------------------------------- */
    /*  If we're using the normal STIC blanking behavior, set our "off"     */
    /*  colors to the currently selected border color.  The alternate mode  */
    /*  (which is useful for debugging) sets the blanked colors to be       */
    /*  dimmed versions of the normal palette.                              */
    /* -------------------------------------------------------------------- */
    if (gfx->debug_blank == 0)
    {
        int i;

        for (i = 0; i < 16; i++)
            gfx->pvt->pal_off[i] = gfx->pvt->pal_on[b_color];
    }

    if (dirty)     { gfx->dirty   |= 1; }
    if (dirty & 2) { gfx->b_dirty |= 2; }
}

/* ======================================================================== */
/*  GFX_SCRSHOT      -- Write a 320x200 screen shot to a GIF file.          */
/* ======================================================================== */
LOCAL void gfx_scrshot(uint_8 *scr)
{
    static uint_8 scrshot_buf[320*200];
    static unique_filename_t shot_file_tmpl =
    {
        "shot", ".gif", NULL, 0, 4, 0
    };
    int i, len;

    /* -------------------------------------------------------------------- */
    /*  Open a unique file for the screenshot.                              */
    /* -------------------------------------------------------------------- */
    FILE *f = open_unique_filename(&shot_file_tmpl);

    if (!f)
    {
        fprintf(stderr, "Error:  Could not open '%s' for screen dump.\n",
                shot_file_tmpl.f_name);
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
        jzp_printf("\nWrote screen shot to '%s', %d bytes\n",
                   shot_file_tmpl.f_name, len);
    } else
    {
        jzp_printf("\nError writing screen shot to '%s'\n",
                   shot_file_tmpl.f_name);
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
        static unique_filename_t mvi_file_tmpl = 
        {
            "mvi_", ".imv", NULL, 0, 4, 0
        };

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
        /* ---------------------------------------------------------------- */
        pvt->movie->f = open_unique_filename(&mvi_file_tmpl);
        if (!pvt->movie->f)
        {
            fprintf(stderr, "Error:  Could not open '%s' for movie.\n",
                    mvi_file_tmpl.f_name);
            return;
        }

        jzp_printf("\nStarted movie file '%s'\n", mvi_file_tmpl.f_name);
        jzp_flush();

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
        static unique_filename_t avi_file_tmpl =
        {
            "avi_", ".avi", NULL, 0, 4, 0
        };
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

        avi_file = open_unique_filename(&avi_file_tmpl);

        if (!avi_file)
        {
            fprintf(stderr, "Error:  Could not open '%s' for AVI.\n",
                    avi_file_tmpl.f_name);
            return;
        }

        jzp_printf("\nStarted AVI file '%s'\n", avi_file_tmpl.f_name);
        jzp_flush();

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
/*  GFX_FIND_DIRTY_RECTS -- Finds dirty rectangles in the current image.    */
/*                                                                          */
/*  Current algorithm just divides the display into 240 8x16 tiles aligned  */
/*  with the STIC's cards.  A tile is considered either clean or dirty      */
/*  in its entirety for now.  A tile can be merged with tiles to its        */
/*  right if they're contiguous, or there's a gap of at most one tile.      */
/*                                                                          */
/*  The algorithm is also responsible for copying the new image into the    */
/*  reference image, and constructing a bitmap of which rows need to be     */
/*  expanded by the scaler code.                                            */
/* ======================================================================== */
LOCAL void gfx_find_dirty_rects(gfx_t *gfx)
{
    int x, y, xx, yy, i, j, t;
    int nr = 0, row_start;
    uint_32 *RESTRICT old_pix = (uint_32 *)(void *)gfx->pvt->prev;
    uint_32 *RESTRICT new_pix = (uint_32 *)(void *)gfx->pvt->inter_vid;
    uint_32 is_dirty;
    SDL_Rect *rect = gfx->pvt->dirty_rects;

    int wpitch = gfx->pvt->dr_spec.pitch >> 2;
    int y0 = gfx->pvt->dr_spec.active_first_y;
    int y1 = gfx->pvt->dr_spec.active_last_y + 1;
    int ys = gfx->pvt->dr_spec.y_step;

    int x0 = (gfx->pvt->dr_spec.active_first_x >> 3);
    int x1 = (gfx->pvt->dr_spec.active_last_x  >> 3) + 1;
    int xs = (gfx->pvt->dr_spec.x_step         >> 3);

    int bo = (gfx->pvt->dr_spec.bord_first_x >> 2) +
             (gfx->pvt->dr_spec.bord_first_y * wpitch);

    /* -------------------------------------------------------------------- */
    /*  Set our merge threshold based on whether we're allowed to include   */
    /*  a clean rectangle between two dirty rectangles when coalescing.     */
    /* -------------------------------------------------------------------- */
    t = gfx->pvt->flags & GFX_DRCMRG ? 1 : 0;

    /* -------------------------------------------------------------------- */
    /*  Initally mark all rows clean.                                       */
    /* -------------------------------------------------------------------- */
    memset((void *)gfx->pvt->dirty_rows, 0, gfx->pvt->dirty_rows_sz);

    /* -------------------------------------------------------------------- */
    /*  Scan the source image tile-row-wise looking for differences.        */
    /* -------------------------------------------------------------------- */
    for (y = y0; y < y1; y += ys)
    {
        row_start = nr;

        /* ---------------------------------------------------------------- */
        /*  Find dirty rectangles in this row of cards.                     */
        /* ---------------------------------------------------------------- */
        for (x  = x0; x < x1; x += xs)
        {
            is_dirty = 0;
            switch (xs)
            {
                case 1:
                {
                    for (yy = y; yy < y + ys; yy++)
                        is_dirty  |= (old_pix[yy * wpitch + x*2 + 0] !=
                                      new_pix[yy * wpitch + x*2 + 0])
                                  |  (old_pix[yy * wpitch + x*2 + 1] !=
                                      new_pix[yy * wpitch + x*2 + 1]);
                    break;
                }

                case 2:
                {
                    for (yy = y; yy < y + ys; yy++)
                        is_dirty  |= (old_pix[yy * wpitch + x*2 + 0] !=
                                      new_pix[yy * wpitch + x*2 + 0])
                                  |  (old_pix[yy * wpitch + x*2 + 1] !=
                                      new_pix[yy * wpitch + x*2 + 1])
                                  |  (old_pix[yy * wpitch + x*2 + 2] !=
                                      new_pix[yy * wpitch + x*2 + 2])
                                  |  (old_pix[yy * wpitch + x*2 + 3] !=
                                      new_pix[yy * wpitch + x*2 + 3]);
                    break;
                }

                case 3:
                {
                    for (yy = y; yy < y + ys; yy++)
                        is_dirty  |= (old_pix[yy * wpitch + x*2 + 0] !=
                                      new_pix[yy * wpitch + x*2 + 0])
                                  |  (old_pix[yy * wpitch + x*2 + 1] !=
                                      new_pix[yy * wpitch + x*2 + 1])
                                  |  (old_pix[yy * wpitch + x*2 + 2] !=
                                      new_pix[yy * wpitch + x*2 + 2])
                                  |  (old_pix[yy * wpitch + x*2 + 3] !=
                                      new_pix[yy * wpitch + x*2 + 3])
                                  |  (old_pix[yy * wpitch + x*2 + 4] !=
                                      new_pix[yy * wpitch + x*2 + 4])
                                  |  (old_pix[yy * wpitch + x*2 + 5] !=
                                      new_pix[yy * wpitch + x*2 + 5]);
                    break;
                }

                case 4:
                {
                    for (yy = y; yy < y + ys; yy++)
                        is_dirty  |= (old_pix[yy * wpitch + x*2 + 0] !=
                                      new_pix[yy * wpitch + x*2 + 0])
                                  |  (old_pix[yy * wpitch + x*2 + 1] !=
                                      new_pix[yy * wpitch + x*2 + 1])
                                  |  (old_pix[yy * wpitch + x*2 + 2] !=
                                      new_pix[yy * wpitch + x*2 + 2])
                                  |  (old_pix[yy * wpitch + x*2 + 3] !=
                                      new_pix[yy * wpitch + x*2 + 3])
                                  |  (old_pix[yy * wpitch + x*2 + 4] !=
                                      new_pix[yy * wpitch + x*2 + 4])
                                  |  (old_pix[yy * wpitch + x*2 + 5] !=
                                      new_pix[yy * wpitch + x*2 + 5])
                                  |  (old_pix[yy * wpitch + x*2 + 6] !=
                                      new_pix[yy * wpitch + x*2 + 6])
                                  |  (old_pix[yy * wpitch + x*2 + 7] !=
                                      new_pix[yy * wpitch + x*2 + 7]);
                    break;
                }

                default:
                {
                    for (yy = y; yy < y + ys; yy++)
                        for (xx = x; xx < x + xs; xx++)
                            is_dirty |= (old_pix[yy * wpitch + xx*2 + 0] !=
                                         new_pix[yy * wpitch + xx*2 + 0])
                                     |  (old_pix[yy * wpitch + xx*2 + 1] !=
                                         new_pix[yy * wpitch + xx*2 + 1]);

                    break;
                }
            }

            if (is_dirty)
            {
                rect[nr].x = x;
                rect[nr].y = y;
                rect[nr].w = xs;
                rect[nr].h = ys;
                nr++;
            }
/*fprintf(stderr, "%3d %3d %3d\n", x, y, nr); */
        }

        /* ---------------------------------------------------------------- */
        /*  While it's still hot in the cache, copy "new" to "old"          */
        /* ---------------------------------------------------------------- */
        memcpy((void *)&old_pix[y * wpitch],
               (void *)&new_pix[y * wpitch],
               sizeof(uint_32) * wpitch * ys);

        /* ---------------------------------------------------------------- */
        /*  Mark these rows as dirty in the dirty_row bitmap                */
        /* ---------------------------------------------------------------- */
        if (nr > row_start)
            for (yy = y; yy < y + ys; yy++)
                gfx->pvt->dirty_rows[yy >> 5] |= 1u << (yy & 31);

        /* ---------------------------------------------------------------- */
        /*  Coalesce rectangles if they're adjacent or separated by at      */
        /*  most one clean rectangle.                                       */
        /* ---------------------------------------------------------------- */
        if (nr - row_start < 2)
            continue;

        for (i = row_start, j = row_start + 1; j < nr; j++)
        {
            if (rect[i].x + rect[i].w + t >= rect[j].x)
            {
                rect[i].w = rect[j].x - rect[i].x + rect[j].w;
                continue;
            } else
            {
                rect[++i] = rect[j];
            }
        }

        nr = i + 1;
    }

    /* -------------------------------------------------------------------- */
    /*  If border areas changed color, update those too.                    */
    /*  XXX:  This needs to get fixed when I fix scaler's border handler.   */
    /* -------------------------------------------------------------------- */
    if (old_pix[bo] != new_pix[bo])
    {
        int x0l, x0h, y0l, y0h;     /* upper rectangle */
        int x1l, x1h, y1l, y1h;     /* lower rectangle */

        old_pix[bo] =  new_pix[bo];

        x0l = x1l = gfx->pvt->dr_spec.bord_first_x >> 3;    /* in dwords */
        x0h = x1h = gfx->pvt->dr_spec.bord_last_x  >> 3;    /* in dwords */

        y0l = gfx->pvt->dr_spec.bord_first_y;               /* in pixels */
        y0h = gfx->pvt->dr_spec.active_first_y - 1;         /* in pixels */

        y1l = gfx->pvt->dr_spec.active_last_y + 1;          /* in pixels */
        y1h = gfx->pvt->dr_spec.bord_last_y;                /* in pixels */

        rect[nr].x = x0l;
        rect[nr].y = y0l;
        rect[nr].w = x0h - x0l + 1;
        rect[nr].h = y0h - y0l + 1;
        nr++;

        rect[nr].x = x1l;
        rect[nr].y = y1l;
        rect[nr].w = x1h - x1l + 1;
        rect[nr].h = y1h - y1l + 1;
        nr++;

        for (yy = y0l; yy <= y0h; yy++)
            gfx->pvt->dirty_rows[yy >> 5] |= 1u << (yy & 31);

        for (yy = y1l; yy <= y1h; yy++)
            gfx->pvt->dirty_rows[yy >> 5] |= 1u << (yy & 31);
    }

    /* -------------------------------------------------------------------- */
    /*  Convert the rectangles to display coordinates.  Ick.                */
    /* -------------------------------------------------------------------- */
    for (i = 0; i < nr; i++)
    {
        int w, h;
#ifdef BENCHMARK_GFX
        drw_hist[rect[i].w]++;
#endif
        x = rect[i].x * 8;
        y = rect[i].y;
        w = rect[i].w * 8;
        h = rect[i].h;

        rect[i].x  = gfx->pvt->scaler.scaled_x[x];
        rect[i].y  = gfx->pvt->scaler.scaled_y[y];
        rect[i].w  = gfx->pvt->scaler.scaled_x[x + w] - rect[i].x;
        rect[i].h  = gfx->pvt->scaler.scaled_y[y + h] - rect[i].y;

        rect[i].x += gfx->pvt->ofs_x;
        rect[i].y += gfx->pvt->ofs_y;
    }

    gfx->pvt->num_rects = nr;

#ifdef BENCHMARK_GFX
    dr_hist[nr]++;
#endif

    return;
}

#ifdef BENCHMARK_GFX
LOCAL void gfx_dr_hist_dump(void)
{
    int i;

    jzp_printf("Dirty rectangle counts:\n");
    for (i = 0; i <= 244; i++)
        if (dr_hist[i])
            jzp_printf("%4d: %7d\n", i, dr_hist[i]);

    jzp_printf("Dirty rectangle width counts:\n");
    for (i = 0; i <= 20; i++)
        if (drw_hist[i])
            jzp_printf("%4d: %7d\n", i, drw_hist[i]);
}
#endif

/* ======================================================================== */
/*  GFX_RESYNC   -- Resynchronize GFX after a load.                         */
/* ======================================================================== */
void gfx_resync(gfx_t *gfx)
{
    gfx->dirty   = 3;
    gfx->b_dirty = 3;
}

/* ======================================================================== */
/*  GFX_HIDDEN       -- Returns true if the graphics window is hidden.      */
/* ======================================================================== */
int gfx_hidden(const gfx_t *gfx)
{
    return gfx->hidden;
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
/*          Copyright (c) 1998-2006, Joseph Zbiciak, John Tanner            */
/* ======================================================================== */
