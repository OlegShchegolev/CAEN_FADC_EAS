#include "Additions.h"

long get_time()
{
	long time_ms;
	struct _timeb timebuffer;
	_ftime(&timebuffer);
	time_ms = (long)timebuffer.time * 1000 + (long)timebuffer.millitm;
	return time_ms;
}

long my_get_time()
{
	long time_ms;

	struct _timeb timebuffer;
	_ftime(&timebuffer);
	time_ms = (((long)timebuffer.time - (long)timebuffer.timezone * 60) % (3600 * 24)) * 1000 + \
		(long)timebuffer.millitm;
	return time_ms;
}

FILE* my_fopen(const char* prefix, const char* mod, SYSTEMTIME time)
{
	FILE* file;
	char file_name[40];

	sprintf(file_name, "%s%s%02d-%02d.%02d", "c:\\data\\", prefix, \
		time.wMonth, time.wDay, time.wYear - 2000);
	file = fopen(file_name, mod);
	return (file);
}

void my_printf_osc(t_EAS_Event event, int length)
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

void my_printf_eas(t_EAS_Event event, int record_length)
{
	FILE* eas_file;
	FILE* fosc;

	if (event.nnumber >= 10 && event.master > 0)
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

void my_printf_a(SYSTEMTIME str_time, int amp_distrib[SIZE_OF_AMP_DISTRIB][16])
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

void my_printf_t(SYSTEMTIME str_time, int time_distrib[SIZE_OF_TIME_DISTRIB][16])
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

void my_printf_t_eas(SYSTEMTIME str_time, int time_distrib[SIZE_OF_TIME_EAS_DISTRIB][16])
{
	FILE* t_file;

	t_file = my_fopen("time_eas\\t_eas", "w", str_time);
	for (int raw = 0; raw < SIZE_OF_TIME_EAS_DISTRIB; raw++) {
		fprintf(t_file, "%d ", raw);
		for (int ch = 0; ch < 16; ch++) {
			fprintf(t_file, "%d ", time_distrib[raw][ch]);
		}
		fprintf(t_file, "\n");
	}
	fclose(t_file);
}

void quit_program(int handle, ERROR_CODES ErrCode, char **buffer, CAEN_DGTZ_UINT16_EVENT_t **Event16)
{
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
    CAEN_DGTZ_FreeEvent(handle, (void**)Event16);
    CAEN_DGTZ_FreeReadoutBuffer(buffer);
    CAEN_DGTZ_CloseDigitizer(handle);
}

CAEN_DGTZ_BoardInfo_t prepare_device(WaveDumpConfig_t *WDcfg, WaveDumpRun_t *WDrun, int *handle, \
												char **buffer, CAEN_DGTZ_UINT16_EVENT_t **Event16)
{
    CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_Success;
    int isVMEDevice= 0, MajorNumber;
    CAEN_DGTZ_BoardInfo_t       BoardInfo;
    FILE *f_ini;

    printf("\n");
    printf("**************************************************************\n");
    printf("   Edited 06.08.2020      Wave Dump %s\n", WaveDump_Release);
    printf("**************************************************************\n");
	/* *************************************************************************************** */
	/* Open and parse default configuration file                                               */
	/* *************************************************************************************** */
	memset(WDrun, 0, sizeof(*WDrun));
	memset(WDcfg, 0, sizeof(*WDcfg));
	printf("Opening Configuration File %s\n", DEFAULT_CONFIG_FILE);
	f_ini = fopen(DEFAULT_CONFIG_FILE, "r");
	if (f_ini == NULL)
		quit_program(*handle, ERR_CONF_FILE_NOT_FOUND, buffer, Event16);
	ParseConfigFile(f_ini, WDcfg);
	fclose(f_ini);
    /* *************************************************************************************** */
    /* Open the digitizer and read the board information                                       */
    /* *************************************************************************************** */
    isVMEDevice = WDcfg->BaseAddress ? 1 : 0;
    ret = CAEN_DGTZ_OpenDigitizer(WDcfg->LinkType, WDcfg->LinkNum, WDcfg->ConetNode, WDcfg->BaseAddress, handle);
    if (ret)
        quit_program(handle, ERR_DGZ_OPEN, buffer, Event16);
    ret = CAEN_DGTZ_GetInfo(*handle, &BoardInfo);
    if (ret)
        quit_program(*handle, ERR_BOARD_INFO_READ, buffer, Event16);
    printf("Connected to CAEN Digitizer Model %s\n", BoardInfo.ModelName);
    printf("ROC FPGA Release is %s\n", BoardInfo.ROC_FirmwareRel);
    printf("AMC FPGA Release is %s\n", BoardInfo.AMC_FirmwareRel);
    /* Check firmware rivision (DPP firmwares cannot be used with WaveDump */
    sscanf(BoardInfo.AMC_FirmwareRel, "%d", &MajorNumber);
    if (MajorNumber >= 128) {
        printf("This digitizer has a DPP firmware\n");
        quit_program(*handle, ERR_INVALID_BOARD_TYPE, buffer, Event16);
	}
	/* *************************************************************************************** */
	/* Check if the board needs a specific config file and parse it instead of the default one */
	/* *************************************************************************************** */
	printf("\nUsing configuration file %s.\n ", SPECIAL_CONFIG_FILE);
	memset(WDrun, 0, sizeof(*WDrun));
	memset(WDcfg, 0, sizeof(*WDcfg));
	f_ini = fopen(SPECIAL_CONFIG_FILE, "r");
	if (f_ini == NULL)
		quit_program(*handle, ERR_CONF_FILE_NOT_FOUND, buffer, Event16);
	ParseConfigFile(f_ini, WDcfg);
	fclose(f_ini);
    /* Get Number of Channels, Number of bits, Number of Groups of the board */
    ret = GetMoreBoardInfo(*handle, BoardInfo, WDcfg);
    if (ret)
        quit_program(*handle, ERR_INVALID_BOARD_TYPE, buffer, Event16);
	/* set default DAC calibration coefficients */
	for (int i = 0; i < MAX_SET; i++) {
		WDcfg->DAC_Calib.cal[i] = 1;
		WDcfg->DAC_Calib.offset[i] = 0;
	}
	/* load DAC calibration data (if present in flash) */
    Load_DAC_Calibration_From_Flash(*handle, WDcfg, BoardInfo);
    /* Perform calibration (if needed) */
    if (WDcfg->StartupCalibration)
        calibrate(*handle, WDrun, BoardInfo);
	return (BoardInfo);
}

void start_device(WaveDumpConfig_t *WDcfg, WaveDumpRun_t *WDrun, int handle, CAEN_DGTZ_BoardInfo_t *BoardInfo, \
								int ReloadCfgStatus, char **buffer, CAEN_DGTZ_UINT16_EVENT_t **Event16)
{
	uint32_t AllocatedSize;
	CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_Success;
	FILE *f_ini;

	if (WDrun->Restart) {
			CAEN_DGTZ_SWStopAcquisition(handle);
			CAEN_DGTZ_FreeReadoutBuffer(buffer);
			CAEN_DGTZ_FreeEvent(handle, (void**)Event16);
			f_ini = fopen(SPECIAL_CONFIG_FILE, "r");
			ReloadCfgStatus = ParseConfigFile(f_ini, WDcfg);
			fclose(f_ini);
	}
	/* mask the channels not available for this model */
    WDcfg->EnableMask &= (1<<(WDcfg->Nch/8))-1;
    /* *************************************************************************************** */
    /* program the digitizer                                                                   */
    /* *************************************************************************************** */
    ret = ProgramDigitizer(handle, *WDcfg, *BoardInfo);
    if (ret)
        quit_program(handle, ERR_DGZ_PROGRAM, buffer, Event16);
    /* Read again the board infos, just in case some of them were changed by the programming
       (like, for example, the TSample and the number of channels if DES mode is changed) */
    if(ReloadCfgStatus > 0) {
        ret = CAEN_DGTZ_GetInfo(handle, BoardInfo);
        if (ret)
            quit_program(handle, ERR_BOARD_INFO_READ, buffer, Event16);
        ret = GetMoreBoardInfo(handle, *BoardInfo, WDcfg);
        if (ret)
			quit_program(handle, ERR_INVALID_BOARD_TYPE, buffer, Event16);
    }
    /* Allocate memory for the event data and readout buffer */
    ret = CAEN_DGTZ_AllocateEvent(handle, (void**)Event16);
    if (ret != CAEN_DGTZ_Success)
        quit_program(handle, ERR_MALLOC, buffer, Event16);
	/* WARNING: This malloc must be done after the digitizer programming */
    ret = CAEN_DGTZ_MallocReadoutBuffer(handle, buffer, &AllocatedSize);
    if (ret)
        quit_program(handle, ERR_MALLOC, buffer, Event16);
	if (WDrun->Restart && WDrun->AcqRun) 
	{
		#ifdef _WIN32
				Sleep(300);
		#else
				usleep(300000);
		#endif
				Set_relative_Threshold(handle, WDcfg, *BoardInfo);

		CAEN_DGTZ_SWStartAcquisition(handle);
	}
    else
        printf("[s] start/stop the acquisition, [q] quit, [SPACE] help\n");
}