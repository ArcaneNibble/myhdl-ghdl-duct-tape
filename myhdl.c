#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "vpi_user.h"

#define MAXLINE 4096
#define MAXWIDTH 10
#define MAXARGS 1024

/* 64 bit type for time calculations */
typedef unsigned long long myhdl_time64_t;

static int rpipe;
static int wpipe;

static vpiHandle from_myhdl_net_handle[MAXARGS];
static vpiHandle to_myhdl_net_handle[MAXARGS];
int from_myhdl_net_count, to_myhdl_net_count;

static char changeFlag[MAXARGS];

// A copy of the receive buffer used for handling data from MyHDL
static char bufcp[MAXLINE];

static myhdl_time64_t myhdl_time;
static myhdl_time64_t verilog_time;
static myhdl_time64_t pli_time;
static int delta;

/* prototypes */
static PLI_INT32 endofcompile_callback(p_cb_data cb_data);
static PLI_INT32 startofsimulation_callback(p_cb_data cb_data);
static PLI_INT32 readonly_callback(p_cb_data cb_data);
static PLI_INT32 delay_callback(p_cb_data cb_data);
static PLI_INT32 delta_callback(p_cb_data cb_data);
static PLI_INT32 change_callback(p_cb_data cb_data);

static int init_pipes();

static myhdl_time64_t timestruct_to_time(const struct t_vpi_time*ts);
static inline int startswith(const char *s, const char *prefix);
static void binstr2hexstr(char *out, const char *in);
static void hexstr2binstr(char *out, const char *in);

static inline int startswith(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

// This function is needed because MyHDL wants signals encoded as hex, but the
// GHDL VPI interface supports only binary.
static void binstr2hexstr(char *out, const char *in) {
    char *out_orig = out;
    const char *in_orig = in;
    in = in + strlen(in);
    // In points to terminating null
    int nybble_count = 0;
    int nybble_temp = -1;
    const char * const nybble_lut = "0123456789ABCDEFZX";

    while ((in--) != in_orig) {
        // In points to a valid character
        switch (*in) {
            case '0':
                if (nybble_temp == -1) {
                    nybble_temp = 0;
                }
                // If Z/X then keep as Z/X
                // If valid val then don't need to do anything; it contains
                // all the needed zeros already.
                break;
            case '1':
                if (nybble_temp == -1) {
                    nybble_temp = (1 << nybble_count);
                } else if (nybble_temp != 16 && nybble_temp != 17) {
                    nybble_temp |= (1 << nybble_count);
                }
                // If Z/X then keep as Z/X
                break;
            case 'Z':
                if (nybble_temp == -1) {
                    nybble_temp = 16;
                } else if (nybble_temp != 16) {
                    nybble_temp = 17;
                }
                // By default, make it Z
                // If there is still a Z keep it
                // If there is anything else make it X
                break;
            default:
                // Anything else is an X
                nybble_temp = 17;
                break;
        }

        nybble_count++;
        if (nybble_count == 4) {
            assert(nybble_temp >= 0 && nybble_temp <= 17);
            *(out++) = nybble_lut[nybble_temp];
            nybble_count = 0;
            nybble_temp = -1;
        }
    }

    // Dump out last nybble
    if (nybble_count != 0) {
        assert(nybble_temp >= 0 && nybble_temp <= 17);
        *(out++) = nybble_lut[nybble_temp];
    }

    // Need to flip the output the right way around
    int len = out - out_orig;
    for (int i = 0; i < len / 2; i++) {
        char tmp = out_orig[i];
        out_orig[i] = out[-i - 1];
        out[-i - 1] = tmp;
    }

    // Put a null
    *out = '\0';
}

// This function is here for the same reason. It puts out extra bits, but that
// should be ok
static void hexstr2binstr(char *out, const char *in) {
    char c;
    while ((c = *(in++))) {
        switch (c) {
            case 'a':
            case 'b':
            case 'c':
            case 'd':
            case 'e':
            case 'f':
                // Make it uppercase
                c = c & ~0x20;
            case 'A':
            case 'B':
            case 'C':
            case 'D':
            case 'E':
            case 'F':
                c = c - ('A' - '0') + 10;
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                c = c - '0';

                *(out++) = c & (1 << 3) ? '1' : '0';
                *(out++) = c & (1 << 2) ? '1' : '0';
                *(out++) = c & (1 << 1) ? '1' : '0';
                *(out++) = c & (1 << 0) ? '1' : '0';
                break;
            case 'z':
            case 'Z':
                *(out++) = 'Z';
                *(out++) = 'Z';
                *(out++) = 'Z';
                *(out++) = 'Z';
                break;
            default:
                *(out++) = 'X';
                *(out++) = 'X';
                *(out++) = 'X';
                *(out++) = 'X';
                break;
        }
    }

    // Put a null
    *out = '\0';
}

/* from Icarus */
static myhdl_time64_t timestruct_to_time(const struct t_vpi_time*ts)
{
      myhdl_time64_t ti = ts->high;
      ti <<= 32;
      ti += ts->low & 0xffffffff;
      return ti;
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

    // Start iterating nets
    vpiHandle net_iter = vpi_iterate(vpiNet, top_module);
    vpiHandle net_handle;
    while ((net_handle = vpi_scan(net_iter))) {
        char *name = vpi_get_str(vpiName, net_handle);
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

    pli_time = 0;
    delta = 0;

    // register read-write callback //
    // GHDL maps RO synch to EndOfTimeStep, which is too late.
    // It maps RW synch to LastKnownDelta, which runs before postponed
    // processes but otherwise is early enough.
    time_s.type = vpiSimTime;
    time_s.high = 0;
    time_s.low = 0;
    cb_data_s.reason = cbReadWriteSynch;
    cb_data_s.user_data = NULL;
    cb_data_s.cb_rtn = readonly_callback;
    cb_data_s.obj = NULL;
    cb_data_s.time = &time_s;
    cb_data_s.value = NULL;
    vpi_register_cb(&cb_data_s);

    // pre-register delta cycle callback //
    time_s.type = vpiSimTime;
    time_s.high = 0;
    time_s.low = 1;
    cb_data_s.reason = cbAfterDelay;
    cb_data_s.user_data = NULL;
    cb_data_s.cb_rtn = delta_callback;
    cb_data_s.obj = NULL;
    cb_data_s.time = &time_s;
    cb_data_s.value = NULL;
    vpi_register_cb(&cb_data_s);

    return(0);
}


static PLI_INT32 readonly_callback(p_cb_data cb_data)
{
  vpiHandle net_handle;
  s_cb_data cb_data_s;
  s_vpi_time verilog_time_s;
  s_vpi_value value_s;
  s_vpi_time time_s;
  char buf[MAXLINE];
  // FIXME: Way oversize, unsafe due to buffer overflow
  char hexbuf[MAXLINE];
  int n;
  int i;
  char *myhdl_time_string;
  myhdl_time64_t delay;

  static int start_flag = 1;

  if (start_flag) {
    start_flag = 0;
    n = write(wpipe, "START", 5);  
    // vpi_printf("INFO: RO cb at start-up\n");
    if ((n = read(rpipe, buf, MAXLINE)) == 0) {
      vpi_printf("ABORT from RO cb at start-up\n");
      vpi_control(vpiFinish, 1);  /* abort simulation */
    }  
    assert(n > 0);
  }

  buf[0] = '\0';
  verilog_time_s.type = vpiSimTime;
  vpi_get_time(NULL, &verilog_time_s);
  verilog_time = timestruct_to_time(&verilog_time_s);
   if (verilog_time != (pli_time * 1000 + delta)) {
     vpi_printf("%u %u\n", verilog_time_s.high, verilog_time_s.low );
     vpi_printf("%llu %llu %d\n", verilog_time, pli_time, delta);
   }
  assert( (verilog_time & 0xFFFFFFFF) == ( (pli_time * 1000 + delta) & 0xFFFFFFFF ) );
  sprintf(buf, "%llu ", pli_time);
  value_s.format = vpiBinStrVal;
  for (i = 0; i < to_myhdl_net_count; i++) {
    if (changeFlag[i]) {
      net_handle = to_myhdl_net_handle[i];
      strcat(buf, vpi_get_str(vpiName, net_handle));
      strcat(buf, " ");
      vpi_get_value(net_handle, &value_s);
      binstr2hexstr(hexbuf, value_s.value.str);
      strcat(buf, hexbuf);
      strcat(buf, " ");
      changeFlag[i] = 0;
    }
  }
  n = write(wpipe, buf, strlen(buf));
  if ((n = read(rpipe, buf, MAXLINE)) == 0) {
    // vpi_printf("ABORT from RO cb\n");
    vpi_control(vpiFinish, 1);  /* abort simulation */
    return(0);
  }
  assert(n > 0);
  buf[n] = '\0';

  /* save copy for later callback */
  strcpy(bufcp, buf);

  myhdl_time_string = strtok(buf, " ");
  myhdl_time = (myhdl_time64_t) strtoull(myhdl_time_string, (char **) NULL, 10);
  delay = (myhdl_time - pli_time) * 1000;
  assert(delay >= 0);
  assert(delay <= 0xFFFFFFFF);
  if (delay > 0) { // schedule cbAfterDelay callback
    assert(delay > delta);
    delay -= delta;
    delta = 0;
    pli_time = myhdl_time;

    // register cbAfterDelay callback //
    time_s.type = vpiSimTime;
    time_s.high = 0;
    time_s.low = (PLI_UINT32) delay;
    cb_data_s.reason = cbAfterDelay;
    cb_data_s.user_data = NULL;
    cb_data_s.cb_rtn = delay_callback;
    cb_data_s.obj = NULL;
    cb_data_s.time = &time_s;
    cb_data_s.value = NULL;
    vpi_register_cb(&cb_data_s);
  } else {
    delta++;
    assert(delta < 1000);
  }
  return(0);
}

static PLI_INT32 delay_callback(p_cb_data cb_data)
{
  s_vpi_time time_s;
  s_cb_data cb_data_s;

  // register readwrite callback //
  time_s.type = vpiSimTime;
  time_s.high = 0;
  time_s.low = 0;
  cb_data_s.reason = cbReadWriteSynch;
  cb_data_s.user_data = NULL;
  cb_data_s.cb_rtn = readonly_callback;
  cb_data_s.obj = NULL;
  cb_data_s.time = &time_s;
  cb_data_s.value = NULL;
  vpi_register_cb(&cb_data_s);

  // register delta callback //
  time_s.type = vpiSimTime;
  time_s.high = 0;
  time_s.low = 1;
  cb_data_s.reason = cbAfterDelay;
  cb_data_s.user_data = NULL;
  cb_data_s.cb_rtn = delta_callback;
  cb_data_s.obj = NULL;
  cb_data_s.time = &time_s;
  cb_data_s.value = NULL;
  vpi_register_cb(&cb_data_s);

  return(0);
}

static PLI_INT32 delta_callback(p_cb_data cb_data)
{
  s_cb_data cb_data_s;
  s_vpi_time time_s;
  vpiHandle reg_handle;
  s_vpi_value value_s;
  char *hexstr;
  char binbuf[MAXLINE];

  if (delta == 0) {
    return(0);
  }

  /* skip time value */
  strtok(bufcp, " ");

  int i = 0;
  value_s.format = vpiBinStrVal;
  while ((hexstr = strtok(NULL, " ")) != NULL) {
    if (i < from_myhdl_net_count) {
      reg_handle = from_myhdl_net_handle[i++];
      hexstr2binstr(binbuf, hexstr);
      value_s.value.str = binbuf;
      vpi_put_value(reg_handle, &value_s, NULL, vpiNoDelay);
    }
  }

  // register readwrite callback //
  time_s.type = vpiSimTime;
  time_s.high = 0;
  time_s.low = 0;
  cb_data_s.reason = cbReadWriteSynch;
  cb_data_s.user_data = NULL;
  cb_data_s.cb_rtn = readonly_callback;
  cb_data_s.obj = NULL;
  cb_data_s.time = &time_s;
  cb_data_s.value = NULL;
  vpi_register_cb(&cb_data_s);

  // register delta callback //
  time_s.type = vpiSimTime;
  time_s.high = 0;
  time_s.low = 1;
  cb_data_s.reason = cbAfterDelay;
  cb_data_s.user_data = NULL;
  cb_data_s.cb_rtn = delta_callback;
  cb_data_s.obj = NULL;
  cb_data_s.time = &time_s;
  cb_data_s.value = NULL;
  vpi_register_cb(&cb_data_s);

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
