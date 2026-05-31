# Variable-length strings in `tableRecord` — design evaluation

Evaluation of how to support **variable-length string columns** in the
`tableRecord` POC (records `tableA`/`tableB`), served over **PV Access only**
(no Channel Access requirement). Written read-only; no code changed.

Key architectural fact that shapes everything below: **this project does not
serve table columns through EPICS base's QSRV / `DBF→DBR` conversion path.** It
ships a bespoke PVXS `TableSource` (`tableRecordSup/src/tableSource.cpp`) that
snapshots each column straight out of record memory (`prec->colNNval`) under
`dbScanLock`, builds an `NTTable`, and posts it. Columns travel:

```
record buffer (colNNval)  →  fillCol()  →  pvxs string[]   (GET/MONITOR)
pvxs string[]             →  drainCol() →  record buffer    (PUT)
```

Both `fillCol` and `drainCol` are code we own (`tableSource.cpp:51` and `:85`).
The 40-char wall (`MAX_STRING_SIZE`) lives in base's `DBR_STRING` conversion
routines (`dbConvert.c` `getStringString`/`putStringString`) and in QSRV's
`pvif.cpp` — **none of which our column data passes through.** That bypass is
what makes every option below feasible; we are extending our own ~5 files, not
fighting base's type system.

Relevant existing mechanics:

- `tableBRecord.dbd`: each column is `COLnnFTVL` (`DBF_MENU`, `menu(menuFtype)`)
  + `COLnnVAL` (`DBF_NOACCESS`, `special(SPC_DBADDR)`, `pp(TRUE)`,
    `extra("void *colNNval")`).
- `tableBRecord.c:101` `init_record` allocates each column
  `callocMustSucceed(maxrows, dbValueSize(ftvl), …)`.
- `tableBRecord.c:184` `cvt_dbaddr` sets `field_type = ftvl`,
  `field_size = dbValueSize(ftvl)`, `no_elements = maxrows`.
- `tableSource.cpp:32` `ftypeToTC` maps `DBF_STRING → TypeCode::String`; `:55`
  reads a STRING column as `buf + r*MAX_STRING_SIZE`.
- The whole codebase treats `ftvl` as a **DBF type code**: `dbValueSize(ftvl)`
  and `ftypeToTC(ftvl)` both assume `choice index == DBF_*`.

Verified base constants: `MAX_STRING_SIZE = 40` (`libcom/.../epicsTypes.h:59`);
`epicsString { unsigned length; char *pString; }` exists but is annotated "may
vanish" (`epicsTypes.h:64`); `SPC_NOMOD=2`, `SPC_MOD=100`, `SPC_DBADDR=104`
(`special.h`).

---

## Approach 1 — New `VSTRING` menu value (custom `menuTableFtype`)

Replace `menu(menuFtype)` on the `COLnnFTVL` fields with a project-local
`menu(menuTableFtype)` that repeats the 12 base choices **in the same order**
(`STRING=0 … ENUM=11`) and appends `VSTRING=12`. Store the column as an array of
`char*` (the "array of pointers" idea). `TableSource` already keys off record
fields directly, so serialization changes are localized.

Changes (~5 files): add the menu + switch 8 `COLnnFTVL` fields to it; in
`init_record` allocate `maxrows*sizeof(char*)` for VSTRING and exempt it from
the `if (ftvl > DBF_ENUM) ftvl = DBF_DOUBLE` clamp (`tableBRecord.c:134`);
`ftypeToTC`/`fillCol`/`drainCol` gain a VSTRING case; sim/soft device support
gain a VSTRING fill (soft's `dbGetLink` cannot carry VSTRING — skip it).

**Sharp edge — menu value 12 collides with `DBF_MENU`.** `dbGetConvertRoutine`
is `[DBF_DEVICE+1][DBR_ENUM+1]` and `DBF_MENU == 12`. If `cvt_dbaddr` ever
advertises `field_type = 12`, any dbAccess/CA touch dispatches through the *menu*
conversion routines against a pointer buffer → garbage or crash. Mitigation
(mandatory): keep the logical type in the menu field (`prec->colNNftvl`, which is
what `TableSource` reads) and have `cvt_dbaddr` advertise a **benign** type
(e.g. `DBF_STRING` with `no_elements = 0`) so non-PVA access **fails closed**.
Every index ≥12 collides with *some* special DBF type, so this decoupling is
required regardless of the chosen value.

Property worth noting: with the benign-type decoupling, **base access fails
closed** — a stray `caget`/`caput`/autosave on a VSTRING column gets an
error/empty, never corruption.

---

## Approach 2 — Side-flag `COLnnVSTR` (keep `FTVL=STRING`)

Keep `FTVL=STRING` and add a per-column boolean `COLnnVSTR`. "Variable string" =
`ftvl==STRING && vstr==1`. No custom menu; advertised `field_type` stays a valid
`DBF_STRING=0`, so Approach 1's value-12 collision never arises. Lower-risk hack;
trade-off is that "variable string" is a side-flag, not a selectable entry in the
FTVL menu (OPI/menu tools won't list it as a column type).

---

## Approach 3 — Abuse `DBF_STRING`: inline SSO with overflow pointer

**The proposal.** Keep `FTVL=STRING`. Reinterpret each existing 40-byte slot as a
small-string-optimization cell: first bytes hold the start of the string inline;
the last 8 bytes are either all-zero ("string fits inline") or a pointer to a
heap buffer holding the remainder.

### 3.1 Why it is appealing

- **Zero DBD plumbing.** No new menu, no new field. `FTVL=STRING` already exists
  and already maps `DBF_STRING → TypeCode::String` in `ftypeToTC`
  (`tableSource.cpp:35`).
- **No allocation change.** `init_record` already allocates
  `maxrows * dbValueSize(STRING) = maxrows*40` — exactly enough for 32 inline +
  8 pointer. `cvt_dbaddr` already sets `field_type=STRING`, `field_size=40`.
- **Change is localized** to `fillCol`/`drainCol` (and the sim/soft fills): read
  the 40 bytes as SSO instead of as a plain C string.
- **Unique upside — graceful CA degradation.** Because the slot is a *legal*
  `DBF_STRING` to base, a legacy `caget COLnnVAL` still returns *something*
  readable (the inline prefix) instead of failing. For short strings the CA read
  is fully correct. (See the firewall refinement in 3.3 to make this safe.)

### 3.2 Why "legal STRING to base" is also the core hazard

The same property that enables graceful reads means **base will happily process
these slots as strings on every uncontrolled path — and corrupt them.**

- **Reads leak pointer bytes.** `getStringString` does
  `strncpy(dst, slot, MAX_STRING_SIZE-1=39)`. For a slot with an overflow
  pointer, bytes 32–38 are raw pointer bits → a `caget`/`dbpr`/CA-monitor copies
  heap-address bytes onto the wire. Wrong value **and** an address-disclosure.
- **Writes clobber the pointer → leak/corruption.** `COLnnVAL` has `pp(TRUE)`. A
  `caput`/`dbpf`, an autosave restore, or a database OUT-link targeting
  `COLnnVAL` calls `putStringString` → `strncpy(slot, src, 40)`, overwriting
  bytes 32–39. If that slot held an overflow pointer, it is now **lost (leak)**
  or **half-overwritten (wild pointer)** → a later `free()` corrupts the heap /
  crashes. This is exactly the failure the user's "protect VAL to prevent leaks"
  question targets.
- **Autosave is a live hazard.** If `COLnnVAL` is in a `.sav` set, autosave reads
  it as a string array (capturing pointer bytes) and on restore writes them back
  via `dbPut` → wild pointer → crash. These fields must be excluded from
  autosave.

Contrast with Approaches 1–2: there, base access **fails closed**; here it
**succeeds and corrupts**, which is more dangerous because it looks legal.

### 3.3 Layout details that must be specified

- **Alignment works — but only by luck of the constant.** Slot *i* starts at
  `base + i*40`; calloc gives ≥16-byte base alignment; `40 ≡ 0 (mod 8)` and the
  pointer offset `32 ≡ 0 (mod 8)`, so the 8-byte pointer is naturally aligned.
  This silently breaks if `MAX_STRING_SIZE` ever stops being a multiple of 8.
  Guard with `STATIC_ASSERT(MAX_STRING_SIZE % 8 == 0)` and
  `STATIC_ASSERT(MAX_STRING_SIZE >= 32 + sizeof(char*))`. Note `32+8 == 40`
  consumes the **entire** slot — zero slack, no room for an inline NUL when the
  32 inline bytes are fully used.
- **Sentinel / length convention.** `pointer==0` ⇒ whole string inline,
  NUL-terminated within the inline region. `pointer!=0` ⇒ inline region is all
  content (no NUL) and the heap buffer holds the NUL-terminated remainder; total
  length is `inline_len + strlen(heap)`.
- **Recommended refinement — a NUL "firewall" at byte 31.** Use bytes 0..30 (31
  inline chars) + a **forced NUL at byte 31** + pointer at bytes 32..39
  (`31+1+8 = 40`). Then a base `getStringString` stops at the byte-31 NUL and
  copies exactly the first 31 chars, **clean and with no pointer disclosure** —
  making CA *reads* degrade safely. (CA *writes* are still destructive; see 3.4.)
  Cost: one inline char.
- **Pointer size / portability.** Reserve the full 8 bytes even on 32-bit
  targets; never serialize the pointer (it stays process-local, so endianness is
  irrelevant). `drainCol`/sim must `free` the old overflow pointer before
  overwriting a slot, and a `free_vstring_column()` helper must run on column
  re-alloc / record teardown — all under `dbScanLock`.

### 3.4 Can we "protect the VAL fields" to prevent leaks? (the user's question)

The intended writer — `TableSource::putValueB` (`tableSource.cpp:227`) — writes
**directly into the buffer + `dbProcess`**, bypassing `dbPut`/`putStringString`
entirely, so it is already safe and can be made SSO-aware. **All danger is from
*uncontrolled* base writes** (`caput`, `dbpf`, autosave restore, DB OUT-links).
Protection options, best-effort honest assessment:

1. **Reject writes in the record `special()` hook — AVAILABLE (verified in
   base).** Implement `rset->special(paddr, pass)` and return an error for
   `COLnnVAL` at `pass==0`, aborting the put before any byte copy. Confirmed
   call chain in `dbAccess.c` (this session):
   - `dbPut` value path (`dbAccess.c:1350-1353`): `if (special) { status =
     dbPutSpecial(paddr,0); if (status) return status; }` — runs **before** the
     `dbPutConvertRoutine[...]` copy and before `put_array_info` (line 1367+).
   - `dbPutSpecial` (line 111): `if (special < 100)` handles the global cases
     (`SPC_NOMOD`/`SPC_SCAN`/`SPC_AS`); the `else` branch — taken for
     `special >= 100`, which **includes `SPC_DBADDR = 104`** — calls
     `rset->special(paddr, pass)` and returns its status if nonzero.

   So a `SPC_DBADDR` field (which `COLnnVAL` must be, for `cvt_dbaddr`) **does**
   invoke `rset->special` on put. (The "Brutal hack" `SPC_DBADDR` check at
   `dbAccess.c:642` is in the address-resolution / GET path — `dbNameToAddr` /
   `dbGetField` — not the put path, so it does not pre-empt this.) Returning
   `S_db_noMod` (or a custom status) from `special()` for `COLnnVAL` cleanly
   blocks every `dbPutField`-based write — `caput`, `dbpf`, DB OUT-links,
   autosave restore — **before the overflow pointer can be clobbered**, while
   leaving the legitimate writers untouched: `TableSource::putValueB` and the
   device-support `read_table` both write the buffers directly (not via
   `dbPut`), so they bypass `special()` entirely. Note the record currently has
   `#define special NULL` (`tableBRecord.c:33`); a real `special()` must be
   implemented. `special(paddr,1)` (after) would be too late — bytes already
   written — so the block must be at `pass==0`.
2. **Access Security read-only ASG.** Deny write (and optionally read) permission
   on the table records' ASG. Blocks CA/PVA external puts and can also hide the
   pointer-byte disclosure on reads. Limitation: AS is enforced at the
   `dbPutField`/`dbChannel` layer, so internal `dbPutLink` from another record's
   OUT link may bypass it — unusual for these fields, but not guaranteed.
3. **Make the field opaque to base (fail-closed) — most robust.** Have
   `cvt_dbaddr` advertise a benign view (e.g. `no_elements = 0`, or do not treat
   `COLnnVAL` as a normal array) so base reads/writes can't touch the SSO bytes.
   But this is the crux of the whole approach: **once base can't touch the slot,
   `FTVL=STRING` buys nothing** — you've reproduced Approach 1's "opaque buffer +
   benign advertised type," at which point a clean `char*[]` (8 bytes/elem) is
   strictly better than 40-byte SSO cells. The SSO layout earns its keep *only*
   while base is allowed to read it (graceful CA reads); and exactly then writes
   are unprotected by (3).
4. **Excluding from autosave + documenting PVA-only** is mandatory regardless of
   1–3.

**The tension is intrinsic:** SSO's only unique benefit (legacy CA reads see the
prefix) requires base to read the slot, which is the same door through which base
writes corrupt it. You can protect reads (firewall NUL, 3.3) but protecting
writes requires blocking base access, which removes the benefit.

### 3.5 Cost/benefit vs Approaches 1–2

| | A1 VSTRING (`char*[]`) | A2 side-flag | A3 SSO-in-40 |
|---|---|---|---|
| DBD changes | new menu + 8 fields | 1 bool field | none |
| Bytes/element | 8 | 40 (unchanged) | 40 |
| Base access on a var-string column | fails **closed** | fails closed | **succeeds & corrupts** |
| Legacy CA read of short string | fails | fails | works (nice) |
| External-write leak risk | none (opaque) | none | high unless writes blocked |
| Autosave-safe by default | yes (opaque) | yes | **no** (must exclude) |
| Pointer-byte disclosure | none | none | yes (mitigated by firewall NUL) |
| Coupling to base internals | menu-order only | none | `MAX_STRING_SIZE` value & mult-of-8 |

### 3.6 UTF-8 encoding across the inline / overflow boundary

**PVA string semantics.** pvData defines its `string` scalar as UTF-8. On the
wire each element is a length-prefixed byte sequence whose length is a **byte
count, not a code-point count**. pvxs carries the bytes verbatim and does not
itself validate UTF-8; clients (p4p/Python, Java) typically *decode* as UTF-8 and
may substitute U+FFFD (�) or raise on malformed input. The practical
consequence for us: **we never transcode.** "Encoding in UTF-8" here means
treating each column element as an opaque UTF-8 byte string end-to-end —
record buffer → `fillCol` → pvxs `std::string` and back — so whatever valid
UTF-8 a producer wrote is exactly what a consumer reads.

**"31 chars" is really 31 bytes.** The inline region (bytes 0..30, firewall NUL
at 31) holds 31 *bytes*: between 7 characters (all 4-byte code points) and 31
characters (all ASCII). A 20-character CJK string is 60 bytes and overflows. Any
"max inline length" we document must be stated in bytes, not characters.

**The boundary straddle — and why it does NOT corrupt the PVA value.** A
multi-byte code point can begin at byte 29 or 30 and need a continuation byte at
or past the firewall at byte 31. The key finding: **this is only a problem for
the physical inline cut and the legacy-CA preview, never for the PVA value** —
*provided* `fillCol` reassembles `inline_bytes ++ overflow_bytes` byte-for-byte
before building the pvxs string. The producer's exact byte sequence is
reconstructed no matter where the split lands, so the PVA consumer always sees
complete, valid UTF-8 (when the input was valid). So the real question is not
"how do we encode a straddling code point" — we don't re-encode, we splice bytes
— but "where do we place the physical cut, and what does a CA reader see."

**Two cut strategies:**

- **(a) Byte-boundary cut (simple).** Fill inline bytes 0..30 regardless of
  code-point boundaries; remainder to heap. Reassembly is trivial and
  PVA-correct. **But** the CA preview (`getStringString`/`strncpy` stopping at
  the firewall NUL) can end mid-code-point → a *malformed* UTF-8 fragment
  (dangling lead byte, or orphaned `10xxxxxx` continuation bytes). Display tools
  / archivers may render �.
- **(b) Code-point-boundary cut (clean preview).** Choose the largest code-point
  boundary `K ≤ 31`, NUL-terminate inline at `K`, send bytes `K..end` to heap.
  The CA preview is then valid (if truncated) UTF-8. Finding `K` is a backward
  scan: start at `min(31, len)` and walk left while `(byte & 0xC0) == 0x80`
  (continuation byte); the first non-continuation byte is the boundary. Cost: up
  to 3 wasted inline bytes. No degenerate case — a single code point is ≤ 4
  bytes ≤ 31, so `K` is always ≥ the first code point.

**Length & NUL-safety.** With (b), `inline_len = K = strlen(inline)` and
`total = K + strlen(heap)` — but this relies on no embedded `U+0000`. To be fully
NUL-safe (and to skip `strlen`), make the overflow allocation length-prefixed,
e.g. `struct { uint32_t rem_len; char rem[]; }`, or store the *whole* string
there and treat the inline region purely as a CA-preview cache. A length-prefixed
heap also lets the PVA value carry embedded NULs that a C-string heap cannot.

**Shortcomings specific to UTF-8:**

- **Malformed CA preview** unless (b) is implemented: the firewall NUL guarantees
  *termination*, not *validity*.
- **Grapheme clusters / normalization.** A code-point boundary is not a
  *grapheme* boundary. Combining marks, ZWJ emoji (👩‍👩‍👧), regional-indicator
  flags, etc. span several code points; even a codepoint-clean cut can split a
  user-perceived character, so the CA preview can look broken while still being
  valid UTF-8. Honoring grapheme clusters needs ICU-class logic (out of scope) —
  promise "valid UTF-8," not "valid display."
- **Untrusted input.** pvxs may hand us invalid UTF-8. The backward boundary
  scan must tolerate garbage (treat unexpected bytes as boundaries / fall back to
  byte-cut) so a buggy/malicious client can't drive an out-of-bounds read. The
  PVA round-trip stays byte-exact regardless of validity.
- **Embedded `U+0000`.** EPICS C-string paths and a NUL-terminated heap silently
  truncate at an embedded NUL; only the length-prefixed heap avoids it. Most
  EPICS tooling can't carry embedded NULs anyway — note as a known limit.
- **Expectation mismatch.** Callers think "characters"; the limit is bytes.
  Multi-byte-heavy locales overflow far sooner than expected, pushing nearly
  everything onto the heap and eroding the SSO benefit.
- **Cost/benefit reminder.** All of this UTF-8 complexity buys *only* the CA
  preview. The PVA path needs none of it — a byte-exact splice (a) is enough and
  always correct. If the CA preview isn't valued, use (a) and ignore UTF-8
  structure entirely; if it is valued, you inherit the codepoint scan plus the
  grapheme caveat. This further tilts the choice toward the opaque `char*[]`
  (Approach 1), which has no inline region and therefore no boundary question at
  all.

---

## Recommendation

1. **Lowest risk, recommended:** **Approach 2 (side-flag)** if you only need the
   *behavior*, or **Approach 1 (VSTRING menu value)** if you want "variable
   string" to be a first-class, selectable column type. Both store data opaquely
   so base access **fails closed**; both confine all changes to ~5 project files.
   For Approach 1, the `cvt_dbaddr` benign-type decoupling (value-12 collision)
   is a hard requirement.
2. **Approach 3 (abuse STRING) is feasible and the least DBD work, but it is the
   riskiest** and I do not recommend it as the primary path. Its single real
   advantage — graceful truncated CA reads — depends on base being allowed to
   read the slots, which is the same path by which base *writes* would otherwise
   corrupt the overflow pointers. **Good news on the user's "protect VAL" goal:
   it is achievable.** Reads are made safe with the byte-31 NUL firewall (3.3),
   and writes are reliably blocked by returning an error from the record's
   `rset->special(paddr,0)` for `COLnnVAL` — verified to fire on `SPC_DBADDR`
   puts (3.4 item 1) — which stops `caput`/`dbpf`/DB-link/autosave-restore
   clobbers before they touch the slot, without affecting the legitimate PVXS
   and device-support writers (they bypass `dbPut`). The fields must still be
   excluded from autosave (a captured slot would hold stale/over-long pointer
   bytes). The remaining judgement call: with `special()` blocking all base
   *writes*, base only ever *reads* these slots, so the SSO layout's benefit
   narrows to "legacy CA reads see a clean 31-char prefix." If that read-side
   nicety is not worth the `MAX_STRING_SIZE`-coupling and the extra hardening,
   a plain `char*[]` (Approach 1, 8 bytes/element, opaque) is the simpler way to
   get the same variable-length PVA behavior. On UTF-8: the PVA value is always
   correct as long as `fillCol` splices inline+overflow byte-for-byte; the only
   UTF-8 subtlety (a code point straddling the inline boundary) affects just the
   optional CA preview — see §3.6.

## Open / resolved verification items

- **RESOLVED — Does a `SPC_DBADDR` (104) put invoke `rset->special(paddr,0)`?**
  Yes. `dbPut` calls `dbPutSpecial(paddr,0)` whenever `special` is nonzero, and
  `dbPutSpecial` (`dbAccess.c:111`) routes `special >= 100` (incl. `SPC_DBADDR`)
  to `rset->special`, returning its status before the data copy. Write-blocking
  via `special()` is therefore available (3.4 item 1). Confirmed by direct
  inspection of `dbAccess.c` this session via grep + `cat -n` (the Read tool was
  returning stale output, but grep/cat were consistent across calls).
- `MAX_STRING_SIZE == 40` confirmed (`libcom/.../epicsTypes.h:59`). If Approach 3
  is chosen, add the two `STATIC_ASSERT`s from 3.3 to fail the build if that or
  the mult-of-8 invariant ever changes.

## Verification (once implemented, any approach)

- Build base + this module + IOC; load `db/tableB_demo.db` with a variable-string
  column carrying elements **> 40 chars**.
- `pvget`/`pvmonitor` the table PV: confirm the column introspects as `string[]`
  and long elements round-trip untruncated; `pvput` long elements and confirm
  round-trip.
- Memory-safety: run under `valgrind`/ASan across repeated `pvput` + record
  processing; confirm no leaks and no invalid frees (especially overwriting a
  long element with a short one and vice-versa).
- UTF-8 round-trip: `pvput`/`pvget` elements containing 2-, 3-, and 4-byte code
  points, including one deliberately positioned so a multi-byte code point
  straddles the inline boundary (e.g. 30 ASCII bytes + a 3-byte CJK char).
  Confirm the PVA value round-trips byte-exact regardless of the split. If the
  code-point-boundary cut (§3.6 (b)) is implemented, also confirm a legacy
  `caget COLnnVAL` returns a valid (truncated) UTF-8 preview rather than a
  fragment ending mid-code-point. Add a malformed-UTF-8 input case to confirm
  the boundary scan does not over-read.
- Approach 3 only: confirm `caput`/`dbpf` to `COLnnVAL` is rejected (or, if
  allowed, that it does not crash and does not leak — exercise the protection
  chosen in 3.4); confirm these fields are excluded from any autosave set.
