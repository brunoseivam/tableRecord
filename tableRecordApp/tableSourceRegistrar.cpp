#include <memory>
#include <stdexcept>

#include <initHooks.h>
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

/* Register the table NTTable source automatically during iocInit. By
   initHookAfterIocBuilt the database is fully initialized (record and device
   support init done) and the QSRV server exists but is not yet serving, which
   is the pvxs-documented point to add a custom source (see pvxs/iochooks.h). */
static void tableSourceInitHook(initHookState state)
{
    if (state == initHookAfterIocBuilt)
        addTableSource();
}

static void registerTableSourceCmds(void)
{
    initHookRegister(&tableSourceInitHook);
}

extern "C" {
    epicsExportRegistrar(registerTableSourceCmds);
}
