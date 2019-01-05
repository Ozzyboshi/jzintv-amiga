/* ======================================================================== */
/*  REQ_Q:          Structure to encapsulate INTRQ/BUSRQ/BUSAK stuff.       */
/*  Author:         J. Zbiciak                                              */
/* ======================================================================== */

#ifndef CP1600_REQ_Q_H_
#define CP1600_REQ_Q_H_

/* ------------------------------------------------------------------------ */
/*  Request queue depth. Must be power of 2 large enough to hold an entire  */
/*  display frame, when queuing requests from STIC to CP1600 (as in the     */
/*  Intellivision).                                                         */
/* ------------------------------------------------------------------------ */
#define REQ_Q_DEPTH (32)

enum
{
    REQ_INT = 1, REQ_BUS = 2
};

enum
{
    REQ_INACTIVE = 0,
    REQ_PENDING, REQ_ACKED, REQ_DROPPED
};

/* ------------------------------------------------------------------------ */
/*  REQ_T        -- Interrupt/bus request toward the CPU                    */
/* ------------------------------------------------------------------------ */
typedef struct req_t
{
    uint_64 start;                  /* First cycle of the request           */
    uint_64 end;                    /* First cycle after the request        */
    uint_64 ack_cycle;              /* Cycle CPU acknowledged the request   */
    uint_8  type;                   /* Type of request: INT or BUS          */
    uint_8  state;                  /* Pending, ACK'd, DROPped.             */
} req_t;

/* ------------------------------------------------------------------------ */
/*  REQ_Q        -- A time-decoupled interrupt/bus request queue to CPU.    */
/*                                                                          */
/*  The request bus has a queue of future interrupt and bus requests to     */
/*  the CPU.  This is updated by the requestor whenever the requestor gets  */
/*  opportunity to run.  The request horizon indicates how far ahead the    */
/*  CPU can run before outstripping the contents of the queue.              */
/* ------------------------------------------------------------------------ */
struct req_q_t;
typedef void req_ack_drop_fn(struct req_q_t *r, uint_64 cycle);
typedef struct req_q_t
{
    uint_64 horizon;                /* Simulate CPU no further than this.   */
    req_t   req[REQ_Q_DEPTH];       /* Circular queue of INT, BUS requests  */
    uint_8  wr, rd;                 /* write and read pointers for queue.   */
    /* Acknowledge a request */
    req_ack_drop_fn *ack;
    req_ack_drop_fn *drop;
    void            *opaque;        /* used by ack */
} req_q_t;

/* ------------------------------------------------------------------------ */
/*  The following names are inspired by C++ STL containers.                 */
/*                                                                          */
/*      REQ_Q_SIZE          Number of pending requests coming to the CPU.   */
/*      REQ_Q_FRONT         The next/current request the CPU can see.       */
/*      REQ_Q_POP           Removes front() from the request queue.         */
/*      REQ_Q_PUSH_BACK     Puts a new request on the back of the queue.    */
/*      REQ_Q_CLEAR         Empties out a req_q_t.                          */
/*                                                                          */
/*  Requests are guaranteed to be in monotonically increasing time, so if   */
/*  you call REQ_Q_FRONT() when REQ_Q_SIZE() == 0, you are guaranteed to    */
/*  get an already-expired request.                                         */
/* ------------------------------------------------------------------------ */
extern void req_q_clear(req_q_t *const RESTRICT q);
#define REQ_Q_SIZE(rq) (((rq)->wr - (rq)->rd) & (REQ_Q_DEPTH - 1))
#define REQ_Q_FRONT(rq) ((rq)->req[(rq)->rd & (REQ_Q_DEPTH - 1)])
#define REQ_Q_POP(rq) ((rq)->rd++)
#define REQ_Q_PUSH_BACK(rq,r) ((rq)->req[(rq)->wr++ & (REQ_Q_DEPTH - 1)] = (r))

/* ------------------------------------------------------------------------ */
/*  Print out a request queue (debugging).                                  */
/* ------------------------------------------------------------------------ */
void req_q_print
(
    req_q_t    *const RESTRICT rq, 
    const char *const RESTRICT context
);

/* ------------------------------------------------------------------------ */
/*  REQ_Q_DEFAULT_ACK_FN     -- Acknowledge a request.                      */
/*  REQ_Q_DEFAULT_DROP_FN    -- Mark a request as dropped                   */
/* ------------------------------------------------------------------------ */
void req_q_default_ack_fn
(
    req_q_t *const RESTRICT req_t,
    uint_64  const cycle
);
void req_q_default_drop_fn
(
    req_q_t *const RESTRICT req_t,
    uint_64  const cycle 
);

/* ------------------------------------------------------------------------ */
/*  REQ_Q_INIT   -- Initialize a request queue.                             */
/* ------------------------------------------------------------------------ */
void req_q_init
(
    req_q_t *const RESTRICT req_q
);

#endif /* CP1600_REQ_Q_H_ */

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
/*                   Copyright (c) 2017, Joseph Zbiciak                     */
/* ======================================================================== */
