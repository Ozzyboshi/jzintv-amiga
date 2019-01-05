
/*
 * ============================================================================
 *  CP_1600:        CP-1600 Main Core
 *
 *  Author:         J. Zbiciak
 *
 *
 * ============================================================================
 *  CP1600_INIT        --  Initializes the CP-1600 structure to a basic setup
 *  CP1600_RUN         --  Runs the CP1600 for some number of microcycles.
 *  CP1600_RD          --  Perform a read from the CP1600   (macro in cp_1600.h)
 *  CP1600_WR          --  Perform a write from the CP1600  (macro in cp_1600.h)
 * ============================================================================
 *
 *  Notes/To-do:
 *
 *   -- The CP1600_RD_xxx, CP1600_WR_xxx functions must not be called
 *      directly.  Rather, the CP1600_RD, CP1600_WR macros must be used
 *      to assure proper address decoding and dispatch.
 *
 *   -- The CP1600 supports interrupts but doesn't currently have a method for
 *      receiving them.  Peripherals wishing to interrupt the CP1600 need to
 *      somehow set cp1600->intrq to '1' to trigger an interrupt.
 *
 *   -- The CP1600 supports external branch conditions.  Peripherals need to
 *      set cp1600->ext to the appropriate state to trigger these.
 *
 *   -- SIN, TCI, etc. don't do anything yet.  What do they need to do?
 *
 *   -- Functions should be provided for setting up RAM, ROM images, and
 *      registering peripherals.
 *
 * ============================================================================
 */


#include "config.h"
#include "periph/periph.h"
#include "cp1600.h"
#include "op_decode.h"
#include "op_exec.h"
#include "emu_link.h"
#include <limits.h>


LOCAL void cp1600_dtor(periph_p p);

/*
 * ============================================================================
 *  CP1600_INIT        -- Initializes a CP1600_T structure
 *
 *  This function sets up a basic CP1600 structure.  It allocates a local "flat
 *  memory" which corresponds to the CP-1600 memory map, and it sets up the
 *  initial state of the instruction decoder logic.
 *
 *  When it's finished, the CP1600 structure is configured to *not* have any
 *  memory deviced enabled at all.  Calls to "CP1600_ADD_xxx" should be issued
 *  to configure the memory map as appropriate.
 * ============================================================================
 */

int cp1600_init
(
    cp1600_t *const cp1600,
    uint_16   const rst_vec,
    uint_16   const int_vec
)
{
    /* -------------------------------------------------------------------- */
    /*  Avoid problems with a garbage cp1600 structure by setting it to     */
    /*  all-bits-zero.  Note:  This could be a portability problem to       */
    /*  machines which represent NULL pointers as something other than      */
    /*  all-bits-zero, but what interesting modern machines do that?        */
    /* -------------------------------------------------------------------- */
    memset((void*)cp1600, 0, sizeof(cp1600_t));

    /* -------------------------------------------------------------------- */
    /*  Set up our interrupt and reset vectors.                             */
    /* -------------------------------------------------------------------- */
    cp1600->r[7]    = rst_vec;
    cp1600->int_vec = int_vec;

    /* -------------------------------------------------------------------- */
    /*  Initially not single-stepping.                                      */
    /* -------------------------------------------------------------------- */
    cp1600->step_count = 0;
    cp1600->steps_remaining = 0;

    /* -------------------------------------------------------------------- */
    /*  Nothing hooked to the request queue initially.                      */
    /* -------------------------------------------------------------------- */
    req_q_init(&cp1600->req_q);

    /* -------------------------------------------------------------------- */
    /*  Set the entire memory map to not-cacheable.                         */
    /* -------------------------------------------------------------------- */
    /*  TODO:                                                               */
    /*  Outside routines will need to configure the memory map to make the  */
    /*  CP1600 useful.  :-)  It is especially important to set the          */
    /*  cacheable bits for performance.                                     */
    /* -------------------------------------------------------------------- */
    for (unsigned i = 0; 
         i < (1u << (CP1600_MEMSIZE - CP1600_DECODE_PAGE - 5)); i++)
    {
        cp1600->cacheable[i] = 0;
    }

    /* -------------------------------------------------------------------- */
    /*  Mark all instructions as needing decode.  The fn_decode function    */
    /*  will cache the decoded instruction if the "cacheable" bit is set    */
    /*  for the page containing the instruction.                            */
    /* -------------------------------------------------------------------- */
    for (unsigned i = 0; i < (1u << CP1600_MEMSIZE); i++)
    {
        cp1600->execute[i] = fn_decode_1st;
    }

    /* -------------------------------------------------------------------- */
    /*  Set up the CP-1600 as a peripheral.                                 */
    /* -------------------------------------------------------------------- */
    cp1600->periph.read     = NULL;
    cp1600->periph.write    = NULL;
    cp1600->periph.peek     = NULL;
    cp1600->periph.poke     = NULL;
    cp1600->periph.reset    = cp1600_reset;
    cp1600->periph.tick     = cp1600_run;
    cp1600->periph.min_tick = 1;
    cp1600->periph.max_tick = 4;
    cp1600->periph.dtor     = cp1600_dtor;

    cp1600->snoop.read      = NULL;
    cp1600->snoop.write     = cp1600_write; /* Bus snoop for cache inval.   */
    cp1600->snoop.peek      = NULL;
    cp1600->snoop.poke      = cp1600_write; /* Bus snoop for cache inval.   */
    cp1600->snoop.addr_base = 0;
    cp1600->snoop.addr_mask = 0xFFFF;
    cp1600->snoop.tick      = NULL;
    cp1600->snoop.min_tick  = 0;
    cp1600->snoop.max_tick  = ~0U;
    cp1600->snoop.parent    = (void*)cp1600;
    cp1600->snoop.dtor      = NULL;
    return 0;
}

/*
 * ============================================================================
 *  CP1600_RESET       -- Reset the CP1060
 * ============================================================================
 */
void cp1600_reset(periph_t *const p)
{
    cp1600_t *const cp1600 = (cp1600_t*)p;

    cp1600->r[0] = 0;
    cp1600->r[1] = 0;
    cp1600->r[2] = 0;
    cp1600->r[3] = 0;
    cp1600->r[4] = 0;
    cp1600->r[5] = 0;
    cp1600->r[6] = 0;
    cp1600->r[7] = 0x1000;

    cp1600->S = cp1600->C = cp1600->O = cp1600->Z = cp1600->D = 0;
    cp1600->intr = 0;
}

/*
 * ============================================================================
 *  CP1600_RUN         -- Runs the CP1600 for some number of microcycles
 *
 *  This is the main CP1600 loop.  It is responsible for fetching instructions,
 *  decoding them if necessary (or using predecoded instructions if possible)
 *  and calling the required execute functions.
 *
 *  The cp1600_run function will run as many instructions as are necessary to
 *  just barely exceed the specified number of microcycles.  eg.  It will
 *  execute a new instruction if the specified total number of microcycles
 *  has not yet been exceeded.  The new instruction may exceed the specified
 *  number of microcycles.  The total number of microcycles exhausted is
 *  returned as an int.
 * ============================================================================
 */
uint_32 cp1600_run
(
    periph_t *const periph,
    uint_32   const microcycles
)
{
    cp1600_t *const RESTRICT     cp1600     = (cp1600_t *)periph;
    const periph_tick_t RESTRICT instr_tick = cp1600->instr_tick;
    req_q_t *const RESTRICT      req_q      = &cp1600->req_q;
    const uint_64                orig_now   = cp1600->periph.now;
    uint_64                      now        = cp1600->periph.now;
    const uint_64                future     =
        now + microcycles > req_q->horizon ? req_q->horizon 
                                           : now + microcycles;
    uint_64 actual_microcycles = 0;
//printf("cp1600_run %u\n", microcycles);
    /* -------------------------------------------------------------------- */
    /*  Initially, we're not stopped at any sort of breakpoint.             */
    /* -------------------------------------------------------------------- */
    cp1600->hit_breakpoint = BK_NONE;

    /* -------------------------------------------------------------------- */
    /*  Iterate until we've run out of microcycles.  We can slightly        */
    /*  exceed our target.                                                  */
    /* -------------------------------------------------------------------- */
    while (now < future)
    {
        const req_t req       = REQ_Q_FRONT(req_q);
        const int req_valid   = REQ_Q_SIZE(req_q) != 0 &&
                                req.state == REQ_PENDING;
        const int in_req_span = req_valid && req.start <= now && now < req.end;
        const int in_busrq_span   = in_req_span && req.type == REQ_BUS;
        const int in_intrq_span   = in_req_span && req.type == REQ_INT;
        const int before_req_span = req_valid && now <  req.start;
        const int after_req_span  = req_valid && now >= req.end;
        uint_32 pc = cp1600->r[7];
        uint_64 near_future   = future;
        int cycles = 0;
        int instrs = 0;

        assert(!(before_req_span && in_req_span   ));
        assert(!(in_req_span     && after_req_span));
        assert(!(before_req_span && after_req_span));

        /* ---------------------------------------------------------------- */
        /*  Update our INTRQ/BUSRQ flags, in case debugger is watching.     */
        /* ---------------------------------------------------------------- */
        cp1600->req_ack_state = (in_busrq_span ? CP1600_BUSRQ : 0) 
                              | (in_intrq_span ? CP1600_INTRQ : 0);

        /* ---------------------------------------------------------------- */
        /*  Determine if we need to respond to a bus request.               */
        /* ---------------------------------------------------------------- */
        if (in_busrq_span && CP1600_CAN_BUSAK(cp1600))
        {
            cp1600->req_ack_state |= CP1600_BUSAK;
            cycles = (int)(req.end - now);
            req_q->ack(req_q, now);
            now = req.end;
            REQ_Q_POP(req_q);
            goto do_instr_tick;
        }

        /* ---------------------------------------------------------------- */
        /*  Determine if we need to respond to an interrupt request.        */
        /* ---------------------------------------------------------------- */
        if (in_intrq_span && CP1600_CAN_INTAK(cp1600))
        {
            cp1600->req_ack_state |= CP1600_INTAK;

            /* ------------------------------------------------------------ */
            /*  The CPU goes 'dead' for 2 cycles before INTAK.              */
            /* ------------------------------------------------------------ */
            now += 2;
            req_q->ack(req_q, now);
            REQ_Q_POP(req_q);

            /* ------------------------------------------------------------ */
            /*  Then the CPU writes out the current PC at the top of stack. */
            /* ------------------------------------------------------------ */
            cp1600->periph.now = now;
            CP1600_WR(cp1600, cp1600->r[6], cp1600->r[7]);
            cp1600->r[6]++;

            /* ------------------------------------------------------------ */
            /*  10 more cycles pass across INTAK, DW, DWS, IAB, until we    */
            /*  finally get to the first BAR of associated with the ISR.    */
            /* ------------------------------------------------------------ */
            now += 10;
            cp1600->r[7] = pc = cp1600->int_vec;

            cycles = 12;
            goto do_instr_tick;
        }

        /* ---------------------------------------------------------------- */
        /*  If we make it to here, either we are not in a request span, or  */
        /*  we could not acknowledge the request type presented to us.      */
        /* ---------------------------------------------------------------- */
        assert(!in_req_span || !CP1600_CAN_INTAK(cp1600) 
                            || !CP1600_CAN_BUSAK(cp1600));

        /* ---------------------------------------------------------------- */
        /*  If we've gone past a request span, pop it off and loop.  We     */
        /*  may have blown through a BUSRQ or INTRQ.  Ooopsie!  :D          */
        /* ---------------------------------------------------------------- */
        if (after_req_span)
        {
            req_q->drop(req_q, now);
            REQ_Q_POP(req_q);
            /* Go back to the top of the loop to look at next req, if any. */
            continue;   
        }

        /* ---------------------------------------------------------------- */
        /*  Handle step requests.                                           */
        /* ---------------------------------------------------------------- */
        if (cp1600->steps_remaining == 0)
            cp1600->steps_remaining = cp1600->step_count;

        /* ---------------------------------------------------------------- */
        /*  Attempt to execute as many instructions as possible in a tight  */
        /*  loop.  We will only execute up until the next request boundary  */
        /*  or instruction-step boundary.  Also, if an instruction happens  */
        /*  to be a breakpoint, we'll exit early.                           */
        /* ---------------------------------------------------------------- */
        /* If we're before a new request span, run up to the edge of it.    */
        if (before_req_span && near_future > req.start)
            near_future = req.start;

        /* If we're *in* a request span, run for 1 instruction.             */
        if (in_req_span)
            near_future = now + 1;

        while (now < near_future)
        {
            /* ------------------------------------------------------------ */
            /*  Grab our execute function and instruction pointer.          */
            /* ------------------------------------------------------------ */
            const cp1600_ins_t execute = cp1600->execute[pc];
            instr_t     *const instr   = cp1600->instr[pc];
            cp1600->periph.now         = now;
            cp1600->oldpc              = pc;

            /* ------------------------------------------------------------ */
            /*  The flag cp1600->intr is our interruptibility state. It is  */
            /*  set equal to our interrupt enable bit, and is cleared by    */
            /*  non-interruptible instructions, thus making it a "logical-  */
            /*  AND" of the two conditions.                                 */
            /* ------------------------------------------------------------ */
            cp1600->intr = (cp1600->I ? CP1600_INT_ENABLE : 0)
                         | CP1600_INT_INSTR;

            /* ------------------------------------------------------------ */
            /*  Execute the next instruction, and record its cycle count    */
            /*  and new PC value.  Count-down the DBD state.                */
            /* ------------------------------------------------------------ */
            cycles      = execute(instr, cp1600);
            pc          = cp1600->r[7];
            cp1600->D >>= 1;

            /* ------------------------------------------------------------ */
            /*  Tally up instruction count and microcycle count.            */
            /* ------------------------------------------------------------ */
            if (cycles == CYC_MAX)
            {
                cycles = -1;
                break;
            }
            now += cycles;
            instrs++;

            /* ------------------------------------------------------------ */
            /*  Handle instruction step counter.                            */
            /* ------------------------------------------------------------ */
            if (cp1600->steps_remaining > 0 && !--cp1600->steps_remaining)
                break;
        }

        /* ---------------------------------------------------------------- */
        /*  Accumulate instructions.                                        */
        /* ---------------------------------------------------------------- */
        cp1600->tot_instr += instrs;

        /* ---------------------------------------------------------------- */
        /*  If we have an "instruction tick" function registered,           */
        /*  go run it.  This is usually the debugger.                       */
        /* ---------------------------------------------------------------- */
do_instr_tick:
        cp1600->periph.now = now;
        if (instr_tick)
            instr_tick(cp1600->instr_tick_periph, cycles);
    }

    /* -------------------------------------------------------------------- */
    /*  Back out our updates to periph.now and move them to tot_cycles.     */
    /*  We back them out because periph_tick will add them back in.         */
    /*  Crappy software architecture is crappy...                           */
    /* -------------------------------------------------------------------- */
    actual_microcycles  = now - orig_now;
    cp1600->tot_cycle  += actual_microcycles;
    cp1600->periph.now  = orig_now;

    /* -------------------------------------------------------------------- */
    /*  Update our max_tick to account for our ever-shifting sim horizon.   */
    /* -------------------------------------------------------------------- */
    if (now >= req_q->horizon)
    {
        cp1600->periph.min_tick = 1;
        cp1600->periph.max_tick = 1;
    } else
    {
        cp1600->periph.min_tick = 1;
        cp1600->periph.max_tick = req_q->horizon - now;
    }
//printf("max_tick=%llu horizon=%llu now=%llu\n", (unsigned long long)cp1600->periph.max_tick, (unsigned long long)req_q->horizon, (unsigned long long)now);

    /* -------------------------------------------------------------------- */
    /*  Return how long we ran for.                                         */
    /* -------------------------------------------------------------------- */
    return actual_microcycles;
}

/*
 * ============================================================================
 *  CP1600_CACHEABLE     -- Marks a region of instruction space as
 *                          cacheable in the decoder, so that we can cache
 *                          the decoded instructions.  It doesn't actually
 *                          trigger the decode of the instructions though.
 * ============================================================================
 */
void cp1600_cacheable
(
    cp1600_t *const cp1600,
    uint_32   const addr_lo,
    uint_32   const addr_hi,
    int       const need_snoop
)
{
    /* -------------------------------------------------------------------- */
    /*  First, pull in addresses at either end of range.  If address range  */
    /*  doesn't span full decode pages at the ends, then don't include the  */
    /*  pages at the ends of the range.                                     */
    /* -------------------------------------------------------------------- */
    const uint_32 page_mask = (1u << CP1600_DECODE_PAGE) - 1;
    const uint_32 r_addr_lo =  (addr_lo + page_mask)     & ~page_mask;
    const uint_32 r_addr_hi = ((addr_hi + page_mask + 1) & ~page_mask) - 1;

    if (r_addr_hi < r_addr_lo)
        return;

    /* -------------------------------------------------------------------- */
    /*  Now, step through all of the addresses and mark the instructions    */
    /*  cacheable.                                                          */
    /* -------------------------------------------------------------------- */
    for (uint_32 addr = r_addr_lo; addr <= r_addr_hi;
         addr += 1u << CP1600_DECODE_PAGE)
    {
        cp1600->cacheable[addr >> (CP1600_DECODE_PAGE + 5)] |=
                    1u << ((addr >> CP1600_DECODE_PAGE) & 31);
    }

    /* -------------------------------------------------------------------- */
    /*  If this memory range needs snooping (eg. is writable), inform the   */
    /*  peripheral subsystem that the CP-1610 is interested in seeing       */
    /*  memory events in these regions, since it now considers these        */
    /*  locations to be cacheable.                                          */
    /* -------------------------------------------------------------------- */
    if (need_snoop)
        periph_register(cp1600->periph.bus, &cp1600->snoop,
                        r_addr_lo, r_addr_hi, "CP-1610 Snoop");
}

/*
 * ============================================================================
 *  CP1600_INVALIDATE    -- Invalidates a region of cached instructions.
 * ============================================================================
 */
void cp1600_invalidate
(
    cp1600_t *const cp1600,
    uint_32   const addr_lo,
    uint_32   const addr_hi
)
{
    /* -------------------------------------------------------------------- */
    /*  Step through all of the addresses and mark the instructions for     */
    /*  decode.                                                             */
    /* -------------------------------------------------------------------- */
    for (uint_32 addr = addr_lo; addr <= addr_hi; addr ++)
    {
        if (cp1600->execute[addr] != fn_breakpt)
            cp1600->execute[addr] = fn_decode;
        cp1600->disasm [addr] = NULL;
    }
}

/*
 * ============================================================================
 *  CP1600_WRITE         -- Snoops bus writes and invalidates its cache.
 * ============================================================================
 */
void cp1600_write
(
    periph_p const per,         /*  Peripheral being written to.        */
    periph_p const req,         /*  Peripheral requesting the write.    */
    uint_32  const addr,        /*  Address being written.              */
    uint_32  const data         /*  Data being written.                 */
)
{
    const cp1600_p cp1600 = (cp1600_p)per->parent;
    const uint_32 a0 = 0xFFFF & (addr - 0);
    const uint_32 a1 = 0xFFFF & (addr - 1);
    const uint_32 a2 = 0xFFFF & (addr - 2);

    UNUSED(req);
    UNUSED(data);

    /* -------------------------------------------------------------------- */
    /*  Step through "addr - 2" to "addr" to invalidate.                    */
    /* -------------------------------------------------------------------- */
    if (cp1600->execute[a0] != fn_breakpt) cp1600->execute[a0] = fn_decode;
    if (cp1600->execute[a1] != fn_breakpt) cp1600->execute[a1] = fn_decode;
    if (cp1600->execute[a2] != fn_breakpt) cp1600->execute[a2] = fn_decode;

    cp1600->disasm[a0] = NULL;
    cp1600->disasm[a1] = NULL;
    cp1600->disasm[a2] = NULL;
}

/*
 * ============================================================================
 *  CP1600_INSTR_TICK    -- Sets/unsets an per-instruction ticker
 *
 *  Note:  I may eventually split cp1600_run into two flavors that vary
 *  depending on whether or not I have an instr_tick function registered.
 * ============================================================================
 */
void cp1600_instr_tick
(
    cp1600_t      *const cp1600,
    periph_tick_t  const instr_tick,
    periph_p       const instr_tick_periph
)
{
    cp1600->instr_tick        = instr_tick;
    cp1600->instr_tick_periph = instr_tick_periph;
}

/*
 * ============================================================================
 *  CP1600_SET_BREAKPT   -- Sets a breakpoint at a given address.
 *  CP1600_SET_TRACEPT   -- Like a breakpoint, except it resets itself
 *
 *  Note:  Instructions which overlap a breakpoint address but don't start
 *  at the breakpoint address won't trigger the breakpoint.
 * ============================================================================
 */
int cp1600_set_breakpt
(
    cp1600_t *const cp1600,
    uint_16   const addr,
    uint_16   const flags
)
{
    const int was_bkpt = (cp1600->execute[addr] == fn_decode_bkpt ||
                          cp1600->execute[addr] == fn_breakpt);

    if (!cp1600->instr[addr])
        cp1600->instr[addr] = get_instr();

    cp1600->instr[addr]->opcode.breakpt.flags |= flags;

    cp1600->execute[addr] = addr == cp1600->r[7] ? fn_decode_bkpt : fn_breakpt;

    return was_bkpt;
}

/*
 * ============================================================================
 *  CP1600_CLR_BREAKPT   -- Clears a breakpoint at a given address.
 * ============================================================================
 */
void cp1600_clr_breakpt
(
    cp1600_t  *const cp1600,
    uint_16    const addr,
    uint_16    const flags
)
{
    if (cp1600->execute[addr] != fn_breakpt &&
        cp1600->execute[addr] != fn_decode_bkpt)
        return;

    if (!cp1600->instr[addr])
        return;

    cp1600->instr[addr]->opcode.breakpt.flags &= ~flags;

    if (!cp1600->instr[addr]->opcode.breakpt.flags)
        cp1600->execute[addr] = fn_decode;
}

/*
 * ============================================================================
 *  CP1600_LIST_BREAKPTS -- Lists all matching breakpoints via callback.
 * ============================================================================
 */
void cp1600_list_breakpts
(
    const cp1600_t          *const cp1600,
    uint_16                  const flags,
    cp1600_addr_callback_fn *const callback,
    void                    *const opaque
)
{
    uint_32 addr;

    for (addr = 0; addr <= 0xFFFF; addr++)
    {
        if (!cp1600->instr[addr])
            continue;

        const uint_32 instr_flags = cp1600->instr[addr]->opcode.breakpt.flags;
        if ((instr_flags & flags) == flags)
        {
            callback(opaque, addr);
        }
    }
}

/*
 * ============================================================================
 *  CP1600_DTOR          -- Destructor for a cp1600_t              
 * ============================================================================
 */
LOCAL void cp1600_dtor(periph_p const p)
{
    cp1600_t *const cp1600 = (cp1600_t *)p;

    for (uint_32 addr = 0; addr <= 0xFFFF; addr++)
    {
        if (cp1600->instr[addr])
        {
            put_instr(cp1600->instr[addr]);
            cp1600->instr[addr] = NULL;
        }
    }

    emu_link_dtor();
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
/*                 Copyright (c) 1998-2018, Joseph Zbiciak                  */
/* ======================================================================== */
