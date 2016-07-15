#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "vpi_user.h"

#define MAXLINE 4096
#define MAXWIDTH 10
#define MAXARGS 1024

static int rpipe;
static int wpipe;

static vpiHandle from_myhdl_net_handle[MAXARGS];
static vpiHandle to_myhdl_net_handle[MAXARGS];
int from_myhdl_net_count, to_myhdl_net_count;

static char changeFlag[MAXARGS];

/* prototypes */
static PLI_INT32 endofcompile_callback(p_cb_data cb_data);
static PLI_INT32 startofsimulation_callback(p_cb_data cb_data);
static PLI_INT32 change_callback(p_cb_data cb_data);

static int init_pipes();

static inline int startswith(const char *s, const char *prefix);

static inline int startswith(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

// FIXME: The "early" vpiFinish don't work. What to do???

static int init_pipes()
{
  char *w;
  char *r;

  static int init_pipes_flag = 0;

  if (init_pipes_flag) {
    return(0);
  }

  if ((w = getenv("MYHDL_TO_PIPE")) == NULL) {
    vpi_printf("ERROR: no write pipe to myhdl\n");
    vpi_control(vpiFinish, 1);  /* abort simulation */
    return(0);
  }
  if ((r = getenv("MYHDL_FROM_PIPE")) == NULL) {
    vpi_printf("ERROR: no read pipe from myhdl\n");
    vpi_control(vpiFinish, 1);  /* abort simulation */
    return(0);
  }
  wpipe = atoi(w);
  rpipe = atoi(r);
  init_pipes_flag = 1;
  return (0);
}

static PLI_INT32 endofcompile_callback(p_cb_data cb_data) {
    vpiHandle top_iter = vpi_iterate(vpiModule, NULL);
    vpiHandle top_module = vpi_scan(top_iter);
    fprintf(stderr, "top_module is %p\n", top_module);

    // Start iterating nets
    vpiHandle net_iter = vpi_iterate(vpiNet, top_module);
    vpiHandle net_handle;
    while ((net_handle = vpi_scan(net_iter))) {
        char *name = vpi_get_str(vpiName, net_handle);
        fprintf(stderr, "net %s\n", name);
        if (startswith(name, "to_myhdl_")) {
            if (to_myhdl_net_count == MAXARGS) {
                vpi_printf("ERROR: Too many to_myhdl nets\n");
                vpi_control(vpiFinish, 1);
            }
            to_myhdl_net_handle[to_myhdl_net_count++] = net_handle;
        } else if (startswith(name, "from_myhdl_")) {
            if (from_myhdl_net_count == MAXARGS) {
                vpi_printf("ERROR: Too many from_myhdl nets\n");
                vpi_control(vpiFinish, 1);
            }
            from_myhdl_net_handle[from_myhdl_net_count++] = net_handle;
        }
    }

    // debug
    for (int i = 0; i < to_myhdl_net_count; i++) {
        fprintf(stderr, "to_myhdl handle %p\n", to_myhdl_net_handle[i]);
    }
    for (int i = 0; i < from_myhdl_net_count; i++) {
        fprintf(stderr, "from_myhdl handle %p\n", from_myhdl_net_handle[i]);
    }

    return(0);
}

static PLI_INT32 startofsimulation_callback(p_cb_data cb_data) {
    char buf[MAXLINE];
    char s[MAXWIDTH];
    int n;

    init_pipes();

    // Send FROM
    strcpy(buf, "FROM 0 ");
    for (int i = 0; i < from_myhdl_net_count; i++) {
        size_t buf_left = sizeof(buf) - (strlen(buf) + 1);
        strncat(buf, vpi_get_str(vpiName, from_myhdl_net_handle[i]), buf_left);
        buf_left = sizeof(buf) - (strlen(buf) + 1);
        strncat(buf, " ", buf_left);
        sprintf(s, "%d ", vpi_get(vpiSize, from_myhdl_net_handle[i]));
        buf_left = sizeof(buf) - (strlen(buf) + 1);
        strncat(buf, s, buf_left);
    }
    n = write(wpipe, buf, strlen(buf));
    if ((n = read(rpipe, buf, MAXLINE)) == 0) {
        vpi_printf("Info: MyHDL simulator down\n");
        vpi_control(vpiFinish, 1);  /* abort simulation */
        return(0);
    }
    assert(n > 0);
    buf[n] = '\0';

    // Send TO
    strcpy(buf, "TO 0 ");
    for (int i = 0; i < to_myhdl_net_count; i++) {
        size_t buf_left = sizeof(buf) - (strlen(buf) + 1);
        strncat(buf, vpi_get_str(vpiName, to_myhdl_net_handle[i]), buf_left);
        buf_left = sizeof(buf) - (strlen(buf) + 1);
        strncat(buf, " ", buf_left);
        sprintf(s, "%d ", vpi_get(vpiSize, to_myhdl_net_handle[i]));
        buf_left = sizeof(buf) - (strlen(buf) + 1);
        strncat(buf, s, buf_left);
    }
    n = write(wpipe, buf, strlen(buf));
    if ((n = read(rpipe, buf, MAXLINE)) == 0) {
        vpi_printf("ABORT from $to_myhdl\n");
        vpi_control(vpiFinish, 1);  /* abort simulation */
        return(0);
    }
    assert(n > 0);
    buf[n] = '\0';

    // Register change callback
    s_cb_data cb_data_s;
    s_vpi_time time_s;
    s_vpi_value value_s;

    time_s.type = vpiSuppressTime;
    value_s.format = vpiSuppressVal;
    cb_data_s.reason = cbValueChange;
    cb_data_s.cb_rtn = change_callback;
    cb_data_s.time = &time_s;
    cb_data_s.value = &value_s;

    for (int i = 0; i < to_myhdl_net_count; i++) {
        changeFlag[i] = 0;
        int *id = malloc(sizeof(int));
        *id = i;
        cb_data_s.user_data = (PLI_BYTE8 *)id;
        cb_data_s.obj = to_myhdl_net_handle[i];
        vpi_register_cb(&cb_data_s);
    }

    return(0);
}

static PLI_INT32 change_callback(p_cb_data cb_data)
{
  int *id;

  id = (int *)cb_data->user_data;
  changeFlag[*id] = 1;
  return(0);
}

void myhdl_register()
{
    s_cb_data cb;

    // Normally the MyHDL VPI would register two systf here that the HDL calls.
    // GHDL doesn't actually support the ability to create foreign functions
    // that work in the right way, so we simply register an end of compile
    // callback that searches for signals matching a particular name.
    cb.reason = cbEndOfCompile;
    cb.cb_rtn = &endofcompile_callback;
    cb.user_data = NULL;
    vpi_register_cb(&cb);

    // We also need a callback when the simulation starts to do the initial
    // pipe setup.
    // XXX Do we need this, or can we do it in EndOfCompile?
    cb.reason = cbStartOfSimulation;
    cb.cb_rtn = &startofsimulation_callback;
    cb.user_data = NULL;
    vpi_register_cb(&cb);
}

void (*vlog_startup_routines[])() = {
    myhdl_register,
    0
};
