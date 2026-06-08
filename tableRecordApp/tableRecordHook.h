#ifndef TABLE_RECORD_HOOK_H
#define TABLE_RECORD_HOOK_H

/*
 * Per-record processing hook.
 *
 * A publisher (e.g. the pvxs TableSource) installs a pointer to one of these in
 * the record's RPVT field. tableRecord's process() invokes notify() at the end
 * of processing, while the record lock is held and the per-column CHGD flags are
 * still valid for the current cycle. notify() recovers its full context from
 * prec->rpvt (this struct is the first member of the publisher's context), so no
 * separate user-data argument is needed. The record itself stays agnostic of the
 * publisher: if RPVT is NULL the hook is simply a no-op.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct tableRecord;

typedef struct tableRecordPvt {
    void (*notify)(struct tableRecord *prec);
    void  *self;   /* opaque publisher context, recovered inside notify() */
} tableRecordPvt;

#ifdef __cplusplus
}
#endif

#endif /* TABLE_RECORD_HOOK_H */
