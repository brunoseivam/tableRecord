#pragma once

#include "tableRecord.h"
#include "tableVStr.h"

#include <string>
#include <vector>

struct TableRecordWrapper {
    struct DataColumnConfig {
        std::string name;
        std::string label;
        epicsEnum16 type;

        DataColumnConfig(const std::string & name, const std::string & label,
            epicsEnum16 type);
    };

    struct OptColumnConfig {
        std::string name;
        epicsEnum16 type;

        OptColumnConfig(const std::string & name, epicsEnum16 type);
    };

    struct DataColumn {
        struct DataColumnConfig config;
        DBLINK *inp;
        void **val;
        epicsUInt32 *numrows;
        epicsUInt8  *chgd;
        char        *label;   /* live pointer to the CxxLABEL field (40 bytes) */

        DataColumn(const std::string & name, const std::string & label,
            epicsEnum16 type, DBLINK *inp, void **val, epicsUInt32 *numrows,
            epicsUInt8 *chgd, char *labelfld);
    };

    struct OptColumn {
        struct OptColumnConfig config;
        DBLINK *inp;
        void **val;
        epicsUInt32 *numrows;
        epicsUInt8  *chgd;

        OptColumn(const std::string & name, epicsEnum16 type, DBLINK *inp, void **val,
            epicsUInt32 *numrows, epicsUInt8 *chgd);
    };

    tableRecord & rec;

    TableRecordWrapper(tableRecord & rec);
    TableRecordWrapper(struct dbCommon *prec);
    virtual ~TableRecordWrapper();

    template<typename T>
    void set_private(T* pvt) {
        rec.dpvt = (void*)pvt;
    }

    template<typename T>
    T* get_private() {
        return (T*)rec.dpvt;
    }

    // Returns the number of active data columns in the record
    // Where active means the column has a name set
    size_t num_data_cols();

    // Returns the maximum number of active data columns in the record
    // This is hard-coded in the record definition
    size_t max_data_cols();

    // Returns the number of active optional columns in the record
    // Where active means the column has a name set
    size_t num_opt_cols();

    // Returns the maximum number of active optional columns in the record
    // This is hard-coded in the record definition
    size_t max_opt_cols();

    // Returns the maximum number of rows in data columns
    size_t max_data_rows();

    // Returns the maximum number of rows in opt columns
    // (= number of active data columns: one opt row per data column)
    size_t max_opt_rows();

    void configure_data_column(size_t data_col_num, DataColumnConfig & data_col);
    void configure_opt_column(size_t opt_col_num, OptColumnConfig & opt_col);

    // Configures this record's active data columns, setting the following fields:
    // COLxxNAME, COLxxLABEL, COLxxTYPE. Intended to be called at record
    // initialization, pass 0. The input must be shorter than or equal to max_data_cols().
    // Returns the number of configured data columns
    size_t configure_data_columns(std::vector<DataColumnConfig> const & data_cols);

    // Configures this record's active optional columns, setting the following fields:
    // COLOPTxxNAME, COLOPTxxTYPE. Intended to be called at record initialization, pass 0.
    // The input must be shorter than or equal to max_opt_cols().
    // Returns the number of configured optional columns
    size_t configure_opt_columns(std::vector<OptColumnConfig> const & opt_cols);

    // Returns a list of active data columns
    void data_cols(std::vector<DataColumn> & cols);

    // Returns a list of active optional columns
    void opt_cols(std::vector<OptColumn> & cols);

    // ---- vstring cell codec (STRING columns only) ----

    // Write a single std::string into row `row` of the given column buffer.
    static void vstr_write_cell(void *colbuf, size_t row, const std::string &s);

    // Read a single string from row `row` of the given column buffer.
    static std::string vstr_read_cell(const void *colbuf, size_t row);

    // Write vals into a STRING data column: writes min(vals.size(), max_data_rows())
    // cells, sets *col.numrows and *col.chgd=1. No-op if !*col.val. Returns rows written.
    size_t write_string_column(DataColumn &col, const std::vector<std::string> &vals);

    // Write vals into a STRING optional column: writes min(vals.size(), max_opt_rows())
    // cells, sets *col.numrows and *col.chgd=1. No-op if !*col.val. Returns rows written.
    size_t write_string_column(OptColumn &col, const std::vector<std::string> &vals);

    // Read *col.numrows cells into out (out is cleared first).
    void read_string_column(const DataColumn &col, std::vector<std::string> &out);

    // Read *col.numrows cells into out (out is cleared first).
    void read_string_column(const OptColumn &col, std::vector<std::string> &out);
};