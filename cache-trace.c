#include "cache-trace.h"


uint64_t sim_time;


/* the following array is used to deal with def-use register interlocks, which we
 * can compute statically, very fortunately.
 *
 * the idea is that interlock_base contains the number of cycles "executed" from
 * the start of a basic block. It is set to 0 in trace_bb_start, and incremented
 * in each call to get_insn_ticks.
 *
 * interlocks[N] correspond to the value of interlock_base after which a register N
 * can be used by another operation, it is set each time an instruction writes to
 * the register in get_insn_ticks()
 */

static int   interlocks[16];
static int   interlock_base;

static void
_interlock_def(int  reg, int  delay)
{
    if (reg >= 0)
        interlocks[reg] = interlock_base + delay;
}

static int _interlock_use(int  reg)
{
    int  delay = 0;

    if (reg >= 0)
    {
        delay = interlocks[reg] - interlock_base;
        if (delay < 0)
            delay = 0;
    }
    return delay;
}


// Define the number of clock ticks for some instructions.  Add one to these
// (in some cases) if there is an interlock.  We currently do not check for
// interlocks.
#define INTERLOCK_TICKS_OTHER	1
#define INTERLOCK_TICKS_SMULxy	1
#define INTERLOCK_TICKS_SMLAWy	1
#define INTERLOCK_TICKS_SMLALxy	2
#define INTERLOCK_TICKS_MUL	    2
#define INTERLOCK_TICKS_MLA	    2
#define INTERLOCK_TICKS_MULS	4	// no interlock penalty
#define INTERLOCK_TICKS_MLAS	4	// no interlock penalty
#define INTERLOCK_TICKS_UMULL	3
#define INTERLOCK_TICKS_UMLAL	3
#define INTERLOCK_TICKS_SMULL	3
#define INTERLOCK_TICKS_SMLAL	3
#define INTERLOCK_TICKS_UMULLS	5	// no interlock penalty
#define INTERLOCK_TICKS_UMLALS	5	// no interlock penalty
#define INTERLOCK_TICKS_SMULLS	5	// no interlock penalty
#define INTERLOCK_TICKS_SMLALS	5	// no interlock penalty

/* 
 * Compute the number of cycles that this instruction will take,
 * not including any I-cache or D-cache misses.  This function
 * is called for each instruction in a basic block when that
 * block is being translated. 
 */

unsigned int get_insn_ticks(uint32_t insn) {
    unsigned int result = 1;

    /* See Chapter 12 of the ARM920T Reference Manual for details about clock cycles */

    /* first check for invalid condition codes */
    if ((insn >> 28) == 0xf)
    {
        if ((insn >> 25) == 0x7d) {  /* BLX */
            result = 3;
            goto Exit;
        }
        /* XXX: if we get there, we're either in an UNDEFINED instruction     */
        /*      or in co-processor related ones. For now, only return 1 cycle */
        goto Exit;
    }

    /* other cases */
    switch ((insn >> 25) & 7)
    {
        case 0:
            if ((insn & 0x00000090) == 0x00000090)  /* Multiplies, extra load/store, Table 3-2 */
            {
                /* XXX: TODO: Add support for multiplier operand content penalties in the translator */

                if ((insn & 0x0fc000f0) == 0x00000090)   /* 3-2: Multiply (accumulate) */
                {
                    int  Rm = (insn & 15);
                    int  Rs = (insn >> 8) & 15;
                    int  Rn = (insn >> 12) & 15;

                    if ((insn & 0x00200000) != 0) {  /* MLA */
                        result += _interlock_use(Rn);
                    } else {   /* MLU */
                        if (Rn != 0)      /* UNDEFINED */
                            goto Exit;
                    }
                    /* cycles=2+m, assume m=1, this should be adjusted at interpretation time */
                    result += 2 + _interlock_use(Rm) + _interlock_use(Rs);
                }
                else if ((insn & 0x0f8000f0) == 0x00800090)  /* 3-2: Multiply (accumulate) long */
                {
                    int  Rm   = (insn & 15);
                    int  Rs   = (insn >> 8) & 15;
                    int  RdLo = (insn >> 12) & 15;
                    int  RdHi = (insn >> 16) & 15;

                    if ((insn & 0x00200000) != 0) { /* SMLAL & UMLAL */
                        result += _interlock_use(RdLo) + _interlock_use(RdHi);
                    }
                    /* else SMLL and UMLL */

                    /* cucles=3+m, assume m=1, this should be adjusted at interpretation time */
                    result += 3 + _interlock_use(Rm) + _interlock_use(Rs);
                }
                else if ((insn & 0x0fd00ff0) == 0x01000090)  /* 3-2: Swap/swap byte */
                {
                    int  Rm = (insn & 15);
                    int  Rd = (insn >> 8) & 15;

                    result = 2 + _interlock_use(Rm);
                    _interlock_def(Rd, result+1);
                }
                else if ((insn & 0x0e400ff0) == 0x00000090)  /* 3-2: load/store halfword, reg offset */
                {
                    int  Rm = (insn & 15);
                    int  Rd = (insn >> 12) & 15;
                    int  Rn = (insn >> 16) & 15;

                    result += _interlock_use(Rn) + _interlock_use(Rm);
                    if ((insn & 0x00100000) != 0)  /* it's a load, there's a 2-cycle interlock */
                        _interlock_def(Rd, result+2);
                }
                else if ((insn & 0x0e400ff0) == 0x00400090)  /* 3-2: load/store halfword, imm offset */
                {
                    int  Rd = (insn >> 12) & 15;
                    int  Rn = (insn >> 16) & 15;

                    result += _interlock_use(Rn);
                    if ((insn & 0x00100000) != 0)  /* it's a load, there's a 2-cycle interlock */
                        _interlock_def(Rd, result+2);
                }
                else if ((insn & 0x0e500fd0) == 0x000000d0) /* 3-2: load/store two words, reg offset */
                {
                    /* XXX: TODO: Enhanced DSP instructions */
                }
                else if ((insn & 0x0e500fd0) == 0x001000d0) /* 3-2: load/store half/byte, reg offset */
                {
                    int  Rm = (insn & 15);
                    int  Rd = (insn >> 12) & 15;
                    int  Rn = (insn >> 16) & 15;

                    result += _interlock_use(Rn) + _interlock_use(Rm);
                    if ((insn & 0x00100000) != 0)  /* load, 2-cycle interlock */
                        _interlock_def(Rd, result+2);
                }
                else if ((insn & 0x0e5000d0) == 0x004000d0) /* 3-2: load/store two words, imm offset */
                {
                    /* XXX: TODO: Enhanced DSP instructions */
                }
                else if ((insn & 0x0e5000d0) == 0x005000d0) /* 3-2: load/store half/byte, imm offset */
                {
                    int  Rd = (insn >> 12) & 15;
                    int  Rn = (insn >> 16) & 15;

                    result += _interlock_use(Rn);
                    if ((insn & 0x00100000) != 0)  /* load, 2-cycle interlock */
                        _interlock_def(Rd, result+2);
                }
                else
                {
                    /* UNDEFINED */
                }
            }
            else if ((insn & 0x0f900000) == 0x01000000)  /* Misc. instructions, table 3-3 */
            {
                switch ((insn >> 4) & 15)
                {
                    case 0:
                        if ((insn & 0x0fb0fff0) == 0x0120f000) /* move register to status register */
                        {
                            int  Rm = (insn & 15);
                            result += _interlock_use(Rm);
                        }
                        break;

                    case 1:
                        if ( ((insn & 0x0ffffff0) == 0x01200010) ||  /* branch/exchange */
                             ((insn & 0x0fff0ff0) == 0x01600010) )   /* count leading zeroes */
                        {
                            int  Rm = (insn & 15);
                            result += _interlock_use(Rm);
                        }
                        break;

                    case 3:
                        if ((insn & 0x0ffffff0) == 0x01200030)   /* link/exchange */
                        {
                            int  Rm = (insn & 15);
                            result += _interlock_use(Rm);
                        }
                        break;

                    default:
                        /* TODO: Enhanced DSP instructions */
                        ;
                }
            }
            else  /* Data processing */
            {
                int  Rm = (insn & 15);
                int  Rn = (insn >> 16) & 15;

                result += _interlock_use(Rn) + _interlock_use(Rm);
                if ((insn & 0x10)) {   /* register-controlled shift => 1 cycle penalty */
                    int  Rs = (insn >> 8) & 15;
                    result += 1 + _interlock_use(Rs);
                }
            }
            break;

        case 1:
            if ((insn & 0x01900000) == 0x01900000)
            {
                /* either UNDEFINED or move immediate to CPSR */
            }
            else  /* Data processing immediate */
            {
                int  Rn = (insn >> 12) & 15;
                result += _interlock_use(Rn);
            }
            break;

        case 2:  /* load/store immediate */
            {
                int  Rn = (insn >> 16) & 15;

                result += _interlock_use(Rn);
                if (insn & 0x00100000) {  /* LDR */
                    int  Rd = (insn >> 12) & 15;

                    if (Rd == 15)  /* loading PC */
                        result = 5;
                    else
                        _interlock_def(Rd,result+1);
                }
            }
            break;

        case 3:
            if ((insn & 0x10) == 0)  /* load/store register offset */
            {
                int  Rm = (insn & 15);
                int  Rn = (insn >> 16) & 15;

                result += _interlock_use(Rm) + _interlock_use(Rn);

                if (insn & 0x00100000) {  /* LDR */
                    int  Rd = (insn >> 12) & 15;
                    if (Rd == 15)
                        result = 5;
                    else
                        _interlock_def(Rd,result+1);
                }
            }
            /* else UNDEFINED */
            break;

        case 4:  /* load/store multiple */
            {
                int       Rn   = (insn >> 16) & 15;
                uint32_t  mask = (insn & 0xffff);
                int       count;

                for (count = 0; mask; count++)
                    mask &= (mask-1);

                result += _interlock_use(Rn);

                if (insn & 0x00100000)  /* LDM */
                {
                    int  nn;

                    if (insn & 0x8000) {  /* loading PC */
                        result = count+4;
                    } else {  /* not loading PC */
                        result = (count < 2) ? 2 : count;
                    }
                    /* create defs, all registers locked until the end of the load */
                    for (nn = 0; nn < 15; nn++)
                        if ((insn & (1U << nn)) != 0)
                            _interlock_def(nn,result);
                }
                else  /* STM */
                    result = (count < 2) ? 2 : count;
            }
            break;

        case 5:  /* branch and branch+link */
            break;

        case 6:  /* coprocessor load/store */
            {
                int  Rn = (insn >> 16) & 15;

                if (insn & 0x00100000)
                    result += _interlock_use(Rn);

                /* XXX: other things to do ? */
            }
            break;

        default: /* i.e. 7 */
            /* XXX: TODO: co-processor related things */
            ;
    }
Exit:
    interlock_base += result;

    return result;

#if 0
    // old code
    if ((insn & 0x0ff0f090) == 0x01600080) {
        return TICKS_SMULxy;
    } else if ((insn & 0x0ff00090) == 0x01200080) {
        return TICKS_SMLAWy;
    } else if ((insn & 0x0ff00090) == 0x01400080) {
        return TICKS_SMLALxy;
    } else if ((insn & 0x0f0000f0) == 0x00000090) {
        // multiply
        uint8_t bit23 = (insn >> 23) & 0x1;
        uint8_t bit22_U = (insn >> 22) & 0x1;
        uint8_t bit21_A = (insn >> 21) & 0x1;
        uint8_t bit20_S = (insn >> 20) & 0x1;

        if (bit23 == 0) {
            // 32-bit multiply
            if (bit22_U != 0) {
                // This is an unexpected bit pattern.
                return TICKS_OTHER;
            }
            if (bit21_A == 0) {
                if (bit20_S)
                    return TICKS_MULS;
                return TICKS_MUL;
            }
            if (bit20_S)
                return TICKS_MLAS;
            return TICKS_MLA;
        }
        // 64-bit multiply
        if (bit22_U == 0) {
            // Unsigned multiply long
            if (bit21_A == 0) {
                if (bit20_S)
                    return TICKS_UMULLS;
                return TICKS_UMULL;
            }
            if (bit20_S)
                return TICKS_UMLALS;
            return TICKS_UMLAL;
        }
        // Signed multiply long
        if (bit21_A == 0) {
            if (bit20_S)
                return TICKS_SMULLS;
            return TICKS_SMULL;
        }
        if (bit20_S)
            return TICKS_SMLALS;
        return TICKS_SMLAL;
    }
    return TICKS_OTHER;
#endif
}

// I don't want to support thumb instructions right now, but here's the code
//  in case we need it later
#if 0
int  get_insn_ticks_thumb(uint32_t  insn)
{
    int  result = 1;

    switch ((insn >> 11) & 31)
    {
        case 0:
        case 1:
        case 2:   /* Shift by immediate */
            {
                int  Rm = (insn >> 3) & 7;
                result += _interlock_use(Rm);
            }
            break;

        case 3:  /* Add/Substract */
            {
                int  Rn = (insn >> 3) & 7;
                result += _interlock_use(Rn);

                if ((insn & 0x0400) == 0) {  /* register value */
                    int  Rm = (insn >> 6) & 7;
                    result += _interlock_use(Rm);
                }
            }
            break;

        case 4:  /* move immediate */
            break;

        case 5:
        case 6:
        case 7:  /* add/substract/compare immediate */
            {
                int  Rd = (insn >> 8) & 7;
                result += _interlock_use(Rd);
            }
            break;

        case 8:
            {
                if ((insn & 0x0400) == 0)  /* data processing register */
                {
                    /* the registers can also be Rs and Rn in some cases */
                    /* but they're always read anyway and located at the */
                    /* same place, so we don't check the opcode          */
                    int  Rm = (insn >> 3) & 7;
                    int  Rd = (insn >> 3) & 7;

                    result += _interlock_use(Rm) + _interlock_use(Rd);
                }
                else switch ((insn >> 8) & 3)
                {
                    case 0:
                    case 1:
                    case 2:  /* special data processing */
                        {
                            int  Rn = (insn & 7) | ((insn >> 4) & 0x8);
                            int  Rm = ((insn >> 3) & 15);

                            result += _interlock_use(Rn) + _interlock_use(Rm);
                        }
                        break;

                    case 3:
                        if ((insn & 0xff07) == 0x4700)  /* branch/exchange */
                        {
                            int  Rm = (insn >> 3) & 15;

                            result = 3 + _interlock_use(Rm);
                        }
                        /* else UNDEFINED */
                        break;
                }
            }
            break;

        case 9:  /* load from literal pool */
            {
                int  Rd = (insn >> 8) & 7;
                _interlock_def(Rd,result+1);
            }
            break;

        case 10:
        case 11:  /* load/store register offset */
            {
                int  Rd = (insn & 7);
                int  Rn = (insn >> 3) & 7;
                int  Rm = (insn >> 6) & 7;

                result += _interlock_use(Rn) + _interlock_use(Rm);

                switch ((insn >> 9) & 7)
                {
                    case 0: /* STR  */
                    case 1: /* STRH */
                    case 2: /* STRB */
                        result += _interlock_use(Rd);
                        break;

                    case 3: /* LDRSB */
                    case 5: /* LDRH */
                    case 6: /* LDRB */
                    case 7: /* LDRSH */
                        _interlock_def(Rd,result+2);
                        break;

                    case 4: /* LDR */
                        _interlock_def(Rd,result+1);
                }
            }
            break;

        case 12:  /* store word immediate offset */
        case 14:  /* store byte immediate offset */
            {
                int  Rd = (insn & 7);
                int  Rn = (insn >> 3) & 7;

                result += _interlock_use(Rd) + _interlock_use(Rn);
            }
            break;

        case 13:  /* load word immediate offset */
            {
                int  Rd = (insn & 7);
                int  Rn = (insn >> 3) & 7;

                result += _interlock_use(Rn);
                _interlock_def(Rd,result+1);
            }
            break;

        case 15:  /* load byte immediate offset */
            {
                int  Rd = (insn & 7);
                int  Rn = (insn >> 3) & 7;

                result += _interlock_use(Rn);
                _interlock_def(Rd,result+2);
            }
            break;

        case 16:  /* store halfword immediate offset */
            {
                int  Rd = (insn & 7);
                int  Rn = (insn >> 3) & 7;

                result += _interlock_use(Rn) + _interlock_use(Rd);
            }
            break;

        case 17:  /* load halfword immediate offset */
            {
                int  Rd = (insn & 7);
                int  Rn = (insn >> 3) & 7;

                result += _interlock_use(Rn);
                _interlock_def(Rd,result+2);
            }
            break;

        case 18:  /* store to stack */
            {
                int  Rd = (insn >> 8) & 3;
                result += _interlock_use(Rd);
            }
            break;

        case 19:  /* load from stack */
            {
                int  Rd = (insn >> 8) & 3;
                _interlock_def(Rd,result+1);
            }
            break;

        case 20:  /* add to PC */
        case 21:  /* add to SP */
            {
                int  Rd = (insn >> 8) & 3;
                result += _interlock_use(Rd);
            }
            break;

        case 22:
        case 23:  /* misc. instructions, table 6-2 */
            {
                if ((insn & 0xff00) == 0xb000)  /* adjust stack pointer */
                {
                    result += _interlock_use(14);
                }
                else if ((insn & 0x0600) == 0x0400)  /* push pop register list */
                {
                    uint32_t  mask = insn & 0x01ff;
                    int       count, nn;

                    for (count = 0; mask; count++)
                        mask &= (mask-1);

                    result = (count < 2) ? 2 : count;

                    if (insn & 0x0800)  /* pop register list */
                    {
                        for (nn = 0; nn < 9; nn++)
                            if (insn & (1 << nn))
                                _interlock_def(nn, result);
                    }
                    else  /* push register list */
                    {
                        for (nn = 0; nn < 9; nn++)
                            if (insn & (1 << nn))
                                result += _interlock_use(nn);
                    }
                }
                /* else  software breakpoint */
            }
            break;

        case 24:  /* store multiple */
            {
                int  Rd = (insn >> 8) & 7;
                uint32_t  mask = insn & 255;
                int       count, nn;

                for (count = 0; mask; count++)
                    mask &= (mask-1);

                result = (count < 2) ? 2 : count;
                result += _interlock_use(Rd);

                for (nn = 0; nn < 8; nn++)
                    if (insn & (1 << nn))
                        result += _interlock_use(nn);
            }
            break;

        case 25:  /* load multiple */
            {
                int  Rd = (insn >> 8) & 7;
                uint32_t  mask = insn & 255;
                int       count, nn;

                for (count = 0; mask; count++)
                    mask &= (mask-1);

                result  = (count < 2) ? 2 : count;
                result += _interlock_use(Rd);

                for (nn = 0; nn < 8; nn++)
                    if (insn & (1 << nn))
                        _interlock_def(nn, result);
            }
            break;

        case 26:
        case 27:  /* conditional branch / undefined / software interrupt */
            switch ((insn >> 8) & 15)
            {
                case 14: /* UNDEFINED */
                case 15: /* SWI */
                    break;

                default:  /* conditional branch */
                    result = 3;
            }
            break;

        case 28:  /* unconditional branch */
            result = 3;
            break;

        case 29:  /* BLX suffix or undefined */
            if ((insn & 1) == 0)
                result = 3;
            break;

        case 30:  /* BLX/BLX prefix */
            break;

        case 31:  /* BL suffix */
            result = 3;
            break;
    }
    interlock_base += result;
    return result;
}
#endif
