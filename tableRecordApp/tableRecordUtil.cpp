#include "tableRecordUtil.h"

#include <string.h>
#include <errlog.h>

TableRecordWrapper::DataColumnConfig::DataColumnConfig(
    const std::string & name, const std::string & label, epicsEnum16 type
) : name(name), label(label), type(type)
{}

TableRecordWrapper::OptColumnConfig::OptColumnConfig(
    const std::string & name, epicsEnum16 type
) : name(name), type(type)
{}

TableRecordWrapper::DataColumn::DataColumn(
    const std::string & name, const std::string & label, epicsEnum16 type,
    DBLINK *inp, void **val, epicsUInt32 *numrows, epicsUInt8 *chgd
) : config(name, label, type), inp(inp), val(val), numrows(numrows), chgd(chgd)
{}

TableRecordWrapper::OptColumn::OptColumn(
    const std::string & name, epicsEnum16 type, DBLINK *inp, void **val,
    epicsUInt32 *numrows, epicsUInt8 *chgd
) : config(name, type), inp(inp), val(val), numrows(numrows), chgd(chgd)
{}

TableRecordWrapper::TableRecordWrapper(tableRecord & rec)
: rec(rec)
{}

TableRecordWrapper::TableRecordWrapper(struct dbCommon *prec)
: TableRecordWrapper::TableRecordWrapper(*(tableRecord*)prec)
{}

TableRecordWrapper::~TableRecordWrapper()
{}

size_t TableRecordWrapper::num_data_cols() {
    return rec.numcols;
}

size_t TableRecordWrapper::max_data_cols() {
    return TABLEREC_MAX_DATA_COLS;
}

size_t TableRecordWrapper::num_opt_cols() {
    assert(false);
    // TODO
    return 0;
}

size_t TableRecordWrapper::max_opt_cols() {
    return TABLEREC_MAX_OPT_COLS;
}

size_t TableRecordWrapper::max_data_rows() {
    return rec.maxrows;
}

size_t TableRecordWrapper::max_opt_rows() {
    return rec.numcols;
}

// Copy source into dest, fill remainder with NUL
static void copy_into(std::string const & source, char * dest, size_t dest_size) {
    size_t copied = source.copy(dest, dest_size - 1);
    for (size_t i = copied; i < dest_size; ++i)
        dest[i] = '\0';
}

size_t TableRecordWrapper::configure_data_columns(
    std::vector<TableRecordWrapper::DataColumnConfig> const & data_cols
) {
    const size_t NAME_SIZE = sizeof(rec.c00name);
    const size_t LABEL_SIZE = sizeof(rec.c00label);

    size_t num_data_cols = 0;

    char *name = rec.c00name;
    char *label = rec.c00label;
    epicsEnum16 *type = &rec.c00type;

    for (auto & data_col : data_cols) {
        // Stop at first column with an empty name
        if (data_col.name.empty())
            break;

        copy_into(data_col.name, name, NAME_SIZE);
        copy_into(data_col.label, label, LABEL_SIZE);
        *type = data_col.type;

        name += NAME_SIZE;
        label += LABEL_SIZE;
        type += 1;

        ++num_data_cols;
    }

    return num_data_cols;
}

size_t TableRecordWrapper::configure_opt_columns(
    std::vector<TableRecordWrapper::OptColumnConfig> const & opt_cols
) {
    const size_t NAME_SIZE = sizeof(rec.co00name);

    size_t num_opt_cols = 0;

    char *name = rec.co00name;
    epicsEnum16 *type = &rec.co00type;

    for (auto & opt_col : opt_cols) {
        // Stop at first column with an empty name
        if (opt_col.name.empty())
            break;

        copy_into(opt_col.name, name, NAME_SIZE);
        *type = opt_col.type;

        name += NAME_SIZE;
        type += 1;

        ++num_opt_cols;
    }

    return num_opt_cols;
}

/* Returns true if name satisfies pvxs identifier rules: [a-zA-Z_][a-zA-Z0-9_]* */
static bool valid_pvxs_simple_name(const char *name)
{
    if (!name || name[0] == '\0') return false;
    for (size_t i = 0; name[i]; i++) {
        char c = name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') continue;
        if (c >= '0' && c <= '9' && i > 0) continue;
        return false;
    }
    return true;
}

/* For optional column names: allows one dot separator (prefix.fieldname),
   validating each component with valid_pvxs_simple_name. */
static bool valid_pvxs_col_name(const char *name)
{
    if (!name) return false;
    const char *dot = strchr(name, '.');
    if (!dot)
        return valid_pvxs_simple_name(name);
    /* Validate prefix */
    size_t prefix_len = (size_t)(dot - name);
    char prefix[MAX_STRING_SIZE];
    if (prefix_len == 0 || prefix_len >= sizeof(prefix)) return false;
    memcpy(prefix, name, prefix_len);
    prefix[prefix_len] = '\0';
    return valid_pvxs_simple_name(prefix) && valid_pvxs_simple_name(dot + 1);
}

void TableRecordWrapper::data_cols(std::vector<TableRecordWrapper::DataColumn> & cols) {
    cols.clear();

    for (size_t i = 0; i < max_data_cols(); ++i) {
        char *name = rec.c00name + i*sizeof(rec.c00name);

        if (strlen(name) == 0)
            break;

        if (!valid_pvxs_simple_name(name)) {
            errlogPrintf("tableRecord '%s': data column %zu has invalid pvxs name \"%s\""
                         " (must match [a-zA-Z_][a-zA-Z0-9_]*), skipping\n",
                         rec.name, i, name);
            continue;
        }

        char *label = rec.c00label + i*sizeof(rec.c00label);
        epicsEnum16 type = *(&rec.c00type + i);
        DBLINK *inp = &rec.c00inp + i;
        void **val = &rec.c00val + i;
        epicsUInt32 *numrows = &rec.c00nrows + i;
        epicsUInt8  *chgd    = &rec.c00chgd  + i;

        cols.emplace_back(name, label, type, inp, val, numrows, chgd);
    }
}

void TableRecordWrapper::opt_cols(std::vector<TableRecordWrapper::OptColumn> & cols) {
    cols.clear();
    for (size_t i = 0; i < max_opt_cols(); ++i) {
        char *name = rec.co00name + i*sizeof(rec.co00name);

        if (strlen(name) == 0)
            break;

        if (!valid_pvxs_col_name(name)) {
            errlogPrintf("tableRecord '%s': optional column %zu has invalid pvxs name \"%s\""
                         " (must match [a-zA-Z_][a-zA-Z0-9_]*(\\.[a-zA-Z_][a-zA-Z0-9_]*)?), skipping\n",
                         rec.name, i, name);
            continue;
        }

        epicsEnum16 type = *(&rec.co00type + i);
        DBLINK *inp = &rec.co00inp + i;
        void **val = &rec.co00val + i;
        epicsUInt32 *numrows = &rec.co00nrows + i;
        epicsUInt8  *chgd    = &rec.co00chgd  + i;

        cols.emplace_back(name, type, inp, val, numrows, chgd);
    }
}

/* ------------------------------------------------------------------ */
/* vstring cell codec — C++ wrappers                                    */
/* ------------------------------------------------------------------ */

void TableRecordWrapper::vstr_write_cell(void *colbuf, size_t row,
                                         const std::string &s)
{
    tablerec_vstr_write(colbuf, (epicsUInt32)row, s.data(), (epicsUInt32)s.size());
}

std::string TableRecordWrapper::vstr_read_cell(const void *colbuf, size_t row)
{
    epicsUInt32 len = 0;
    const char *p = tablerec_vstr_read(colbuf, (epicsUInt32)row, &len);
    return std::string(p, len);
}

static size_t writeStringColumnImpl(void *buf, size_t maxrows,
                                    epicsUInt32 *numrows, epicsUInt8 *chgd,
                                    const std::vector<std::string> &vals)
{
    if (!buf)
        return 0;
    size_t n = vals.size() < maxrows ? vals.size() : maxrows;
    for (size_t r = 0; r < n; r++)
        tablerec_vstr_write(buf, (epicsUInt32)r, vals[r].data(),
                            (epicsUInt32)vals[r].size());
    *numrows = (epicsUInt32)n;
    *chgd    = 1;
    return n;
}

size_t TableRecordWrapper::write_string_column(DataColumn &col,
                                               const std::vector<std::string> &vals)
{
    return writeStringColumnImpl(*col.val, max_data_rows(),
                                 col.numrows, col.chgd, vals);
}

size_t TableRecordWrapper::write_string_column(OptColumn &col,
                                               const std::vector<std::string> &vals)
{
    return writeStringColumnImpl(*col.val, max_opt_rows(),
                                 col.numrows, col.chgd, vals);
}

static void readStringColumnImpl(const void *buf, epicsUInt32 nrows,
                                 std::vector<std::string> &out)
{
    out.clear();
    if (!buf)
        return;
    out.reserve(nrows);
    for (epicsUInt32 r = 0; r < nrows; r++)
        out.push_back(TableRecordWrapper::vstr_read_cell(buf, r));
}

void TableRecordWrapper::read_string_column(const DataColumn &col,
                                            std::vector<std::string> &out)
{
    readStringColumnImpl(*col.val, *col.numrows, out);
}

void TableRecordWrapper::read_string_column(const OptColumn &col,
                                            std::vector<std::string> &out)
{
    readStringColumnImpl(*col.val, *col.numrows, out);
}