#include "tableVStr.h"

#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include <epicsAssert.h>
#include <cantProceed.h>
#include <epicsString.h>

/* Compile-time layout sanity checks */
STATIC_ASSERT(MAX_STRING_SIZE % 8 == 0);
STATIC_ASSERT(MAX_STRING_SIZE >= 32 + sizeof(char *));

#define VSTR_FIREWALL  31          /* index of the firewall NUL / max inline len */
#define VSTR_PTR_OFF   32          /* byte offset of the pointer in an overflow cell */
#define VSTR_PLAIN_MAX (MAX_STRING_SIZE - 1)  /* 39: max len for a type-2 cell */

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static char *cellAt(void *colbuf, epicsUInt32 row)
{
    return (char *)colbuf + (size_t)row * MAX_STRING_SIZE;
}

static const char *cellAtConst(const void *colbuf, epicsUInt32 row)
{
    return (const char *)colbuf + (size_t)row * MAX_STRING_SIZE;
}

/* Return the overflow pointer from a cell, or NULL if the cell is type 1/2.
 * Uses memcpy to avoid strict-aliasing UB; alignment is guaranteed because
 * VSTR_PTR_OFF=32 is a multiple of 8. */
static tablerecVStr *cellOverflow(const char *cell)
{
    char *p;
    if (cell[VSTR_FIREWALL] != '\0')
        return NULL; /* type 2: byte[31] is a content byte, not a firewall */
    memcpy(&p, cell + VSTR_PTR_OFF, sizeof(char *));
    return (tablerecVStr *)p;
}

/* Find the largest UTF-8 code-point boundary K <= VSTR_FIREWALL (31).
 * If len fits inline (len <= VSTR_FIREWALL) the cut is at len itself.
 * Otherwise we back off from byte VSTR_FIREWALL past any continuation bytes
 * (0x80..0xBF), up to 3 steps.  If we still land on a continuation byte
 * (malformed input), fall back to a plain byte cut at VSTR_FIREWALL. */
static epicsUInt32 utf8PreviewCut(const char *bytes, epicsUInt32 len)
{
    epicsUInt32 k = (len < (epicsUInt32)VSTR_FIREWALL)
                    ? len : (epicsUInt32)VSTR_FIREWALL;

    if (k == len)
        return k; /* string fits in inline region, no cut needed */

    /* k == VSTR_FIREWALL and len > VSTR_FIREWALL.
     * bytes[k] is the first byte that would NOT fit; walk back past any
     * continuation bytes of a multi-byte sequence that straddles the cut. */
    {
        epicsUInt32 k0 = k;
        int steps = 0;
        while (k > 0 && steps < 3 && ((unsigned char)bytes[k] & 0xC0u) == 0x80u) {
            --k;
            ++steps;
        }
        if (((unsigned char)bytes[k] & 0xC0u) == 0x80u) {
            /* Still a continuation byte after 3 steps — malformed input.
             * Fall back to the plain byte cut. */
            k = k0;
        }
        /* bytes[k] is now either a lead byte, an ASCII byte, or the fallback
         * boundary — the preview is bytes[0..k), and k <= VSTR_FIREWALL. */
    }
    return k;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void tablerec_vstr_write(void *colbuf, epicsUInt32 row,
                         const char *bytes, epicsUInt32 len)
{
    char *cell = cellAt(colbuf, row);

    /* (a) free any existing overflow heap block */
    tablerecVStr *old = cellOverflow(cell);
    if (old)
        free(old);

    /* (b) zero the entire cell */
    memset(cell, 0, MAX_STRING_SIZE);

    /* Determine whether the string has an embedded NUL */
    int hasNul = (bytes && len > 0 && memchr(bytes, '\0', len) != NULL);

    if (!bytes || len == 0) {
        /* empty string: type 1, already zeroed */
        return;
    }

    if (len <= (epicsUInt32)VSTR_FIREWALL && !hasNul) {
        /* Type 1 — inline short */
        memcpy(cell, bytes, len);
        /* bytes [len..39] remain zero; no NUL needed explicitly (zero-fill) */
        return;
    }

    if (len <= (epicsUInt32)VSTR_PLAIN_MAX && !hasNul) {
        /* Type 2 — plain long (32..39 bytes, byte[31] will be nonzero) */
        memcpy(cell, bytes, len);
        /* cell[len] == 0 (already), bytes after NUL are zero (memset above) */
        return;
    }

    /* Type 3 — overflow (len > 39, or embedded NUL) */
    {
        tablerecVStr *p = (tablerecVStr *)mallocMustSucceed(
            offsetof(tablerecVStr, data) + (size_t)len + 1,
            "tablerec_vstr_write");
        p->len = len;
        memcpy(p->data, bytes, len);
        p->data[len] = '\0'; /* for debugger friendliness; readers use p->len */

        /* Write UTF-8-clean preview into the inline region */
        epicsUInt32 k = utf8PreviewCut(bytes, len);
        if (k > 0)
            memcpy(cell, bytes, k);
        /* bytes k..31 already zero from memset; byte[31] is the firewall NUL */

        /* Store the pointer at bytes 32..39 using memcpy to avoid aliasing UB */
        {
            char *pval = (char *)p;
            memcpy(cell + VSTR_PTR_OFF, &pval, sizeof(char *));
        }
    }
}

const char *tablerec_vstr_read(const void *colbuf, epicsUInt32 row,
                               epicsUInt32 *plen)
{
    const char *cell = cellAtConst(colbuf, row);
    tablerecVStr *p  = cellOverflow(cell);

    if (cell[VSTR_FIREWALL] != '\0') {
        /* Type 2 — plain long: NUL-terminated C string; byte[39] is always NUL */
        epicsUInt32 n = (epicsUInt32)epicsStrnLen(cell, MAX_STRING_SIZE);
        *plen = n;
        return cell;
    }

    if (p) {
        /* Type 3 — overflow: read from the heap block */
        *plen = p->len;
        return p->data;
    }

    /* Type 1 — inline short: NUL-terminated within bytes 0..31 */
    *plen = (epicsUInt32)epicsStrnLen(cell, VSTR_FIREWALL + 1);
    return cell;
}

void tablerec_vstr_clear(void *colbuf, epicsUInt32 nrows)
{
    epicsUInt32 r;
    for (r = 0; r < nrows; r++) {
        char *cell = cellAt(colbuf, r);
        tablerecVStr *p = cellOverflow(cell);
        if (p)
            free(p);
        memset(cell, 0, MAX_STRING_SIZE);
    }
}
