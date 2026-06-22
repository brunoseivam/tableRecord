#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include "dbAccess.h"
#include "dbFldTypes.h"
#include "dbLink.h"
#include "devSup.h"
#include "recGbl.h"
#include "alarm.h"
#include "errlog.h"
#include "epicsStdlib.h"
#include "epicsExport.h"

#include "tableRecord.h"
#include "tableRecordUtil.h"
#include "tableVStr.h"

/*
 * CSV Loader device support for the table record.
 *
 * The INP field must be a constant link holding the path to a CSV file, e.g.:
 *   field(INP, {const: "testfile.csv"})
 *
 * On every record processing (read_table call) the CSV file is re-opened and
 * re-parsed, so processing the record (e.g. caput .PROC 1 or PINI=YES) acts
 * as a reload.
 *
 * CSV dialect:
 *   - RFC-4180-ish: header row required, fields may be quoted with "
 *   - Quoted fields may contain commas and newlines
 *   - Two consecutive quotes ("") within a quoted field mean a literal quote
 *   - LF and CRLF line endings are both accepted
 *   - Blank lines (or lines that parse as a single empty field) are skipped
 *   - Data rows beyond MAXROWS are silently discarded (clamped)
 *
 * Column matching:
 *   - Active record data column names (C00NAME...) are matched against the
 *     header row; extra CSV columns are ignored.
 *   - A record column missing from the CSV header raises READ_ALARM/INVALID
 *     but still loads all the columns that did match.
 *
 * Conversions per CxxTYPE:
 *   - STRING via the vstring codec (supports >39-byte strings)
 *   - Numerics via epicsParse* (empty cell or parse error → 0, MINOR alarm)
 */

struct DevTableCsvPvt {
    std::string path;
    std::vector<TableRecordWrapper::DataColumn> data_cols;
};

/* ------------------------------------------------------------------ */
/* RFC-4180-ish CSV parser                                              */
/* ------------------------------------------------------------------ */

enum CsvState { CsvStartOfField, CsvUnquoted, CsvQuoted, CsvQuoteInQuoted };

/* Parse `fp` into header (first row) and rows (subsequent rows), stopping once
 * rows.size() == maxDataRows.  Returns true on success.  On unrecoverable parse
 * error (EOF inside a quoted field) returns false with err set. */
static bool csv_parse(FILE *fp,
                     size_t maxDataRows,
                     std::vector<std::string> &header,
                     std::vector<std::vector<std::string>> &rows,
                     std::string &err)
{
    header.clear();
    rows.clear();

    CsvState state = CsvStartOfField;
    std::string field;
    std::vector<std::string> record;
    bool headerDone = false;

    auto endField = [&]() {
        record.push_back(field);
        field.clear();
    };

    auto endRecord = [&]() -> bool {
        endField();
        /* skip blank lines: a record that is exactly one empty field */
        if (record.size() == 1 && record[0].empty()) {
            record.clear();
            return true;
        }
        if (!headerDone) {
            header = record;
            headerDone = true;
        } else {
            if (rows.size() < maxDataRows)
                rows.push_back(record);
        }
        record.clear();
        return true;
    };

    int c;
    while ((c = fgetc(fp)) != EOF) {
        switch (state) {
        case CsvStartOfField:
            if (c == '"') {
                state = CsvQuoted;
            } else if (c == ',') {
                endField();
                /* stay in StartOfField */
            } else if (c == '\n') {
                endRecord();
                if (rows.size() >= maxDataRows && headerDone)
                    return true; /* clamped */
            } else if (c == '\r') {
                int nx = fgetc(fp);
                if (nx != '\n' && nx != EOF)
                    ungetc(nx, fp);
                endRecord();
                if (rows.size() >= maxDataRows && headerDone)
                    return true;
            } else {
                field += (char)c;
                state = CsvUnquoted;
            }
            break;

        case CsvUnquoted:
            if (c == ',') {
                endField();
                state = CsvStartOfField;
            } else if (c == '\n') {
                endRecord();
                state = CsvStartOfField;
                if (rows.size() >= maxDataRows && headerDone)
                    return true;
            } else if (c == '\r') {
                int nx = fgetc(fp);
                if (nx != '\n' && nx != EOF)
                    ungetc(nx, fp);
                endRecord();
                state = CsvStartOfField;
                if (rows.size() >= maxDataRows && headerDone)
                    return true;
            } else {
                field += (char)c;
            }
            break;

        case CsvQuoted:
            if (c == '"') {
                state = CsvQuoteInQuoted;
            } else if (c == EOF) {
                err = "unterminated quoted field";
                return false;
            } else {
                field += (char)c;
            }
            break;

        case CsvQuoteInQuoted:
            if (c == '"') {
                /* doubled quote — literal " */
                field += '"';
                state = CsvQuoted;
            } else if (c == ',') {
                endField();
                state = CsvStartOfField;
            } else if (c == '\n') {
                endRecord();
                state = CsvStartOfField;
                if (rows.size() >= maxDataRows && headerDone)
                    return true;
            } else if (c == '\r') {
                int nx = fgetc(fp);
                if (nx != '\n' && nx != EOF)
                    ungetc(nx, fp);
                endRecord();
                state = CsvStartOfField;
                if (rows.size() >= maxDataRows && headerDone)
                    return true;
            } else {
                /* lenient: treat as end-of-quote + char */
                field += (char)c;
                state = CsvUnquoted;
            }
            break;
        }
    }

    /* Flush any final unterminated record (file ended without a trailing newline) */
    if (state == CsvQuoted) {
        err = "unterminated quoted field at end of file";
        return false;
    }
    if (!field.empty() || !record.empty())
        endRecord();

    return true;
}

/* ------------------------------------------------------------------ */
/* Per-cell numeric conversion                                          */
/* ------------------------------------------------------------------ */

static bool csv_convert_cell(const std::string &cell, epicsEnum16 type,
                           void *colbuf, epicsUInt32 row)
{
    if (cell.empty()) {
        /* empty cell → zero */
        memset((char *)colbuf + (size_t)row * dbValueSize(type), 0, dbValueSize(type));
        return false;
    }

    const char *s = cell.c_str();
    int ok = 0;
    switch (type) {
    case DBF_CHAR:   { epicsInt8   v=0; ok = (epicsParseInt8  (s,&v,0,NULL)==0); ((epicsInt8  *)colbuf)[row]=v; break; }
    case DBF_UCHAR:  { epicsUInt8  v=0; ok = (epicsParseUInt8 (s,&v,0,NULL)==0); ((epicsUInt8 *)colbuf)[row]=v; break; }
    case DBF_SHORT:  { epicsInt16  v=0; ok = (epicsParseInt16 (s,&v,0,NULL)==0); ((epicsInt16 *)colbuf)[row]=v; break; }
    case DBF_USHORT: { epicsUInt16 v=0; ok = (epicsParseUInt16(s,&v,0,NULL)==0); ((epicsUInt16*)colbuf)[row]=v; break; }
    case DBF_LONG:   { epicsInt32  v=0; ok = (epicsParseInt32 (s,&v,0,NULL)==0); ((epicsInt32 *)colbuf)[row]=v; break; }
    case DBF_ULONG:  { epicsUInt32 v=0; ok = (epicsParseUInt32(s,&v,0,NULL)==0); ((epicsUInt32*)colbuf)[row]=v; break; }
    case DBF_INT64:  { epicsInt64  v=0; ok = (epicsParseInt64 (s,&v,0,NULL)==0); ((epicsInt64 *)colbuf)[row]=v; break; }
    case DBF_UINT64: { epicsUInt64 v=0; ok = (epicsParseUInt64(s,&v,0,NULL)==0); ((epicsUInt64*)colbuf)[row]=v; break; }
    case DBF_FLOAT:  { epicsFloat32 v=0; ok=(epicsParseFloat32(s,&v,NULL)==0);   ((epicsFloat32*)colbuf)[row]=v; break; }
    case DBF_DOUBLE: { epicsFloat64 v=0; ok=(epicsParseFloat64(s,&v,NULL)==0);   ((epicsFloat64*)colbuf)[row]=v; break; }
    default:
        return false;
    }
    return ok != 0;
}

/* ------------------------------------------------------------------ */
/* Device support                                                       */
/* ------------------------------------------------------------------ */

static long csv_init_record(struct dbCommon *pcommon)
{
    tableRecord *prec = (tableRecord *)pcommon;
    struct link *plnk = &prec->inp;

    /* Pass 0: validate INP, then defer to pass 1. The active data columns
       (NUMCOLS) are only known after the record validates the column names,
       which happens after this pass-0 callback returns, so data_cols() must be
       captured in pass 1. */
    if (prec->pact != TABLEREC_DEVINIT_PASS1) {
        /* INP must be an instrument-IO link: field(INP, "@testfile.csv") */
        if (plnk->type != INST_IO || !plnk->value.instio.string
                                  || plnk->value.instio.string[0] == '\0') {
            recGblRecordError(S_db_badField, pcommon,
                "devTableCsv: INP must be an instrument-IO CSV file path, "
                "e.g. field(INP, \"@testfile.csv\")");
            return S_db_badField;
        }
        return TABLEREC_DEVINIT_PASS1;
    }

    /* Pass 1: column metadata is now valid */
    DevTableCsvPvt *pvt = new DevTableCsvPvt();
    pvt->path = plnk->value.instio.string;

    TableRecordWrapper rec(pcommon);
    rec.data_cols(pvt->data_cols);
    rec.set_private(pvt);
    return 0;
}

static long csv_read_table(tableRecord *prec)
{
    TableRecordWrapper      rec(*prec);
    DevTableCsvPvt         *pvt = rec.get_private<DevTableCsvPvt>();

    /* Open the CSV file */
    FILE *fp = fopen(pvt->path.c_str(), "rb");
    if (!fp) {
        errlogPrintf("%s devTableCsv: cannot open '%s'\n",
                     prec->name, pvt->path.c_str());
        recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
        return -1;
    }

    std::vector<std::string>              header;
    std::vector<std::vector<std::string>> rows;
    std::string                           err;

    bool ok = csv_parse(fp, prec->maxrows, header, rows, err);
    fclose(fp);

    if (!ok || header.empty()) {
        if (!ok)
            errlogPrintf("%s devTableCsv: parse error in '%s': %s\n",
                         prec->name, pvt->path.c_str(), err.c_str());
        else
            errlogPrintf("%s devTableCsv: '%s' has no header row\n",
                         prec->name, pvt->path.c_str());
        recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
        return -1;
    }

    /* Build a map from CSV column name to its index */
    std::map<std::string, size_t> hidx;
    for (size_t i = 0; i < header.size(); i++)
        hidx.emplace(header[i], i); /* first occurrence wins */

    epicsUInt32 nrows = (epicsUInt32)rows.size();
    bool anyMissing   = false;
    int  convErrs     = 0;

    for (auto &c : pvt->data_cols) {
        if (!*c.val)
            continue;

        auto it = hidx.find(c.config.name);
        if (it == hidx.end()) {
            anyMissing = true;
            errlogPrintf("%s devTableCsv: column '%s' not found in '%s'\n",
                         prec->name, c.config.name.c_str(), pvt->path.c_str());
            continue;
        }
        size_t csvCol = it->second;

        if (c.config.type == DBF_STRING) {
            std::vector<std::string> vals;
            vals.reserve(nrows);
            for (epicsUInt32 r = 0; r < nrows; r++) {
                if (csvCol < rows[r].size())
                    vals.push_back(rows[r][csvCol]);
                else
                    vals.emplace_back(); /* missing field in short row → "" */
            }
            rec.write_string_column(c, vals);
        } else {
            for (epicsUInt32 r = 0; r < nrows; r++) {
                std::string cell;
                if (csvCol < rows[r].size())
                    cell = rows[r][csvCol];
                if (!csv_convert_cell(cell, c.config.type, *c.val, r))
                    convErrs++;
            }
            *c.numrows = nrows;
            *c.chgd    = 1;
        }
    }

    if (anyMissing)
        recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
    else if (convErrs)
        recGblSetSevr(prec, READ_ALARM, MINOR_ALARM);

    return 0;
}

tabledset devTableCsv = {
    {5, NULL, NULL, csv_init_record, NULL},
    csv_read_table
};
epicsExportAddress(dset, devTableCsv);
