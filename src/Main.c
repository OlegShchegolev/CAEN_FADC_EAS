#define WaveDump_Release        "3.9.1"
#define WaveDump_Release_Date   "May 2019"
#define DBG_TIME

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

typedef enum  {
    ERR_NONE= 0,
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

static long my_get_time()
{
	long time_ms;

	struct _timeb timebuffer;
	_ftime(&timebuffer);
	time_ms = (((long)timebuffer.time - (long)timebuffer.timezone * 60) % (3600 * 24)) * 1000 + \
																	(long)timebuffer.millitm;
	return time_ms;
}

static FILE* my_fopen(const char *prefix, const char *mod, SYSTEMTIME time)
{
	FILE* file;
	char file_name[40];

	sprintf(file_name, "%s%s%02d-%02d.%02d", "c:\\data\\", prefix, \
		time.wMonth, time.wDay, time.wYear - 2000);
	file = fopen(file_name, mod);
	return (file);
}

static void my_printf_osc(t_EAS_Event event, int length)
{
	char fname[30];
	sprintf(fname, "osc\\osc%d_", event.event_number);
	FILE* fosc = my_fopen(fname, "w", event.time);
	for (int j = 0; j < length; j++)
	{
		fprintf(fosc, "%d ", j);
		for (int ch = 0; ch < 16; ch++)
			fprintf(fosc, "%d ", event.oscillogram[ch][j]);
		fprintf(fosc, "\n");
	}
	fclose(fosc);
}

static void my_printf_eas(t_EAS_Event event, int record_length)
{
	FILE* eas_file;
	FILE* fosc ;

	if (event.nnumber >= 5 && event.master > 0)
		event.master |= 4;
	if (event.esum >= 4000 && event.master > 0)
		event.master |= 2;
	eas_file = my_fopen("eas\\eas", "a+", event.time);
	printf("%d %ld %02d:%02d:%02d %d %d %d %d  ", event.event_number, event.time_ms, \
		event.time.wHour, event.time.wMinute, event.time.wSecond, \
		event.chnumber, event.esum, event.nnumber, event.master);
	fprintf(eas_file, "%d %ld %02d:%02d:%02d %d %d %d %d ", event.event_number, event.time_ms, \
		event.time.wHour, event.time.wMinute, event.time.wSecond, \
		event.chnumber, event.esum, event.nnumber, event.master);
	for (int ch = 0; ch < 16; ch++) {
		printf("%d ", event.em_in_event[ch]);
		printf("%d ", event.n_in_event[ch]);
		fprintf(eas_file, "%d ", event.em_in_event[ch]);
		fprintf(eas_file, "%d ", event.n_in_event[ch]);
	}
	fprintf(eas_file, "\n");
	printf("\n");
	fclose(eas_file);
    if (event.record_flag == 1)
        my_printf_osc(event, record_length);
}

static void my_printf_a(SYSTEMTIME str_time, int amp_distrib[SIZE_OF_AMP_DISTRIB][16])
{
	FILE* a_file;

	a_file = my_fopen("amp\\a", "w", str_time);
	for (int raw = 0; raw < SIZE_OF_AMP_DISTRIB; raw++) {
		fprintf(a_file, "%d ", raw);
		for (int ch = 0; ch < 16; ch++) {
			fprintf(a_file, "%d ", amp_distrib[raw][ch]);
		}
		fprintf(a_file, "\n");
	}
	fclose(a_file);
}

static void my_printf_t(SYSTEMTIME str_time, int time_distrib[SIZE_OF_TIME_DISTRIB][16])
{
	FILE* t_file;

	t_file = my_fopen("time\\t", "w", str_time);
	for (int raw = 0; raw < SIZE_OF_TIME_DISTRIB; raw++) {
		fprintf(t_file, "%d ", raw);
		for (int ch = 0; ch < 16; ch++) {
			fprintf(t_file, "%d ", time_distrib[raw][ch]);
		}
		fprintf(t_file, "\n");
	}
	fclose(t_file);
}

static void my_printf_t_eas(SYSTEMTIME str_time, int time_distrib[SIZE_OF_TIME_EAS_DISTRIB][16])
{
	FILE* t_file;

	t_file = my_fopen("time_eas\\t", "w", str_time);
	for (int raw = 0; raw < SIZE_OF_TIME_EAS_DISTRIB; raw++) {
		fprintf(t_file, "%d ", raw);
		for (int ch = 0; ch < 16; ch++) {
			fprintf(t_file, "%d ", time_distrib[raw][ch]);
		}
		fprintf(t_file, "\n");
	}
	fclose(t_file);
}

/* ########################################################################### */
/* MAIN                                                                        */
/* ########################################################################### */
int main(int argc, char *argv[])
{
    WaveDumpConfig_t   WDcfg;
    WaveDumpRun_t      WDrun;
    CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_Success;
    int  handle = -1;
    ERROR_CODES ErrCode= ERR_NONE;
    int i, ch, Nb=0, Ne=0;
    uint32_t AllocatedSize, BufferSize, NumEvents;
    char *buffer = NULL;
    char *EventPtr = NULL;
    char ConfigFileName[100];
    int isVMEDevice= 0, MajorNumber;
    uint64_t CurrentTime, PrevRateTime, ElapsedTime;
    int nCycles= 0;
    CAEN_DGTZ_BoardInfo_t       BoardInfo;
    CAEN_DGTZ_EventInfo_t       EventInfo;

    CAEN_DGTZ_UINT16_EVENT_t    *Event16=NULL; /* generic event struct with 16 bit data (10, 12, 14 and 16 bit digitizers */

    CAEN_DGTZ_UINT8_EVENT_t     *Event8=NULL; /* generic event struct with 8 bit data (only for 8 bit digitizers) */ 
    WDPlot_t                    *PlotVar=NULL;
    FILE *f_ini;

    int ReloadCfgStatus = 0x7FFFFFFF; // Init to the bigger positive number

	int baseline_levels[32];

	int event_number, new_day;
	SYSTEMTIME str_time;

	t_EAS_Event current_Event;
	int key = 0;

	int amp_distrib[SIZE_OF_AMP_DISTRIB][16];
	int time_distrib[SIZE_OF_TIME_DISTRIB][16];
    int time_eas_distrib[SIZE_OF_TIME_EAS_DISTRIB][16];

    printf("\n");
    printf("**************************************************************\n");
    printf("                        Wave Dump %s\n", WaveDump_Release);
    printf("**************************************************************\n");

	/* *************************************************************************************** */
	/* Open and parse default configuration file                                                       */
	/* *************************************************************************************** */
	PrevRateTime = 0;
	new_day = 0;
	event_number = 0;
	
Start:
	memset(&WDrun, 0, sizeof(WDrun));
	memset(&WDcfg, 0, sizeof(WDcfg));

	if (argc > 1)//user entered custom filename
		strcpy(ConfigFileName, argv[1]);
	else 
		strcpy(ConfigFileName, DEFAULT_CONFIG_FILE);

	printf("Opening Configuration File %s\n", ConfigFileName);
	f_ini = fopen(ConfigFileName, "r");
	if (f_ini == NULL) {
		ErrCode = ERR_CONF_FILE_NOT_FOUND;
		goto QuitProgram;
	}
	ParseConfigFile(f_ini, &WDcfg);
	fclose(f_ini);

    /* *************************************************************************************** */
    /* Open the digitizer and read the board information                                       */
    /* *************************************************************************************** */
    isVMEDevice = WDcfg.BaseAddress ? 1 : 0;

    ret = CAEN_DGTZ_OpenDigitizer(WDcfg.LinkType, WDcfg.LinkNum, WDcfg.ConetNode, WDcfg.BaseAddress, &handle);
    if (ret) {
        ErrCode = ERR_DGZ_OPEN;
        goto QuitProgram;
    }

    ret = CAEN_DGTZ_GetInfo(handle, &BoardInfo);
    if (ret) {
        ErrCode = ERR_BOARD_INFO_READ;
        goto QuitProgram;
    }
    printf("Connected to CAEN Digitizer Model %s\n", BoardInfo.ModelName);
    printf("ROC FPGA Release is %s\n", BoardInfo.ROC_FirmwareRel);
    printf("AMC FPGA Release is %s\n", BoardInfo.AMC_FirmwareRel);

    // Check firmware rivision (DPP firmwares cannot be used with WaveDump */
    sscanf(BoardInfo.AMC_FirmwareRel, "%d", &MajorNumber);
    if (MajorNumber >= 128) {
        printf("This digitizer has a DPP firmware\n");
        ErrCode = ERR_INVALID_BOARD_TYPE;
        goto QuitProgram;
    }

	/* *************************************************************************************** */
	/* Check if the board needs a specific config file and parse it instead of the default one */
	/* *************************************************************************************** */


	strcpy(ConfigFileName, "WaveDumpConfig_X740.txt");
	printf("\nUsing configuration file %s.\n ", ConfigFileName);
	memset(&WDrun, 0, sizeof(WDrun));
	memset(&WDcfg, 0, sizeof(WDcfg));
	f_ini = fopen(ConfigFileName, "r");
	if (f_ini == NULL) {
		ErrCode = ERR_CONF_FILE_NOT_FOUND;
		goto QuitProgram;
	}
	ParseConfigFile(f_ini, &WDcfg);
	fclose(f_ini);

    // Get Number of Channels, Number of bits, Number of Groups of the board */
    ret = GetMoreBoardInfo(handle, BoardInfo, &WDcfg);
    if (ret) {
        ErrCode = ERR_INVALID_BOARD_TYPE;
        goto QuitProgram;
    }

	//set default DAC calibration coefficients
	for (i = 0; i < MAX_SET; i++) {
		WDcfg.DAC_Calib.cal[i] = 1;
		WDcfg.DAC_Calib.offset[i] = 0;
	}
	//load DAC calibration data (if present in flash)
    Load_DAC_Calibration_From_Flash(handle, &WDcfg, BoardInfo);

    // Perform calibration (if needed).
    if (WDcfg.StartupCalibration)
        calibrate(handle, &WDrun, BoardInfo);

Restart:
    // mask the channels not available for this model
    WDcfg.EnableMask &= (1<<(WDcfg.Nch/8))-1;
    // Set plot mask
    WDrun.ChannelPlotMask = (WDcfg.FastTriggerEnabled == 0) ? 0xFF: 0x1FF;
	WDrun.PlotYscaleLog = 0;
    /* *************************************************************************************** */
    /* program the digitizer                                                                   */
    /* *************************************************************************************** */
    ret = ProgramDigitizer(handle, WDcfg, BoardInfo);
    if (ret) {
        ErrCode = ERR_DGZ_PROGRAM;
        goto QuitProgram;
    }

    // Select the next enabled group for plotting
    if ((WDcfg.EnableMask) && (BoardInfo.FamilyCode == CAEN_DGTZ_XX740_FAMILY_CODE))
        if( ((WDcfg.EnableMask>>WDrun.GroupPlotIndex)&0x1)==0 )
            GoToNextEnabledGroup(&WDrun, &WDcfg);

    // Read again the board infos, just in case some of them were changed by the programming
    // (like, for example, the TSample and the number of channels if DES mode is changed)
    if(ReloadCfgStatus > 0) {
        ret = CAEN_DGTZ_GetInfo(handle, &BoardInfo);
        if (ret) {
            ErrCode = ERR_BOARD_INFO_READ;
            goto QuitProgram;
        }
        ret = GetMoreBoardInfo(handle,BoardInfo, &WDcfg);
        if (ret) {
            ErrCode = ERR_INVALID_BOARD_TYPE;
            goto QuitProgram;
        }
    }

    // Allocate memory for the event data and readout buffer
    ret = CAEN_DGTZ_AllocateEvent(handle, (void**)&Event16);
    if (ret != CAEN_DGTZ_Success) {
        ErrCode = ERR_MALLOC;
        goto QuitProgram;
    }
    ret = CAEN_DGTZ_MallocReadoutBuffer(handle, &buffer,&AllocatedSize); /* WARNING: This malloc must be done after the digitizer programming */
    if (ret) {
        ErrCode = ERR_MALLOC;
        goto QuitProgram;
    }

	if (WDrun.Restart && WDrun.AcqRun) 
	{
#ifdef _WIN32
		Sleep(300);
#else
		usleep(300000);
#endif
		Set_relative_Threshold(handle, &WDcfg, BoardInfo);

		CAEN_DGTZ_SWStartAcquisition(handle);
	}
    else
        printf("[s] start/stop the acquisition, [q] quit, [SPACE] help\n");

    //---------------------------------

	WDrun.Restart = 0;
	
	/* *************************************************************************************** */
    /* Readout Loop                                                                            */
    /* *************************************************************************************** */
	
	memset(amp_distrib, 0, sizeof(int) * 16 * SIZE_OF_AMP_DISTRIB);
	memset(time_distrib, 0, sizeof(int) * 16 * SIZE_OF_TIME_DISTRIB);
    memset(time_eas_distrib, 0, sizeof(int) * 16 * SIZE_OF_TIME_EAS_DISTRIB);

	for (i = 0; i < 16; i++)
		current_Event.oscillogram[i] = (int *)malloc(sizeof(int) * WDcfg.RecordLength);

	while (!WDrun.Quit) {
		//Check for keyboard commands (key pressed)
		if (WDrun.AcqRun == 0 && key == 0) {
			if (BoardInfo.FamilyCode != CAEN_DGTZ_XX742_FAMILY_CODE)//XX742 not considered
				Set_relative_Threshold(handle, &WDcfg, BoardInfo);

			if (BoardInfo.FamilyCode == CAEN_DGTZ_XX730_FAMILY_CODE || BoardInfo.FamilyCode == CAEN_DGTZ_XX725_FAMILY_CODE)
				WDrun.GroupPlotSwitch = 0;

			printf("Acquisition started\n");

			CAEN_DGTZ_SWStartAcquisition(handle);

			WDrun.AcqRun = 1;
			key = 1;
		}
		CheckKeyboardCommands(handle, &WDrun, &WDcfg, BoardInfo);
		if (WDrun.Restart) {
			CAEN_DGTZ_SWStopAcquisition(handle);
			CAEN_DGTZ_FreeReadoutBuffer(&buffer);
			ClosePlotter();
			PlotVar = NULL;
			CAEN_DGTZ_FreeEvent(handle, (void**)& Event16);
			f_ini = fopen(ConfigFileName, "r");
			ReloadCfgStatus = ParseConfigFile(f_ini, &WDcfg);
			fclose(f_ini);
			goto Restart;
		}
		if (WDrun.AcqRun == 0)
			continue;

		/* Send a software trigger */
		if (WDrun.ContinuousTrigger) {
			CAEN_DGTZ_SendSWtrigger(handle);
		}

		/* Wait for interrupt (if enabled) */
		if (WDcfg.InterruptNumEvents > 0) {
			int32_t boardId;
			int VMEHandle = -1;
			int InterruptMask = (1 << VME_INTERRUPT_LEVEL);

			BufferSize = 0;
			NumEvents = 0;
			// Interrupt handling
			ret = CAEN_DGTZ_IRQWait(handle, INTERRUPT_TIMEOUT);
			if (ret == CAEN_DGTZ_Timeout)  // No active interrupt requests
				goto InterruptTimeout;
			if (ret != CAEN_DGTZ_Success) {
				ErrCode = ERR_INTERRUPT;
				goto QuitProgram;
			}
		}
	InterruptTimeout:
		/* Calculate throughput and trigger rate (every second) */
		Nb += BufferSize;
		Ne += NumEvents;
		CurrentTime = get_time();
		ElapsedTime = CurrentTime - PrevRateTime;

		nCycles++;

		GetLocalTime(&str_time);
		if (new_day != 0 && new_day != str_time.wDay)
			current_Event.event_number = 0;

		if (PrevRateTime == 0)
			PrevRateTime = CurrentTime;
		else if (ElapsedTime > 1 * 60 * 1000) {
			nCycles = 0;
			Nb = 0;
			Ne = 0;
			PrevRateTime = CurrentTime;

			if (new_day == 0)
				new_day = str_time.wDay;
			else if (new_day != str_time.wDay)
			{
				new_day = str_time.wDay;
				memset(amp_distrib, 0, sizeof(int) * 16 * SIZE_OF_AMP_DISTRIB);
				memset(time_distrib, 0, sizeof(int) * 16 * SIZE_OF_TIME_DISTRIB;
                memset(time_eas_distrib, 0, sizeof(int) * 16 * SIZE_OF_TIME_EAS_DISTRIB;
            }
			my_printf_a(str_time, amp_distrib);
			my_printf_t(str_time, time_distrib);
            my_printf_t_eas(str_time, time_eas_distrib);
			CAEN_DGTZ_SendSWtrigger(handle);
			current_Event.master = 0;
		}
		else
			current_Event.master = 1;
		/* Read data from the board */
		ret = CAEN_DGTZ_ReadData(handle, CAEN_DGTZ_SLAVE_TERMINATED_READOUT_MBLT, buffer, &BufferSize);
		if (ret) {
			ErrCode = ERR_READOUT;
			goto QuitProgram;
		}
		NumEvents = 0;
		if (BufferSize != 0) {
			ret = CAEN_DGTZ_GetNumEvents(handle, buffer, BufferSize, &NumEvents);
			if (ret) {
				ErrCode = ERR_READOUT;
				goto QuitProgram;
			}
		}
		else {
			uint32_t lstatus;
			ret = CAEN_DGTZ_ReadRegister(handle, CAEN_DGTZ_ACQ_STATUS_ADD, &lstatus);
			if (ret) {
				printf("Warning: Failure reading reg:%x (%d)\n", CAEN_DGTZ_ACQ_STATUS_ADD, ret);
			}
			else {
				if (lstatus & (0x1 << 19)) {
					ErrCode = ERR_OVERTEMP;
					goto QuitProgram;
				}
			}
		}
	
		
		/* Analyze data */
		int detnum = 0;

		for (int e = 0; e < (int)NumEvents; e++) {
			// Get one event from the readout buffer 
			
			ret = CAEN_DGTZ_GetEventInfo(handle, buffer, BufferSize, e, &EventInfo, &EventPtr);

			if (ret) {
				ErrCode = ERR_EVENT_BUILD;
				goto QuitProgram;
			}
			// decode the event 
			ret = CAEN_DGTZ_DecodeEvent(handle, EventPtr, (void**)& Event16);
			if (ret) {
				ErrCode = ERR_EVENT_BUILD;
				goto QuitProgram;
			}
			current_Event.chnumber = 0;

			memset(current_Event.em_in_event, 0, sizeof(int) * 16);
			memset(current_Event.time_max_amp, 0, sizeof(int) * 16);
			memset(current_Event.em_in_event, 0, sizeof(int) * 16);
			memset(current_Event.n_in_event, 0, sizeof(int) * 16);
			current_Event.nnumber = 0;
			current_Event.esum = 0;
            current_Event.record_flag = 0;

			int zero_point = (int)(WDcfg.RecordLength * (1 - 0.01 * WDcfg.PostTrigger));
			for (ch = 0; ch < WDcfg.Nch; ch += 2) {
				memset(current_Event.oscillogram[ch/2], 0, sizeof(int) * WDcfg.RecordLength);
				int chmask = ch / 8;
				int n_base_points = (int)(WDcfg.RecordLength * (1 - 0.01 * WDcfg.PostTrigger) * 0.7);
				baseline_levels[ch] = 0;
				for (i = 0; i < n_base_points; i++)
					baseline_levels[ch] += Event16->DataChannel[ch][i];
				baseline_levels[ch] /= n_base_points;
				int zero_line = 0;
				int max_of_n_pulse = 0;
                int time_of_max_of_n_pulse = 0;

				for (i = 0; i < (int)Event16->ChSize[ch]; i++)
				{
					int x = 0;
					current_Event.oscillogram[ch/2][i] = Event16->DataChannel[ch][i] - baseline_levels[ch];
					if (current_Event.oscillogram[ch/2][i] > current_Event.em_in_event[ch/2] && 
						i < zero_point + 1000)
					{
						current_Event.em_in_event[ch/2] = current_Event.oscillogram[ch/2][i];
						current_Event.time_max_amp[ch/2] = i;
					}
					else if (i > zero_point + 1000 && current_Event.oscillogram[ch/2][i] > 3 && zero_line == 0)
					{
						current_Event.n_in_event[ch/2]++;
                        max_of_n_pulse = current_Event.oscillogram[ch/2][i];
                        time_eas_distrib[i*16*WDcfg.DecimationFactor/1000/100][ch/2]++;
                        time_of_max_of_n_pulse = 0;
						zero_line = 1;
					}
                    else if (i > zero_point + 1000 && zero_line == 1)
                    {
                        if (current_Event.oscillogram[ch/2][i] > max_of_n_pulse)
                        {
                            max_of_n_pulse = current_Event.oscillogram[ch/2][i];
                            time_of_max_of_n_pulse++;
                        }
                        else
                        {
                            zero_line = 0;
                            time_distrib[time_of_max_of_n_pulse][ch/2]++;
                            amp_distrib[max_of_n_pulse][ch/2]++;
                            time_of_max_of_n_pulse = 0;
                            max_of_n_pulse = 0;
                        }
                    }
				}
				if (current_Event.em_in_event[ch/2] > -3 && current_Event.em_in_event[ch/2] < 3)
					current_Event.em_in_event[ch/2] = 0;
				else if (current_Event.em_in_event[ch/2] > 10)
				{
                    if (current_Event.em_in_event > 1900)
                        current_Event.record_flag = 1;
					current_Event.chnumber++;
					printf("%d %d   ", ch, current_Event.time_max_amp[ch/2]);
				}
				current_Event.esum += current_Event.em_in_event[ch/2];
			}
			printf("\n");
			current_Event.trig_time = EventInfo.TriggerTimeTag / 125;
			GetLocalTime(&current_Event.time);
			current_Event.time_ms = my_get_time();
			for (i = 0; i < 16; i++)
				current_Event.nnumber += current_Event.n_in_event[i];
			printf("\n");
            if (current_Event.nnumber > 5 || current_Event.esum > 4000)
                current_Event.record_flag = 1;
			my_printf_eas(current_Event, WDcfg.RecordLength);
			current_Event.event_number++;
		}
	}
    ErrCode = ERR_NONE;
QuitProgram:
    if (ErrCode) {
        printf("\a%s\n", ErrMsg[ErrCode]);
#ifdef WIN32
        printf("Press a key to quit\n");
        getch();
#endif
    }

    /* stop the acquisition */
    CAEN_DGTZ_SWStopAcquisition(handle);

    /* close the device and free the buffers */
    if(Event8)
        CAEN_DGTZ_FreeEvent(handle, (void**)&Event8);
    if(Event16)
        CAEN_DGTZ_FreeEvent(handle, (void**)&Event16);
    CAEN_DGTZ_FreeReadoutBuffer(&buffer);
    CAEN_DGTZ_CloseDigitizer(handle);
    return 0;
}