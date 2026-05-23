# tableRecord — Background Research

Notes gathered before designing the two POC records. Every claim below has a
file-path citation into either `epics-base` or `pvxs` so the design decisions
are verifiable. Local copies used:

- `/home/bmartins/osprey/epics/epics-base`
- `/home/bmartins/osprey/epics/pvxs`

---

## 1. Anatomy of an EPICS record

### 1.1 Record support entry table (RSET)

Every record type provides an RSET with up to 17 function pointers. Definition
at `epics-base/modules/database/src/ioc/dbStatic/recSup.h`:

```c
struct typed_rset {
    long number;
    long (*report)(void *precord);
    long (*init)();
    long (*init_record)(struct dbCommon *precord, int pass);
    long (*process)(struct dbCommon *precord);
    long (*special)(struct dbAddr *paddr, int after);
    long (*get_value)();                /* deprecated */
    long (*cvt_dbaddr)(struct dbAddr *paddr);
    long (*get_array_info)(struct dbAddr *paddr, long *no_elements, long *offset);
    long (*put_array_info)(struct dbAddr *paddr, long nNew);
    long (*get_units)(struct dbAddr *paddr, char *units);
    long (*get_precision)(const struct dbAddr *paddr, long *precision);
    long (*get_enum_str)(...);
    long (*get_enum_strs)(...);
    long (*put_enum_str)(...);
    long (*get_graphic_double)(...);
    long (*get_control_double)(...);
    long (*get_alarm_double)(...);
};
```

Registered globally via `epicsExportAddress(rset, <recordTypeName>RSET);`.

Key routines for array-bearing records (the family `tableRecord` belongs to):

| Routine          | Purpose                                                                     |
| ---------------- | --------------------------------------------------------------------------- |
| `init_record`    | Two-pass init. Pass 0 allocates record-wide state; pass 1 walks links/DSET. |
| `process`        | Called by scan tasks / forward links. Drives the device support read.       |
| `cvt_dbaddr`     | Maps a field-address lookup to the actual array buffer (`paddr->pfield`).   |
| `get_array_info` | Reports current element count + offset to CA/PVA when an array is read.    |
| `put_array_info` | Updates the element count when an array is written to.                      |
| `special`        | Hook for `SPC_xxx` field-write side effects.                                |

### 1.2 Device support entry table (DSET)

A DSET is the hardware (or "soft") plug-in. Layout (waveform example,
`epics-base/modules/database/src/std/dev/devWfSoft.c`):

```c
typedef struct wfdset {
    dset common;                                /* generic header */
    long (*read_wf)(struct waveformRecord *prec);
} wfdset;

wfdset devWfSoft = {
    {5, NULL, NULL, init_record, NULL},
    read_wf
};
epicsExportAddress(dset, devWfSoft);
```

`dset common`:
- `long number` (≥ 5 for valid device support).
- `long (*report)(int)`, `long (*init)(int)`, `long (*init_record)(dbCommon*)`,
  `long (*get_ioint_info)(int, dbCommon*, IOSCANPVT*)`.

A record's `.dbd` file links a DTYP string to a DSET via the
`device(<recordType>, <linkType>, <dsetSymbol>, "<DTYP string>")` directive.

### 1.3 The `dbCommon` base fields

Every record gets the fields declared in
`epics-base/modules/database/src/ioc/db/dbCommon.dbd` (NAME, DESC, SCAN, PACT,
TPRO, SEVR, STAT, TIME, …). Inherit them in a custom record by including
`dbCommon.dbd` at the top of the recordtype block.

### 1.4 The `menuFtype` enum

`epics-base/modules/database/src/ioc/dbStatic/menuFtype.dbd` declares 12
values, in order: `STRING, CHAR, UCHAR, SHORT, USHORT, LONG, ULONG, INT64,
UINT64, FLOAT, DOUBLE, ENUM`. `STRING` is a fixed 40-byte EPICS string
(`MAX_STRING_SIZE`), not variable-length. `dbValueSize(ftvl)` returns the size
of one element for any of these.

### 1.5 DBD-POD format

Modern record sources combine the `.dbd` field declarations with POD docs in a
single `.dbd.pod` file. Templates:

- `epics-base/modules/database/src/std/rec/waveformRecord.dbd.pod`
- `epics-base/modules/database/src/std/rec/aSubRecord.dbd.pod`

Notable directives observed:

- `field(NAME, DBF_TYPE)` block with `prompt`, `promptgroup`, `special`,
  `interest`, `initial`, `menu`, `pp`, `extra` sub-directives.
- `DBF_NOACCESS` + `extra("void *bptr")` for opaque array fields backed by an
  internal pointer; `special(SPC_DBADDR)` flags that a `cvt_dbaddr` lookup is
  needed.
- `pp(TRUE)` triggers forward-link processing when the field is written from
  channel access.
- `=head`, `=fields`, `=type`, `=read`, `=write` POD directives produce the
  documentation pages.

### 1.6 Out-of-tree build pattern

`epics-base/templates/makeBaseApp/top/exampleApp/src` shows the minimum
makefile pattern for a custom record outside `epics-base`:

```makefile
DBDINC += xxxRecord                       # generates xxxRecord.h
DBD    += xxxSupport.dbd                  # aggregator .dbd

LIBRARY_IOC += xxxSupport
xxxSupport_SRCS += xxxRecord.c
xxxSupport_SRCS += devXxxSoft.c
xxxSupport_LIBS += $(EPICS_BASE_IOC_LIBS)

# In the IOC application:
xxx_DBD  += xxxSupport.dbd
xxx_LIBS += xxxSupport
```

The aggregator `.dbd` includes the record's `.dbd` plus `device(...)` and
`registrar(...)` lines.

### 1.7 The `waveformRecord` reference

`waveformRecord.c` + `waveformRecord.dbd.pod`:

- Pass-0 `init_record` allocates `prec->bptr = callocMustSucceed(prec->nelm,
  dbValueSize(prec->ftvl), ...)`.
- `process` calls `pdset->read_wf(prec)`, then `recGblGetTimeStampSimm`,
  `recGblResetAlarms`, `recGblFwdLink`.
- `cvt_dbaddr` swings `paddr->pfield = prec->bptr`,
  `paddr->no_elements = prec->nelm`, `paddr->field_type = prec->ftvl`.
- `get_array_info` returns `*no_elements = prec->nord`.

Soft-channel device support (`devWfSoft.c`) just does `dbGetLink(&prec->inp,
prec->ftvl, prec->bptr, 0, &nReq)` and updates `prec->nord`.

### 1.8 The `aSubRecord` reference

`aSubRecord.c` + `aSubRecord.dbd.pod`:

- 21 parallel input fields `A..U`, each with a parallel `FTA..FTU` (FTVL),
  `NOA..NOU` (capacity), `NEA..NEU` (current count), and matching `VALA..VALU`
  outputs. Same pattern repeated for `OUTA..OUTU`.
- `cvt_dbaddr` indexes adjacent struct fields with pointer arithmetic, e.g.
  `(&prec->a)[offset]`, `(&prec->noa)[offset]`, `(&prec->fta)[offset]`. This is
  the canonical idiom for "many-parallel-typed-arrays" records and is exactly
  what design B will use for COL00..COL07.
- `process` calls a user-supplied subroutine looked up by name.

---

## 2. Normative Types and `NTTable` in pvxs

### 2.1 `pvxs::nt::NTTable`

Declared at `pvxs/src/pvxs/nt.h:121-146`; implemented at
`pvxs/src/nt.cpp:130-185`. Usage:

```cpp
auto def = pvxs::nt::NTTable{}
              .add_column(TypeCode::Float64, "x", "X axis")
              .add_column(TypeCode::String,  "name", "Name")
              .build();
Value v = def.create();   // labels pre-populated
```

The resulting type id is `epics:nt/NTTable:1.0`. Layout
(`pvxs/src/nt.cpp:170-176`):

```cpp
TypeDef def(TypeCode::Struct, "epics:nt/NTTable:1.0", {
    members::StringA("labels"),
    members::Struct("value", columns),    /* one array per column */
    members::String("descriptor"),
    Alarm{}.build().as("alarm"),
    TimeStamp{}.build().as("timeStamp"),
});
```

`add_column` rejects non-scalar types (`nt.cpp:155`) and internally converts
the scalar `TypeCode` to its array form (`code.arrayOf()`, `nt.cpp:156`).
Variable-length string arrays are valid columns.

The header's doc warns
(`nt.h:115-117`): "repeated `create()` could result in re-sending the same
labels array with every update. Users should `create()` once, and then
`Value::cloneEmpty()` or `Value::unmark()` for subsequent updates."

### 2.2 Existing NTTable example: `Q:group`

`pvxs/documentation/qgroup.rst:160-250` walks through composing an NTTable
from multiple `aao` records using `info(Q:group, {...})` JSON tags. This
already exists, but produces NTTable from *multiple* records — it doesn't help
us when the whole table lives in one record.

---

## 3. qsrv internals (`pvxs/ioc`)

### 3.1 The qsrv `Server`

Accessed via `pvxs::ioc::server()` declared in `pvxs/ioc/pvxs/iochooks.h:57`,
implemented in `pvxs/ioc/iochooks.cpp`. Returns a singleton
`pvxs::server::Server`. Sources are added with the standard
`Server::addSource(name, source, priority)` API (`pvxs/src/pvxs/server.h:116`).

### 3.2 `SingleSource` (one record → one PV)

`pvxs/ioc/singlesource.cpp` is the source that publishes individual records.
The hook that picks an NT type for a given record is `getValuePrototype`
(`pvxs/ioc/singlesource.cpp:189-206`):

```cpp
Value getValuePrototype(const std::shared_ptr<SingleInfo>& sinfo) {
    auto& chan(sinfo->chan);
    short dbrType(dbChannelFinalFieldType(chan));
    auto valueType(IOCSource::getChannelValueType(chan));

    if (dbrType == DBR_ENUM) {
        return nt::NTEnum{}.create();
    } else {
        return nt::NTScalar{ valueType, true, true, true, true }.create();
    }
}
```

This is where an upstream patch would branch on the record type name to
produce an `NTTable` instead of `NTScalar`.

`SingleSource` is registered in
`pvxs/ioc/singlesourcehooks.cpp:158-159`:

```cpp
pvxs::ioc::server()
    .addSource("qsrvSingle",
               std::make_shared<pvxs::ioc::SingleSource>(),
               0);
```

### 3.3 `GroupSource` (multiple records → one PV)

`pvxs/ioc/groupsource.cpp` and `groupsourcehooks.cpp`. Driven by JSON in
`info(Q:group, …)`. Registered at priority 1 in
`groupsourcehooks.cpp:218-219`. Sources are tried in priority order, so any
custom source at priority ≥ 1 will see channels before `qsrvSingle` does.

### 3.4 The `Source` interface

`pvxs/src/pvxs/source.h:206-295`. Two virtuals to implement:

- `onSearch(Search&)` — called per name; call `op.claim()` if we own it.
- `onCreate(unique_ptr<ChannelControl>&&)` — install a handler that does GET,
  PUT, MONITOR (`SubscribeOp`).

Optional `onList()` returns the list of channels for `pvlist`.

### 3.5 Extension model

A downstream library can therefore implement a custom `Source`, register it
with `pvxs::ioc::server().addSource(name, source, priority)` after `iocInit`,
and natively publish records under arbitrary NT types — *without* changing
pvxs.

---

## 4. Strings, variable-length columns, atomicity

- EPICS-side: `FTVL=STRING` is always 40 bytes. There is no built-in
  variable-length string array in stock records; supporting one would require
  a custom field type + custom serializer.
- PVAccess-side: NTTable columns may be variable-length string arrays
  natively. The translation between fixed-40 EPICS strings and PVA's
  variable-length strings happens entirely in the publishing Source.
- Atomicity for monitor updates: the publishing Source must take
  `dbScanLock(precord)` while it snapshots all columns, then build the Value
  outside the lock and post.

---

## 5. Implications for the `tableRecord` designs

1. Both designs are buildable as standard custom records — there's nothing
   exotic in the requirement, just careful use of `cvt_dbaddr` /
   `get_array_info`.
2. The "monolithic byte array" idea in Design A (`COLDATA` holding mixed
   types) is awkward and provides no real benefit on the EPICS side because
   `cvt_dbaddr` can already return distinct per-column pointers with different
   `field_type`s. Recommendation: keep one buffer per column internally even
   in Design A; expose per-column slices via cvt_dbaddr on COL00..COLnn
   accessor fields. Drop COLDATA-as-bytes from the field set.
3. Design B's per-column field bundle (COL00NAME, COL00FTVL, …) is the aSub
   pattern verbatim and is well-trodden territory.
4. qsrv publishing as NTTable is best done as a custom `Source` library
   shipped with this repo (no upstream pvxs change). The hook is well-defined:
   `pvxs::ioc::server().addSource()`. An optional upstream patch to
   `singlesource.cpp::getValuePrototype` is a clean follow-up but not required
   for the POC.
5. Fixed 40-char strings (FTVL=STRING) are the right choice for string
   columns — matches all other EPICS records and keeps the Source code
   simple.

These conclusions are the basis of `PLAN.md`.
