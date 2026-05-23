# tableRecord — Implementation Plan

This is the execution plan for the two POC records (`tableA`, `tableB`) and
their qsrv NTTable publishing layer. Tasks are deliberately small and stateful
so that several can run in parallel. Each task lists its inputs, outputs, and
dependencies. The background reasoning that justifies these choices is in
[`RESEARCH.md`](./RESEARCH.md); read that first if anything below seems
arbitrary.

## Design decisions locked in

- **String columns:** fixed 40-char EPICS strings (`FTVL=STRING`).
- **Max columns for Design B:** 8 (`COL00..COL07`).
- **qsrv integration:** custom `Source` library in this repo first; upstream
  pvxs patch documented as a follow-up phase, not implemented in the POC.
- **Device support per design:** a Soft Channel DSET **and** a stub
  "Random Sim" hardware-style DSET, so the hardware DSET interface is exercised.
- **Record-type internal layout (both designs):** one buffer per column,
  allocated at `init_record` from `MAXROWS * dbValueSize(FTVL)`. The "monolithic
  byte array" originally sketched in Design A is replaced by per-column
  accessor fields exposed via `cvt_dbaddr`.

## Repository layout (target)

```
tableRecord/
  configure/                          # exists
  tableRecordSup/                     # NEW: support library
    Makefile
    src/
      Makefile
      tableARecord.dbd.pod
      tableARecord.c
      devTableASoft.c
      devTableASim.c
      tableBRecord.dbd.pod
      tableBRecord.c
      devTableBSoft.c
      devTableBSim.c
      tableSource.h
      tableSource.cpp
      tableSourceRegistrar.cpp
      tableRecordSup.dbd
  tableRecordApp/
    src/Makefile                      # patched
    Db/
      Makefile
      tableA_demo.db                  # NEW
      tableB_demo.db                  # NEW
  iocBoot/                            # NEW
    Makefile
    iocTable/
      Makefile
      st.cmd
  RESEARCH.md
  PLAN.md
```

The top-level `Makefile` already wildcards `*Sup` and `iocBoot`, so creating
those directories wires them into the build automatically.

---

## Phase 0 — Skeleton & build wiring

These are the prerequisite tasks. Everything in Phases 1–4 depends on them
landing first. They are tiny.

**T0.1 — Create `tableRecordSup/Makefile`**
Single line: `TOP=..` then `include $(TOP)/configure/CONFIG`, then
`DIRS += src` and `include $(TOP)/configure/RULES_DIRS`. Mirror
`tableRecordApp/Makefile`.
*Inputs:* none. *Outputs:* `tableRecordSup/Makefile`. *Deps:* none.

**T0.2 — Create `tableRecordSup/src/Makefile`**
Declares the support library. Skeleton:
```makefile
TOP=../..
include $(TOP)/configure/CONFIG

DBDINC += tableARecord
DBDINC += tableBRecord

DBD += tableRecordSup.dbd

LIBRARY_IOC += tableRecordSup
tableRecordSup_SRCS += tableARecord.c
tableRecordSup_SRCS += devTableASoft.c
tableRecordSup_SRCS += devTableASim.c
tableRecordSup_SRCS += tableBRecord.c
tableRecordSup_SRCS += devTableBSoft.c
tableRecordSup_SRCS += devTableBSim.c
tableRecordSup_SRCS += tableSource.cpp
tableRecordSup_SRCS += tableSourceRegistrar.cpp
tableRecordSup_LIBS  += pvxsIoc pvxs $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
```
*Inputs:* none. *Outputs:* `tableRecordSup/src/Makefile`. *Deps:* none.

**T0.3 — Add PVXS to `configure/RELEASE`**
Insert `PVXS = /home/bmartins/osprey/epics/pvxs` above the existing
`EPICS_BASE = …` line.
*Inputs:* none. *Outputs:* edited `configure/RELEASE`. *Deps:* none.

**T0.4 — Patch `tableRecordApp/src/Makefile`**
Add `tableRecord_DBD += tableRecordSup.dbd` and prepend
`tableRecord_LIBS += tableRecordSup pvxsIoc pvxs` before the existing
`$(EPICS_BASE_IOC_LIBS)` line.
*Inputs:* none. *Outputs:* edited `tableRecordApp/src/Makefile`.
*Deps:* T0.2 (so the library target exists).

**T0.5 — Create `iocBoot/Makefile` and `iocBoot/iocTable/Makefile`**
Stock IOC boot makefile from `epics-base/templates/makeBaseApp`. The inner
`iocTable/Makefile` declares `ARCH = linux-x86_64` (or equivalent) and
`include $(TOP)/configure/RULES.ioc`.
*Inputs:* none. *Outputs:* the two Makefiles. *Deps:* none.

**Phase 0 parallelism:** T0.1, T0.2, T0.3, T0.5 in parallel; T0.4 after T0.2.

---

## Phase 1 — `tableARecord`

Design A: table-wide metadata at the top level, internal storage per-column,
accessor pseudo-fields `COL00..COL07` for DB-side per-column access.

**T1.1 — Write `tableARecord.dbd.pod`**
Fields:
- Inherit `dbCommon.dbd` (first line of the recordtype block).
- `MAXCOLS` (DBF_ULONG, SPC_NOMOD, initial("8")).
- `MAXROWS` (DBF_ULONG, SPC_NOMOD, initial("16")).
- `NCOL`    (DBF_ULONG, current column count, ≤ MAXCOLS).
- `NROW`    (DBF_ULONG, current row count, ≤ MAXROWS).
- `COLNAMES` (DBF_NOACCESS, `extra("char *colnames")`, `special(SPC_DBADDR)`;
   `cvt_dbaddr` exposes as STRING[MAXCOLS]).
- `COLTYPES` (DBF_NOACCESS, `extra("epicsUInt16 *coltypes")`,
   `special(SPC_DBADDR)`; exposed as menuFtype[MAXCOLS]).
- `CHGD`     (DBF_NOACCESS, `extra("epicsUInt8 *chgd")`,
   `special(SPC_DBADDR)`; exposed as UCHAR[MAXCOLS]).
- `VAL`      (DBF_NOACCESS, `pp(TRUE)`, `extra("void *val")`,
   `special(SPC_DBADDR)`; cvt_dbaddr points at column 0 as a default).
- `COL00 .. COL07` (DBF_NOACCESS each, `extra("void *col00")` …
   `extra("void *col07")`, `special(SPC_DBADDR)`; per-column array view).
- `INP`      (DBF_INLINK).
- `DTYP`     (standard menu).

Use `epics-base/modules/database/src/std/rec/waveformRecord.dbd.pod` as the
syntactic template.

*Inputs:* none. *Outputs:* `tableRecordSup/src/tableARecord.dbd.pod`.
*Deps:* none (can run in parallel with T2.1).

**T1.2 — Write `tableARecord.c`**
Implement the RSET:
- `init_record(prec, pass)`:
  - Pass 0: allocate `colnames`, `coltypes`, `chgd` arrays of size `MAXCOLS`.
  - Pass 1: for `i` in `[0, NCOL)`, allocate
    `prec->col0i = callocMustSucceed(MAXROWS, dbValueSize(coltypes[i]), …)`.
    Then call the DSET's `init_record` if non-null.
- `process(prec)`:
  - `pdset->read_table(prec)`; on success, set `udf=0`,
    `recGblGetTimeStampSimm`, `recGblResetAlarms`, `recGblFwdLink`. Return 0.
- `cvt_dbaddr(paddr)`:
  - Dispatch on `paddr->pfldDes->indvalFlag`:
    - COLNAMES → `pfield=colnames`, `field_type=DBF_STRING`,
      `field_size=MAX_STRING_SIZE`, `no_elements=MAXCOLS`.
    - COLTYPES → menuFtype[MAXCOLS].
    - CHGD → UCHAR[MAXCOLS].
    - VAL → fall through to COL00 (for default reads).
    - COL00..COL07 → compute `i = idx - COL00`, set `pfield = prec->colXX`,
      `field_type = coltypes[i]`, `field_size = dbValueSize(coltypes[i])`,
      `no_elements = MAXROWS`.
- `get_array_info` returns `NROW` for COLxx and `NCOL` for COLNAMES /
  COLTYPES / CHGD.
- `put_array_info` updates `NROW`.
- `get_units`, `get_precision`, `get_alarm_double`: minimal stubs.
- `epicsExportAddress(rset, tableARSET);`

*Inputs:* `tableARecord.h` (auto-generated from T1.1's `.dbd.pod`).
*Outputs:* `tableRecordSup/src/tableARecord.c`. *Deps:* T1.1.

**T1.3 — Write `devTableASoft.c`**
DSET `devTableASoft` with `read_table(prec)` that calls
`dbGetLink(&prec->inp, DBR_DOUBLE, scratch, 0, &n)` only as a marker — for the
POC the soft path is a no-op and clients write columns directly via CA/PVA.
This documents the interface and proves DSET wiring; per-column input links
are Design B's territory.
*Inputs:* `tableARecord.h`. *Outputs:* `tableRecordSup/src/devTableASoft.c`.
*Deps:* T1.1.

**T1.4 — Write `devTableASim.c`**
DSET `devTableASim` with `read_table(prec)` that, on each call, sets
`NROW = MAXROWS` and fills each active column buffer with random data sized
to its `dbValueSize(coltypes[i])`. Use `random()` for numeric columns; for
STRING columns fill with `"row_%d"` formatted strings.
*Inputs:* `tableARecord.h`. *Outputs:* `tableRecordSup/src/devTableASim.c`.
*Deps:* T1.1.

**T1.5 — Add `recordtype(tableA)` to `tableRecordSup.dbd`**
Lines:
```
include "tableARecord.dbd"
device(tableA, CONSTANT, devTableASoft, "Soft Channel")
device(tableA, CONSTANT, devTableASim, "Random Sim")
```
*Inputs:* T1.2, T1.3, T1.4. *Outputs:* portion of `tableRecordSup.dbd`.
*Deps:* T1.1.

**T1.6 — Write `db/tableA_demo.db`**
Two records:
```
record(tableA, "TST:A:Soft") {
  field(DTYP, "Soft Channel")
  field(MAXCOLS, "4")
  field(MAXROWS, "16")
  field(NCOL,    "4")
  field(NROW,    "0")
  # initialize COLNAMES / COLTYPES via dbpf at startup or info() tag
}

record(tableA, "TST:A:Sim") {
  field(DTYP, "Random Sim")
  field(SCAN, "1 second")
  field(MAXCOLS, "4")
  field(MAXROWS, "16")
  field(NCOL,    "4")
}
```
*Inputs:* T1.5. *Outputs:* `tableRecordApp/Db/tableA_demo.db`.
*Deps:* T1.5.

**Phase 1 parallelism:** T1.1 first; then T1.2, T1.3, T1.4 in parallel; then
T1.5; then T1.6.

---

## Phase 2 — `tableBRecord`

Design B: one field bundle per column, aSub-style.

**T2.1 — Write `tableBRecord.dbd.pod`**
Field set:
- Inherit `dbCommon.dbd`.
- `NCOL` (DBF_ULONG, ≤ 8).
- `MAXROWS` (DBF_ULONG, SPC_NOMOD, initial("16")).
- For each `i` in `00..07`, the block:
  - `COLiNAME` (DBF_STRING).
  - `COLiFTVL` (DBF_MENU menuFtype, SPC_NOMOD, initial("DOUBLE")).
  - `COLiINP`  (DBF_INLINK).
  - `COLiNELM` (DBF_ULONG, SPC_NOMOD; 0 means "use MAXROWS").
  - `COLiNORD` (DBF_ULONG).
  - `COLiVAL`  (DBF_NOACCESS, `extra("void *col"##i##"val")`,
     `special(SPC_DBADDR)`, `pp(TRUE)`).
  - `COLiCHGD` (DBF_UCHAR).
- `DTYP` (standard).

Generate the 8 blocks by hand (8 copies is tolerable; aSub itself has ~21).
Use `epics-base/modules/database/src/std/rec/aSubRecord.dbd.pod` as the
template.

*Inputs:* none. *Outputs:* `tableRecordSup/src/tableBRecord.dbd.pod`.
*Deps:* none.

**T2.2 — Write `tableBRecord.c`**
RSET implementation mirroring aSubRecord's parallel-fields handling:
- `init_record` pass 0: allocate each active column buffer
  (`COLiNELM ? COLiNELM : MAXROWS` × `dbValueSize(coliftvl)`).
- `init_record` pass 1: walk each `COLiINP`, calling
  `dbLoadLinkArray` / `dbLoadLink`.
- `process`: `pdset->read_table(prec)`; standard housekeeping.
- `cvt_dbaddr`: compute the column index from
  `idx = paddr->pfldDes->indvalFlag - tableBRecordCOL00VAL`, then `i = idx / 7`
  if all 7 fields per column have consecutive indvalFlags (verify by reading
  the generated `tableBRecord.h`); fall back to a switch on idx if the
  generated indices aren't contiguous. Use pointer arithmetic:
  `pfield = (&prec->col00val)[i]`, `field_type = (&prec->col00ftvl)[i]`,
  `no_elements = (&prec->col00nelm)[i] ?: prec->maxrows`.
- `get_array_info` returns `(&prec->col00nord)[i]`.
- `put_array_info` updates `(&prec->col00nord)[i]` and sets `CHGD[i] = 1`.

*Inputs:* `tableBRecord.h`. *Outputs:* `tableRecordSup/src/tableBRecord.c`.
*Deps:* T2.1.

**T2.3 — Write `devTableBSoft.c`**
DSET `devTableBSoft.read_table`: for each `i` in `[0, NCOL)`, call
`dbGetLink(&prec->col00inp + i, (&prec->col00ftvl)[i], (&prec->col00val)[i],
0, &nReq)`; set `(&prec->col00nord)[i] = nReq`, `(&prec->col00chgd)[i] = 1`.
*Inputs:* `tableBRecord.h`. *Outputs:* `tableRecordSup/src/devTableBSoft.c`.
*Deps:* T2.1.

**T2.4 — Write `devTableBSim.c`**
DSET `devTableBSim.read_table`: for each active column, fill the buffer with
`MAXROWS` random elements typed by `COLiFTVL`; STRING uses `"row_%d"`.
*Inputs:* `tableBRecord.h`. *Outputs:* `tableRecordSup/src/devTableBSim.c`.
*Deps:* T2.1.

**T2.5 — Add `recordtype(tableB)` to `tableRecordSup.dbd`**
```
include "tableBRecord.dbd"
device(tableB, CONSTANT, devTableBSoft, "Soft Channel")
device(tableB, CONSTANT, devTableBSim, "Random Sim")
```
*Inputs:* T2.2, T2.3, T2.4. *Outputs:* portion of `tableRecordSup.dbd`.
*Deps:* T2.1.

**T2.6 — Write `db/tableB_demo.db`**
Two records analogous to T1.6, with per-column NAME/FTVL/INP set up front:
```
record(tableB, "TST:B:Sim") {
  field(DTYP, "Random Sim")
  field(SCAN, "1 second")
  field(MAXROWS, "16")
  field(NCOL,    "4")
  field(COL00NAME, "x")    field(COL00FTVL, "DOUBLE")
  field(COL01NAME, "y")    field(COL01FTVL, "DOUBLE")
  field(COL02NAME, "label")field(COL02FTVL, "STRING")
  field(COL03NAME, "flag") field(COL03FTVL, "UCHAR")
}

record(tableB, "TST:B:Soft") {
  field(DTYP, "Soft Channel")
  field(NCOL, "2")
  field(COL00NAME, "x") field(COL00FTVL, "DOUBLE")
    field(COL00INP, "SRC:X CP")    # subscription to an existing waveform
  field(COL01NAME, "y") field(COL01FTVL, "DOUBLE")
    field(COL01INP, "SRC:Y CP")
}
```
*Inputs:* T2.5. *Outputs:* `tableRecordApp/Db/tableB_demo.db`.
*Deps:* T2.5.

**Phase 2 parallelism:** T2.1 first; then T2.2, T2.3, T2.4 in parallel; then
T2.5; then T2.6. Phase 2 is fully independent of Phase 1 — both can be worked
in parallel by separate implementers.

---

## Phase 3 — IOC test harness

**T3.1 — Update `tableRecordApp/Db/Makefile`**
Add `DB += tableA_demo.db` and `DB += tableB_demo.db`.
*Deps:* T1.6, T2.6.

**T3.2 — Write `iocBoot/iocTable/st.cmd`**
```
#!../../bin/linux-x86_64/tableRecord
dbLoadDatabase("../../dbd/tableRecord.dbd",0,0)
tableRecord_registerRecordDeviceDriver(pdbbase)
dbLoadRecords("../../db/tableA_demo.db")
dbLoadRecords("../../db/tableB_demo.db")
iocInit()
addTableSource()
```
*Deps:* T0.5, T4.3 (for `addTableSource`).

**T3.3 — Manual smoke test (no NTTable yet)**
Build, start the IOC, run:
- `dbpr TST:A:Sim 2` — expect populated fields after ≥1 SCAN tick.
- `dbpr TST:B:Sim 2` — same.
- `dbgf TST:B:Sim.COL00VAL` — expect 16 doubles.
This is verification, not a coding task; document the expected output in
`iocBoot/iocTable/SMOKE.md` if it helps reproducibility.
*Deps:* T3.2.

---

## Phase 4 — `TableSource` (custom qsrv Source publishing NTTable)

**T4.1 — Write `tableSource.h`**
```cpp
namespace pvxs { namespace ioc { class TableSource final : public pvxs::server::Source {
public:
    TableSource();
    void onSearch(Search& op) override;
    void onCreate(std::unique_ptr<server::ChannelControl>&& op) override;
    List onList() override;
private:
    struct RecInfo { const char* type; dbCommon* prec; };
    std::map<std::string, RecInfo> records;
}; }} // namespace
```
*Outputs:* `tableRecordSup/src/tableSource.h`. *Deps:* none.

**T4.2 — Write `tableSource.cpp`**
- **Constructor:** walk `dbStatic` via `DBENTRY`, find all records of type
  `tableA` and `tableB`, populate `records`. Pattern reference:
  `pvxs/ioc/dbentry.cpp`.
- **`onSearch`:** for each Name, `if (records.count(op.name())) op.claim();`.
- **`onCreate`:** look up the record; build an `pvxs::nt::NTTable` prototype
  by iterating its active columns:
    - For `tableA`: read `NCOL`, `COLNAMES`, `COLTYPES` (under `dbScanLock`).
    - For `tableB`: read `NCOL`, `COLiNAME`, `COLiFTVL` for each active col.
    - Map menuFtype → pvxs `TypeCode` (`DOUBLE`→`Float64`, etc.).
    - Call `add_column(typeCode, name, label)` for each.
    - `Value proto = builder.create();` then keep `proto.cloneEmpty()` cached
      for monitor updates (per the doc note at `pvxs/src/pvxs/nt.h:115-117`).
  - Install `setHandler` callbacks for GET (snapshot under lock → build
    Value), PUT (decode Value → write column buffers under lock → call
    `dbProcess`), and MONITOR (subscribe via the EPICS event mechanism the
    same way `singlesource.cpp` does).
- **`onList`:** return a `List` of all known channel names.

*Outputs:* `tableRecordSup/src/tableSource.cpp`. *Deps:* T4.1, T1.1, T2.1.

**T4.3 — Write `tableSourceRegistrar.cpp`**
- Implement `static void addTableSource() { … }` that constructs a
  `TableSource`, calls
  `pvxs::ioc::server().addSource("tableSrc", std::make_shared<TableSource>(), 1)`.
  Priority 1 places it above the default `qsrvSingle` (priority 0), so
  channels for table records are claimed by us before SingleSource sees them.
- Register the iocsh command with the standard EPICS macros:
  ```cpp
  static const iocshFuncDef funcDef = {"addTableSource", 0, nullptr};
  static void callFunc(const iocshArgBuf*) { addTableSource(); }
  static void registerCmds(void) { iocshRegister(&funcDef, callFunc); }
  extern "C" { epicsExportRegistrar(registerCmds); }
  ```
*Outputs:* `tableRecordSup/src/tableSourceRegistrar.cpp`. *Deps:* T4.2.

**T4.4 — Add `registrar(registerCmds)` to `tableRecordSup.dbd`**
*Outputs:* portion of `tableRecordSup.dbd`. *Deps:* T4.3.

**T4.5 — End-to-end NTTable verification**
From a separate shell:
- `pvget TST:A:Sim` — expect `epics:nt/NTTable:1.0` value with `labels` from
  COLNAMES and the right per-column arrays.
- `pvmonitor TST:B:Sim` — expect one update per SCAN tick, one consistent
  NTTable per update.
- `pvput TST:B:Soft value.x='[1,2,3]' value.y='[4,5,6]'` — atomic write of
  two columns; the subscriber sees a single update.
*Deps:* T3.2, T4.4.

**Phase 4 parallelism:** T4.1 immediately; T4.2 after T4.1 + the records
exist; T4.3, T4.4 after T4.2; T4.5 last.

---

## Phase 5 — Documented follow-up: upstream pvxs/qsrv patch

Not implemented in the POC. Sketch of the change so it isn't lost.

**T5.1 — Patch `pvxs/ioc/singlesource.cpp::getValuePrototype()`**
(`pvxs/ioc/singlesource.cpp:189-206`). Inspect
`dbChannelRecord(chan)->rdes->name`; if it equals `"tableA"` or `"tableB"`,
branch to a new helper `buildTableValuePrototype(chan)` that constructs an
`NTTable` by reading sibling fields off the same record.

**T5.2 — Add packing/unpacking in `IOCSource::get` and `IOCSource::put`**
gated on the same record-type check.

**T5.3 — Optional generalization**
Introduce a registry mapping record-type name → `{ prototypeBuilder, getter,
putter }`, so other custom record types can plug in without further
`if/else`. This is what would make tableRecord upstream-ready.

The decision to actually pursue T5.x should follow the A-vs-B comparison
documented in T6.1.

---

## Phase 6 — Comparison & write-up

**T6.1 — A vs B comparison document**
After both POCs work end-to-end, write a one-page comparison in
`COMPARISON.md` covering:
- `.dbd` readability and verbosity.
- RSET implementation complexity (LOC of `*Record.c`).
- Link-handling ergonomics (whole-record INP vs per-column INP).
- Memory footprint at runtime.
- TableSource code size delta for handling each design.
- A recommendation for which design to upstream (or whether to keep both).

*Deps:* T4.5.

---

## Verification (end-to-end)

A run of the POC is "done" when:

1. `make` from the repo root completes cleanly.
2. The IOC starts via `iocBoot/iocTable/st.cmd` without errors.
3. `dbpr` shows populated fields on the Sim records.
4. `pvget` returns a valid `epics:nt/NTTable:1.0` for each record.
5. `pvmonitor` ticks once per SCAN tick, with consistent (atomic) updates.
6. Atomic multi-column `pvput` round-trips correctly.
7. `COMPARISON.md` exists and recommends a design.

## Dependency graph (compact)

```
T0.1 ─┐
T0.2 ─┼─→ T0.4 ──┐
T0.3 ─┘          │
T0.5 ────────────┤
                 │
T1.1 → T1.2 ─┐   │
       T1.3 ─┼→ T1.5 → T1.6 ─┐
       T1.4 ─┘                │
                              ├→ T3.1 ─┐
T2.1 → T2.2 ─┐                │        │
       T2.3 ─┼→ T2.5 → T2.6 ─┘        │
       T2.4 ─┘                         │
                                       ├→ T3.2 → T3.3
T4.1 → T4.2 → T4.3 → T4.4 ─────────────┘            ↓
                              ↓                    T4.5 → T6.1
                              T3.2 (via st.cmd)
```

T0.x, Phase 1, Phase 2, and T4.1 can all start in parallel. The Sim DSETs
(T1.4, T2.4) are independent of the Soft DSETs (T1.3, T2.3) and can each be
done by a separate implementer.
