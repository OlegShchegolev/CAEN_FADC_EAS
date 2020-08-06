#pragma once
#include <CAENDigitizer.h>
#include "WaveDump.h"
#include "WDconfig.h"
#include "WDplot.h"
#include "fft.h"
#include "keyb.h"
#include "X742CorrectionRoutines.h"

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

typedef enum {
	ERR_NONE = 0,
	ERR_CONF_FILE_NOT_FOUND,
	ERR_DGZ_OPEN,
	ERR_BOARD_INFO_READ,
	ERR_INVALID_BOARD_TYPE,
	ERR_DGZ_PROGRAM,
	ERR_MALLOC,
	ERR_RESTART,
	ERR_INTERRUPT,
	ERR_READOUT,
	ERR_EVENT_BUILD,
	ERR_HISTO_MALLOC,
	ERR_UNHANDLED_BOARD,
	ERR_OUTFILE_WRITE,
	ERR_OVERTEMP,

	ERR_DUMMY_LAST,
} ERROR_CODES;
static char ErrMsg[ERR_DUMMY_LAST][100] = {
	"No Error",                                         /* ERR_NONE */
	"Configuration File not found",                     /* ERR_CONF_FILE_NOT_FOUND */
	"Can't open the digitizer",                         /* ERR_DGZ_OPEN */
	"Can't read the Board Info",                        /* ERR_BOARD_INFO_READ */
	"Can't run WaveDump for this digitizer",            /* ERR_INVALID_BOARD_TYPE */
	"Can't program the digitizer",                      /* ERR_DGZ_PROGRAM */
	"Can't allocate the memory for the readout buffer", /* ERR_MALLOC */
	"Restarting Error",                                 /* ERR_RESTART */
	"Interrupt Error",                                  /* ERR_INTERRUPT */
	"Readout Error",                                    /* ERR_READOUT */
	"Event Build Error",                                /* ERR_EVENT_BUILD */
	"Can't allocate the memory fro the histograms",     /* ERR_HISTO_MALLOC */
	"Unhandled board type",                             /* ERR_UNHANDLED_BOARD */
	"Output file write error",                          /* ERR_OUTFILE_WRITE */
	"Over Temperature",									/* ERR_OVERTEMP */

};

long get_time();
long my_get_time();
FILE* my_fopen(const char* prefix, const char* mod, SYSTEMTIME time);
void my_printf_osc(t_EAS_Event event, int length);
void my_printf_eas(t_EAS_Event event, int record_length);
void my_printf_a(SYSTEMTIME str_time, int amp_distrib[SIZE_OF_AMP_DISTRIB][16]);
void my_printf_t(SYSTEMTIME str_time, int time_distrib[SIZE_OF_TIME_DISTRIB][16]);
void my_printf_t_eas(SYSTEMTIME str_time, int time_distrib[SIZE_OF_TIME_EAS_DISTRIB][16]);

