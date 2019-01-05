#include "config.h"
#include "misc/types.h"
#include "misc/printer.h"

/* ------------------------------------------------------------------------ */
/*  VAL_TRY_PARSE_DECNUM                                                    */
/*                                                                          */
/*  See if this variable has a numeric value that could be interpreted      */
/*  as decimal.  Only succeed if all characters are 0-9.                    */
/* ------------------------------------------------------------------------ */
void val_try_parse_decnum( val_strnum_t *const val )
{
    int num_dig;
    if ( !val || !val->str_val )
        return;
    
    num_dig = strspn(val->str_val, "0123456789");

    /* Are all the characters decimal digits? */
    if (num_dig > 0 && val->str_val[num_dig] == 0)
    {
        val->dec_val = atoi(val->str_val);
        val->flag   |= VAL_DECNUM;
    }
}

/* ------------------------------------------------------------------------ */
/*  VAL_TRY_PARSE_HEXNUM                                                    */
/*                                                                          */
/*  See if this variable has a numeric value that could be interpreted      */
/*  as decimal.  Only succeed if all characters are 0-9, A-F, a-f, with     */
/*  the minor exception that '$' is allowed as a prefix.                    */
/* ------------------------------------------------------------------------ */
void val_try_parse_hexnum( val_strnum_t *const val )
{
    char *first;
    int num_dig;

    if ( !val || !val->str_val )
        return;

    first = val->str_val;
    first += (*first == '$');

    num_dig = strspn(first, "0123456789abcdefABCDEF");

    /* Are all the characters hexadecimal digits? */
    if (num_dig > 0 && first[num_dig] == 0)
    {
        val->hex_val = strtoul(first, NULL, 16);
        val->flag   |= VAL_HEXNUM;
    }
}

/* ------------------------------------------------------------------------ */
/*  VAL_TRY_PARSE_DATE                                                      */
/*                                                                          */
/*  See if this variable has a date-like string in one of the following     */
/*  formats, or a subset.  Fields can be missing from the right.            */
/*                                                                          */
/*      YYYY/MM/DD HH:MM:SS +hh:mm                                          */
/*      YYYY-MM-DD HH:MM:SS +hhmm                                           */
/*      YYYY/MM/DD HH:MM:SS +hh:mm                                          */
/*      YYYY-MM-DD HH:MM:SS +hhmm                                           */
/*                                                                          */
/*  If so, populate the date_val structure and set the VAL_DATE flag.       */
/*  The VAL_DATE is not used for printing most places, but rather just      */
/*  provided as a convenience to date-consuming code.                       */
/* ------------------------------------------------------------------------ */
void val_try_parse_date( val_strnum_t *const val )
{
    int cvt1, y1, m1, d1, hh1, mm1, ss1, hhh1, mmm1;
    int cvt2, y2, m2, d2, hh2, mm2, ss2, hhh2, mmm2;
    char p1, p2;
    int y, m, d, hh, mm, ss, p, hhh, mmm;
    int utc_delta = UTC_DELTA_UNKNOWN;

    if ( !val || !val->str_val )
        return;

    y1 = m1 = d1 = p1 = 0;
    hh1 = mm1 = ss1 = hhh1 = mmm1 = -1;
    cvt1 = sscanf( val->str_val, "%d/%d/%d %d:%d:%d %c%d:%d", &y1, &m1, &d1,
                   &hh1, &mm1, &ss1, &p1, &hhh1, &mmm1);

    y2 = m2 = d2 = p2 = 0;
    hh2 = mm2 = ss2 = hhh2 = mmm2 = -1;
    cvt2 = sscanf( val->str_val, "%d-%d-%d %d:%d:%d %c%d:%d", &y2, &m2, &d2,
                   &hh2, &mm2, &ss2, &p2, &hhh2, &mmm2);

    if ( cvt1 == 0 && cvt2 == 0 )
        return;

    y   = cvt1 > cvt2 ? y1   : y2;
    m   = cvt1 > cvt2 ? m1   : m2;
    d   = cvt1 > cvt2 ? d1   : d2;
    hh  = cvt1 > cvt2 ? hh1  : hh2;
    mm  = cvt1 > cvt2 ? mm1  : mm2;
    ss  = cvt1 > cvt2 ? ss1  : ss2;
    p   = cvt1 > cvt2 ? p1   : p2;    // '+' or '-'
    hhh = cvt1 > cvt2 ? hhh1 : hhh2;
    mmm = cvt1 > cvt2 ? mmm1 : mmm2;

    if ( y > 0 && y < 100 )                 { y += 1900; }
    if ( y < 1901 || y > 1900 + 255 )       { y =     0; }
    if ( !y      || m < 0 || m > 12 )       { m =     0; }
    if ( !m      || d < 0 || d > 31 )       { d =     0; }
    if ( !d      || hh < 0 || hh > 23 )     { hh =   -1; }
    if ( hh < 0  || mm < 0 || mm > 59 )     { mm =   -1; }
    if ( mm < 0  || ss < 0 || ss > 60 )     { ss =   -1; }
    if ( ss < 0  || (p != '-' && p != '+')) { p =     0; }

    if (p && hhh >= 0 && mmm < 0)
    {
        mmm = hhh % 100;
        hhh /= 100;
    } 
    else if (!p || hhh < 0 || hhh > 12 || mmm < 0 || mmm > 59)
    { 
        p   = 0;
        hhh = -1;
        mmm = -1;
    }

    if ( p )
    {
        utc_delta  = 60 * (p == '-' ? -hhh : hhh);
        utc_delta += (p == '-' ? -mmm : mmm);
        if (utc_delta < -720 || utc_delta > 720)
            utc_delta = UTC_DELTA_UNKNOWN;
    }

    if ( y )
    {
        val->flag |= VAL_DATE;
        val->date_val.year      = y;
        val->date_val.month     = m;
        val->date_val.day       = d;
        val->date_val.hour      = hh;
        val->date_val.min       = mm;
        val->date_val.sec       = ss;
        val->date_val.utc_delta = utc_delta;
    }
}

/* ------------------------------------------------------------------------ */
/*  GAME_DATE_TO_STRING                                                     */
/*  Convert a game_date_t to a string.  The string is malloc'd.  Returns    */
/*  NULL on failure.                                                        */
/* ------------------------------------------------------------------------ */
char *game_date_to_string( const game_date_t *const date )
{
    char buf[80];
    const int y = date->year;
    const int m = date->month;
    const int d = date->day;
    const int hh = date->hour;
    const int mm = date->min;
    const int ss = date->sec;

    if ( !date || !date->year )
        return NULL;

    if (y && m && d && hh >= 0 && mm >= 0 && ss >= 0 &&
        date->utc_delta >= -720 && date->utc_delta <= 720 )
    {
        const int uc = date->utc_delta < 0 ? '-' : '+';
        const int ad = abs(date->utc_delta);
        const int uh = ad / 60;
        const int um = ad % 60;

        sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d %c%02d%02d",
                y, m, d, hh, mm, ss, uc, uh, um);
    } 
    else if (y && m && d && hh >= 0 && mm >= 0 && ss >= 0)
        sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d", y, m, d, hh, mm, ss);
    else if (y && m && d && hh >= 0 && mm >= 0)
        sprintf(buf, "%04d-%02d-%02d %02d:%02d", y, m, d, hh, mm);
    else if (y && m && d && hh >= 0)
        sprintf(buf, "%04d-%02d-%02d %02d", y, m, d, hh);
    else if (y && m && d)
        sprintf(buf, "%04d-%02d-%02d", y, m, d);
    else if (y && m)
        sprintf(buf, "%04d-%02d", y, m);
    else 
        sprintf(buf, "%04d", y);

    return strdup( buf );
}

/* ------------------------------------------------------------------------ */
/*  GAME_DATE_TO_UINT_8  -- Convert date to serialized .ROM/.LUIGI format.  */
/*  Note: buf[] must have room for up to 8 bytes.                           */
/* ------------------------------------------------------------------------ */
int game_date_to_uint_8(const game_date_t *const d, uint_8 *const data)
{
    int length = 0;

    if (d->year)        data[length++] = d->year - 1900;    else return length;
    if (d->month)       data[length++] = d->month;          else return length;
    if (d->day)         data[length++] = d->day;            else return length;
    if (d->hour >= 0)   data[length++] = d->hour;           else return length;
    if (d->min >= 0)    data[length++] = d->min;            else return length;
    if (d->sec >= 0)    data[length++] = d->sec;            else return length;
    if (d->utc_delta != UTC_DELTA_UNKNOWN)
    {
        const int ud = d->utc_delta;
        int uh, um;

        /* For -ve numbers, round toward -oo so -01:30 becomes      */
        /* -2, +30; that is, the minutes are always a +ve offset    */

        if (ud < 0) uh = -(((59 - ud)) / 60);   /* Round to -oo */
        else        uh = ud / 60;               /* Round to -oo */

        um = ud - uh * 60;                      /* Positive offset */

        if (uh >= -12 && uh <= 12)
        {
            data[length++] = uh;
            if (um)
                data[length++] = um;
        }
    }

    return length;
}

/* ------------------------------------------------------------------------ */
/*  UINT_8_TO_GAME_DATE  -- Deserialize a serialized game date.             */
/*  Returns 0 on success, non-zero on failure.                              */
/* ------------------------------------------------------------------------ */
int uint_8_to_game_date(game_date_t *const d, const uint_8 *const data,
                        const int length)
{
    if (length < 1 || length > 8)
        return -1;

    int utc_delta = UTC_DELTA_UNKNOWN;
    int ud_byte = 0;

    if (length < 1 || length > 8)
        return -1;

    /* Not a full validation of dates, but a reasonable sanity check. */
    if (length > 1 && data[1] > 12)
        return -1;

    if (length > 2 && data[2] > 31)
        return -1;

    if (length > 3 && data[3] > 23)
        return -1;

    if (length > 4 && data[4] > 59)
        return -1;

    if (length > 5 && data[5] > 60)
        return -1;

    if (length > 6)
    {
        ud_byte = (data[6] ^ 0x80) - 0x80;
        if (ud_byte < -12 || ud_byte > 12)
            return -1;
    }

    if (length > 7 && data[7] > 59)
        return -1;

    d->year  = data[0] + 1900;
    d->month = length > 1 ? data[1] : 0;
    d->day   = length > 2 ? data[2] : 0;
    d->hour  = length > 3 ? data[3] : -1;
    d->min   = length > 4 ? data[4] : -1;
    d->sec   = length > 5 ? data[5] : -1;

    if (length > 6)
    {
        utc_delta = 60 * ud_byte;
        if (length > 7)
            utc_delta += data[7];
    }

    d->utc_delta = utc_delta;

    return 0;
}

/* ------------------------------------------------------------------------ */
/*  VAL_ADD_DATE_STRING                                                     */
/*  If a val has a date but no string, generate the string if possible.     */
/* ------------------------------------------------------------------------ */
void val_add_date_string( val_strnum_t *const val )
{
    if ( !val || !VAL_HAS_DATE( *val ) || VAL_HAS_STRING( *val ) )
        return;

    char *date_string = game_date_to_string( &(val->date_val) );

    if ( date_string )
    {
        val->flag  |= VAL_STRING;
        val->str_val = date_string;
    }
}

/* ------------------------------------------------------------------------ */
/*  FREE_CFG_VAR         -- Free a single CFG_VAR.                          */
/*  FREE_CFG_VAR_LIST    -- Free a list of CFG_VAR_T.                       */
/* ------------------------------------------------------------------------ */
void free_cfg_var( cfg_var_t *var )
{
    CONDFREE(var->name);
    CONDFREE(var->val.str_val);
    free(var);
}

void free_cfg_var_list( cfg_var_t *head )
{
    cfg_var_t *prev = head;

    while ( head )
    {
        prev = head;
        head = (cfg_var_t *)head->l.next;
        free_cfg_var( prev );
    }
}


/* ------------------------------------------------------------------------ */
/*  CFG_QUOTE_STR    -- Quote a string if necessary.  Non-reentrant.        */
/*                      Used for strings in vars and other CFG stuff.       */
/* ------------------------------------------------------------------------ */
const char *cfg_quote_str( const char *str )
{
    static char *buf = NULL;
    static unsigned buf_sz = 0;
    const char *si;
    char *so;
    size_t req_sz = 3;

    size_t idx = strcspn( str, " \t\r\n;[]$=-,\"" );
    if (!str[idx])
        return str;

    si = str;
    while (*si)
    {
        if (*si == '"' || *si == '\\' ) req_sz++;
        req_sz++;
        si++;
    }

    if (buf_sz < req_sz)
    {
        buf_sz = req_sz * 2;
        buf = (char *)realloc( (void *)buf, buf_sz );
        if (!buf)
        {
            buf_sz = 0;
            buf = NULL;
            return str;
        }
    }

    si = str;
    so = buf;
    *so++ = '"';
    while (*si)
    {
        if (*si == '"' || *si == '\\') *so++ = '\\';
        *so++ = *si++;
    }
    *so++ = '"';
    *so++ = 0;

    return buf;
}

/* ------------------------------------------------------------------------ */
/*  PRINT_CFG_VAR        -- Print <name> = <value> tuple.                   */
/*  PRINT_CFG_VAR_LIST   -- Print a list of cfg_vars                        */
/* ------------------------------------------------------------------------ */
void print_cfg_var
(
    cfg_var_t *RESTRICT const var,
    printer_t *RESTRICT const p
)
{
    if ( VAL_HAS_DECNUM( var->val ) )
    {
        p->fxn(p->opq, "%s = %d\015\012", var->name, var->val.dec_val);
    } else if ( VAL_HAS_HEXNUM( var->val ) )
    {
        p->fxn(p->opq, "%s = $%.4X\015\012", var->name, var->val.hex_val);
    } else
    {
        p->fxn(p->opq, "%s = %s\015\012", var->name,
        cfg_quote_str( var->val.str_val ) );
    }
}


void print_cfg_var_list
(
    cfg_var_t *RESTRICT const head,
    printer_t *RESTRICT const p
)
{
    cfg_var_t *curr = head;

    while ( curr )
    {
        print_cfg_var( curr, p );
        curr = (cfg_var_t *)curr->l.next;
    }
}

/* ------------------------------------------------------------------------ */
/*  CONS_CFG_VAR_DEC     -- Construct a decimal config variable.            */
/*  CONS_CFG_VAR_HEX     -- Construct a hexadecimal config variable.        */
/*  CONS_CFG_VAR_STRING  -- Construct a string config variable.             */
/* ------------------------------------------------------------------------ */
cfg_var_t *cons_cfg_var_dec
(
    const char *RESTRICT const name,
    const sint_32              value
)
{
    char buf[32];
    cfg_var_t *var = CALLOC(cfg_var_t, 1);
    char *name_str;
    char *val_str;

    if (!var)
        return NULL;

    name_str = strdup(name);
    if (!name_str)
    {
        free(var);
        return NULL;
    }
    var->name = name_str;

    sprintf(buf, "%d", value);
    val_str = strdup(buf);

    var->val.flag = VAL_DECNUM | (val_str ? VAL_STRING : 0);
    var->val.dec_val = value;
    var->val.str_val = val_str;

    return var;
}

cfg_var_t *cons_cfg_var_hex
(
    const char *RESTRICT const name,
    const uint_32              value
)
{
    char buf[32];
    cfg_var_t *var = CALLOC(cfg_var_t, 1);
    char *name_str;
    char *val_str;

    if (!var)
        return NULL;

    name_str = strdup(name);
    if (!name_str)
    {
        free(var);
        return NULL;
    }
    var->name = name_str;

    sprintf(buf, "%x", value);
    val_str = strdup(buf);

    var->val.flag = VAL_HEXNUM | (val_str ? VAL_STRING : 0);
    var->val.hex_val = value;
    var->val.str_val = val_str;

    return var;
}

cfg_var_t *cons_cfg_var_string
(
    const char *RESTRICT const name,
    const char *RESTRICT const value
)
{
    cfg_var_t *var = CALLOC(cfg_var_t, 1);
    char *name_str = NULL;
    char *val_str  = NULL;

    if (!var)
        return NULL;

    name_str = strdup(name);
    if (!name_str)
    {
        free(var);
        return NULL;
    }
    var->name = name_str;

    val_str = strdup(value);
    if (!val_str)
    {
        free(var);
        free(name_str);
        return NULL;
    }

    var->val.flag = VAL_STRING;
    var->val.str_val = val_str;

    return var;
}

cfg_var_t *cons_cfg_var_date
(
    const char        *RESTRICT const name,
    const game_date_t *RESTRICT const value
)
{
    cfg_var_t *var = CALLOC(cfg_var_t, 1);
    char *name_str = NULL;

    if (!var)
        return NULL;

    name_str = strdup(name);
    if (!name_str)
    {
        free(var);
        return NULL;
    }

    var->name         = name_str;
    var->val.flag     = VAL_DATE;
    var->val.date_val = *value;

    val_add_date_string( &(var->val) );

    return var;
}

/* ------------------------------------------------------------------------ */
/*  CONS_CFG_VAR_KV_STR  -- Construct a config variable from a key=value    */
/*                          string (such as get packed in LUIGIs).          */
/*                                                                          */
/*  This function has to guess at the value type, whether it's a date,      */
/*  number, or generic string.  It's meant for reconstituting cfg_var_t's   */
/*  that were serialized under a "misc" category, as they had no other      */
/*  category at the time of serialization.                                  */
/* ------------------------------------------------------------------------ */
cfg_var_t *cons_cfg_var_kv_str
(
    const char *RESTRICT kv_str
)
{
    const char *div = NULL;
    char *name_str  = NULL;
    char *val_str   = NULL;
    cfg_var_t *var  = NULL;

    div = strchr(kv_str, '=');
    if (!div)
        goto fail;

    /* CALLOC ensures nul-termination here. */
    if (!(name_str = CALLOC(char, div - kv_str + 1)))
        goto fail;

    memcpy(name_str, kv_str, div - kv_str);     /* do not copy the '=' */

    if (!(val_str = strdup(div + 1)))
        goto fail;

    if (!(var = CALLOC(cfg_var_t, 1)))
        goto fail;

    var->name        = name_str;
    var->val.str_val = val_str;
    var->val.flag    = VAL_STRING;

    val_try_parse_decnum( &(var->val) );
    val_try_parse_hexnum( &(var->val) );
    val_try_parse_date  ( &(var->val) );

    return var;

fail:
    CONDFREE(var);
    CONDFREE(val_str);
    CONDFREE(name_str);

    return NULL;
}


/* ------------------------------------------------------------------------ */
/*  APPEND_CFG_VAR       -- Appends a new config var to a list.             */
/*                          Returns the appended var.                       */
/*                                                                          */
/*  Intended to be used as follows:                                         */
/*                                                                          */
/*      var = append_cfg_var( &head, cons_cfg_var_XXX( "name", val ) );     */
/*                                                                          */
/*  If the cons failes, 'var' will be NULL and the list remains unchanged.  */
/* ------------------------------------------------------------------------ */
cfg_var_t *append_cfg_var
(
    cfg_var_t *RESTRICT *RESTRICT const head,
    cfg_var_t           *RESTRICT const var
)
{
    cfg_var_t **curr = head;

    if (!var)
        return NULL;

    while (*curr)
        curr = (cfg_var_t **)&((*curr)->l.next);
    
    *curr = var;
    return var;
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
/* ------------------------------------------------------------------------ */
/*                 Copyright (c) 2003-+Inf, Joseph Zbiciak                  */
/* ======================================================================== */
