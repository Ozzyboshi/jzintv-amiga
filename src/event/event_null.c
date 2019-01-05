/*
 * ============================================================================
 *  Title:    Null Event Handling Subsystem
 *  Author:   J. Zbiciak
 * ============================================================================
 *  EVENT_INIT       -- Initializes the Event Subsystem
 *  EVENT_TICK       -- Processes currently pending events in the event queue
 *  EVENT_MAP        -- Map an event to a set/clear mask and a word pointer.
 * ============================================================================
 */

#include "config.h"
#include "periph/periph.h"
#include "event/event.h"

uint_32 event_count = 0;

/* ======================================================================== */
/*  EVENT_INIT       -- Initializes the Event subsystem.                    */
/* ======================================================================== */
int event_init(event_t *event, int enable_mouse)
{
    UNUSED(enable_mouse);

    /* -------------------------------------------------------------------- */
    /*  Make this peripheral do nothing.                                    */
    /* -------------------------------------------------------------------- */
    event->periph.read      = NULL;
    event->periph.write     = NULL;
    event->periph.peek      = NULL;
    event->periph.poke      = NULL;
    event->periph.tick      = NULL;
    event->periph.min_tick  = ~0U;
    event->periph.max_tick  = ~0U;
    event->periph.addr_base = ~0U;
    event->periph.addr_mask = 0;
    event->periph.dtor      = NULL;
    return 0;

}

/*
 * ============================================================================
 *  EVENT_MAP        -- Maps an event to a particular AND/OR mask set
 *
 *  NOTE:  This implementation currently uses a gawdawful slow linear search
 *  to look up event names.  Eventually, I'll sort the event name list and
 *  use a binary search, which should be many times faster.  I can justify
 *  this slow lookup for now since this event mapping only occurs at startup.
 * ============================================================================
 */
int event_map
(
    event_t     *event,         /* Event_t structure being set up.          */
    const char  *name,          /* Name of event to map.                    */
    int         map,            /* Keyboard map number to map within.       */
    v_uint_32   *word,          /* Word modified by event, (NULL to ignore) */
    uint_32     and_mask[2],    /* AND masks for event up/down.             */
    uint_32     or_mask[2]      /* OR masks for event up/down.              */
)
{
    UNUSED(event);
    UNUSED(name);
    UNUSED(map);
    UNUSED(word);
    UNUSED(and_mask);
    UNUSED(or_mask);

    return 0;
}

/*
 * ============================================================================
 *  EVENT_COMBINE    -- Register a combo event as COMBOxx
 * ============================================================================
 */
int event_combine
(
    event_t     *event,
    const char  *name1,
    const char  *name2,
    int         combo_num
)
{
    UNUSED(event);
    UNUSED(name1);
    UNUSED(name2);
    UNUSED(combo_num);
    return 0;
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
/*                 Copyright (c) 1998-2017, Joseph Zbiciak                  */
/* ======================================================================== */
