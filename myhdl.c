#include <stdio.h>
#include "vpi_user.h"

#define MAXLINE 4096
#define MAXWIDTH 10
#define MAXARGS 1024

static vpiHandle from_myhdl_net_handle[MAXARGS];
static vpiHandle to_myhdl_net_handle[MAXARGS];

static PLI_INT32 endofcompile_callback(p_cb_data cb_data) {
    vpiHandle top_iter = vpi_iterate(vpiModule, NULL);
    vpiHandle top_module = vpi_scan(top_iter);
    fprintf(stderr, "top_module is %p\n", top_module);

    // Start iterating nets
    vpiHandle net_iter = vpi_iterate(vpiNet, top_module);
    vpiHandle net_handle;
    while ((net_handle = vpi_scan(net_iter))) {
        char *n = vpi_get_str(vpiName, net_handle);
        fprintf(stderr, "net %s\n", n);
    }

    return(0);
}

void myhdl_register()
{
    // Normally the MyHDL VPI would register two systf here that the HDL calls.
    // GHDL doesn't actually support the ability to create foreign functions
    // that work in the right way, so we simply register an end of compile
    // callback that searches for signals matching a particular name.
    s_cb_data cb;

    cb.reason = cbEndOfCompile;
    cb.cb_rtn = &endofcompile_callback;
    cb.user_data = NULL;
    vpi_register_cb(&cb);
}

void (*vlog_startup_routines[])() = {
    myhdl_register,
    0
};
