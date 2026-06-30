#ifndef TABLE_VSTR_H
#define TABLE_VSTR_H

#include <epicsTypes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Variable-length string codec for STRING columns in the tableRecord.
 *
 * Each 40-byte cell (MAX_STRING_SIZE) in a STRING column buffer is exactly
 * one of three types, discriminated by bytes 31 and 32..39:
 *
 *   Type 1 — inline short (len <= 31, no embedded NUL):
 *     bytes  0..len-1  content
 *     byte   len       NUL terminator
 *     bytes  len+1..39 zero
 *     (byte[31] == 0, pointer bytes 32..39 all zero)
 *
 *   Type 2 — plain long (32 <= len <= 39, no embedded NUL):
 *     bytes  0..len-1  content
 *     byte   len       NUL terminator
 *     bytes  len+1..39 zero
 *     discriminated by byte[31] != 0 (it is a content byte)
 *
 *   Type 3 — overflow (len > 39, or string has embedded NUL):
 *     bytes  0..K-1    UTF-8-clean preview (K <= 31, code-point-boundary cut)
 *     bytes  K..31     zero  (byte[31] is the firewall NUL)
 *     bytes  32..39    non-NULL char* to a tablerecVStr heap block
 *
 * Read discriminator:
 *   byte[31] != 0          -> type 2
 *   else pointer != NULL   -> type 3
 *   else                   -> type 1
 *
 * Writer invariants:
 *   (a) free existing type-3 heap block before overwriting a cell
 *   (b) zero the entire 40-byte cell before writing new content
 *   (c) never store a non-NULL pointer unless byte[31] == 0
 *
 * base's putStringString produces only type-1/2 cells (strncpy + forced [39]=0),
 * so external caput/dbpf writes are always safe.  The special() sanitizer in
 * tableRecord.c frees overflow pointers before an external put can clobber them.
 *
 * DB/CA/PVA/const *link* loads (devTableSoft.cpp) likewise only ever produce
 * type-1/2 cells: they read DBF_STRING, which every link plugin truncates to 39
 * chars, and EPICS has no DBR type for an array of long strings.  Type-3
 * (overflow, >39 chars) is therefore reachable only via the direct
 * tablerec_vstr_write / write_string_column callers below.
 *
 * All access must be under the record lock (dbScanLock / process / dbPutField).
 */

/* Heap block for type-3 overflow cells.
 * Allocated as: malloc(offsetof(tablerecVStr,data) + len + 1)
 * data[len] = '\0' for debugger friendliness; readers must use len. */
typedef struct tablerecVStr {
    epicsUInt32 len;
    char        data[1];
} tablerecVStr;

/* Write bytes[0..len) into row `row` of STRING column buffer `colbuf`.
 * Frees any existing overflow pointer, zeroes the cell, then writes a
 * type-1, type-2, or type-3 cell per the layout invariants above. */
void tablerec_vstr_write(void *colbuf, epicsUInt32 row,
                         const char *bytes, epicsUInt32 len);

/* Return a pointer to the bytes of cell `row` and its length via *plen.
 * For type 1/2 the pointer is into the cell itself; for type 3 it points
 * into the heap block.  Valid until the next write to that cell.
 * Caller must hold the record lock. */
const char *tablerec_vstr_read(const void *colbuf, epicsUInt32 row,
                               epicsUInt32 *plen);

/* Free all type-3 overflow pointers in rows [0, nrows) and zero every cell.
 * Used by tableRecord.c special() sanitizer and staging writers. */
void tablerec_vstr_clear(void *colbuf, epicsUInt32 nrows);

#ifdef __cplusplus
}
#endif

#endif /* TABLE_VSTR_H */
