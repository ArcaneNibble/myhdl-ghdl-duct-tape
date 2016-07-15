#include <stdio.h>
#include "vpi_user.h"

void myhdl_register()
{
    fprintf(stderr, "Hello world!\n");
}

void (*vlog_startup_routines[])() = {
      myhdl_register,
      0
};
