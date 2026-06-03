#pragma once

#include "tableRecord.h"

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
        void **val;

        DataColumn(const std::string & name, const std::string & label,
            epicsEnum16 type, void **val);
    };

    struct OptColumn {
        struct OptColumnConfig config;
        void **val;

        OptColumn(const std::string & name, epicsEnum16 type, void **val);
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
    size_t max_opt_rows();

    // Configures this record's active data columns, setting the following fields:
    // COLxxNAME, COLxxLABEL, COLxxTYPE. Intended to be called at record
    // initialization. The input must be shorter than or equal to max_data_cols().
    // Returns the list of configured data columns
    size_t configure_data_columns(std::vector<DataColumnConfig> const & data_cols);

    // Configures this record's active optional columns, setting the following fields:
    // COLOPTxxNAME, COLOPTxxTYPE. Intended to be called at record initialization.
    // The input must be shorter than or equal to max_opt_cols().
    // Returns the list of configured data columns
    size_t configure_opt_columns(std::vector<OptColumnConfig> const & opt_cols);

    // Returns a list of active data columns
    std::vector<DataColumn> data_cols();

    // Returns a list of active optional columns
    std::vector<OptColumn> opt_cols();
};