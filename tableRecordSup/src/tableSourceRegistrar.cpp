#include <memory>
#include <stdexcept>

#include <iocsh.h>
#include <epicsExport.h>

#include <pvxs/iochooks.h>

#include "tableSource.h"

static void addTableSource(void)
{
    try {
        /* Use priority -1 so tableSrc is checked first in the onCreate
           dispatch loop (pvxs iterates sources from lowest to highest
           priority and stops at the first accepting source). */
        pvxs::ioc::server().addSource(
            "tableSrc",
            std::make_shared<table::TableSource>(),
            -1);
    } catch (std::exception& e) {
        fprintf(stderr, "addTableSource: %s\n", e.what());
    }
}

static const iocshFuncDef addTableSourceFuncDef = {
    "addTableSource", 0, nullptr, "Register table record NTTable source\n"
};
static void addTableSourceCallFunc(const iocshArgBuf *) { addTableSource(); }

static void registerTableSourceCmds(void)
{
    iocshRegister(&addTableSourceFuncDef, addTableSourceCallFunc);
}

extern "C" {
    epicsExportRegistrar(registerTableSourceCmds);
}
