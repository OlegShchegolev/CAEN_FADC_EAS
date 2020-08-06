#define WaveDump_Release        "3.9.1"
#define WaveDump_Release_Date   "May 2019"
#define DBG_TIME

#include "Additions.h"

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

    int is_zero_line = 0;
    int current_level = 0;
	int max_of_pulse = 0;
    int start_time = 0;
    int time_of_max_of_pulse = 0;
    int dead_time = 0;
    int current_data = 0;
    int floating_baseline = 0;

    printf("\n");
    printf("**************************************************************\n");
    printf("   Edited 06.08.2020      Wave Dump %s\n", WaveDump_Release);
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

	current_Event.event_number = 1;
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
				memset(time_distrib, 0, sizeof(int) * 16 * SIZE_OF_TIME_DISTRIB);
                memset(time_eas_distrib, 0, sizeof(int) * 16 * SIZE_OF_TIME_EAS_DISTRIB);
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
			printf("master = %d\n",current_Event.master);
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
            printf("Zero point = %d\n", zero_point);
			for (ch = 0; ch < WDcfg.Nch; ch += 2) {
				memset(current_Event.oscillogram[ch/2], 0, sizeof(int) * WDcfg.RecordLength);
				int chmask = ch / 8;
				int n_base_points = (int)(WDcfg.RecordLength * (1 - 0.01 * WDcfg.PostTrigger) * 0.7);
				baseline_levels[ch] = 0;
				for (i = 0; i < n_base_points; i++)
					baseline_levels[ch] += Event16->DataChannel[ch][i];
				baseline_levels[ch] /= n_base_points;
				
                is_zero_line = 1;
                current_level = 0;
				max_of_pulse = 0;
                start_time = 0;
                time_of_max_of_pulse = 0;
                dead_time = 0;
                current_data = 0;

                for (i = 0; i < n_base_points; i++)
					current_Event.oscillogram[ch / 2][i] = Event16->DataChannel[ch][i] - baseline_levels[ch];

                for (i = n_base_points; i < zero_point + 100; i++)
				{
					current_data = Event16->DataChannel[ch][i] - baseline_levels[ch];
					current_Event.oscillogram[ch / 2][i] = current_data;
                    if (current_data > 20) 
                    {
                        if (current_data > current_Event.em_in_event[ch/2])
					    {
					        current_Event.em_in_event[ch/2] = current_data;
					        current_Event.time_max_amp[ch/2] = i;
					    }
                    }
                }
                if (current_Event.master > 0)
					printf("d#%d: amp = %d, tamp = %d; ", ch/2, current_Event.em_in_event[ch/2], current_Event.time_max_amp[ch/2]);
				for (i = zero_point + 100; i < zero_point + 30000 / 32; i++)
				{
					current_data = Event16->DataChannel[ch][i] - baseline_levels[ch];
					current_Event.oscillogram[ch / 2][i] = current_data;
				}
                for (i = zero_point + 30000/32; i < (int)Event16->ChSize[ch] - 20; i++)
				{
					current_Event.oscillogram[ch / 2][i] = Event16->DataChannel[ch][i] - baseline_levels[ch];
                    if (i < zero_point + 500000/32 && \
                                abs(Event16->DataChannel[ch][i-10] - Event16->DataChannel[ch][i+10]) < 5)
                    {
                        floating_baseline = 0;
                        for (int temp_n = -10; temp_n < 10; temp_n++)
                            floating_baseline += Event16->DataChannel[ch][i-temp_n];
                        floating_baseline /= 20;
                    }
                    else if (i >= zero_point + 500000/32)
                        floating_baseline = baseline_levels[ch];
                    current_data = Event16->DataChannel[ch][i] - floating_baseline;
					
                    if (is_zero_line == 1)
                    {
                        if (dead_time == 0)
                        {
                            if (current_data > 4)
                            {
                                start_time = i;
                                is_zero_line = 0;
                                max_of_pulse = current_data;
                            }
                        }
                        else
                            dead_time--;
                    }
                    else
                    {
                        if (current_data > max_of_pulse)
                        {
                            time_of_max_of_pulse = i - start_time;
                            max_of_pulse = current_data;
                        }
                        else if (current_data <= 3)
                        {
                            if (time_of_max_of_pulse > 5 && max_of_pulse > 10)
                            {
                                current_Event.n_in_event[ch/2]++;
                                time_eas_distrib[i*16*WDcfg.DecimationFactor/1000/100][ch/2]++;
                            }
                            time_distrib[time_of_max_of_pulse][ch/2]++;
                            amp_distrib[max_of_pulse][ch/2]++;
                            is_zero_line = 1;
                            dead_time = 100;
                            time_of_max_of_pulse = 0;
                            max_of_pulse = 0;
                        }
                    }
				}
                if (current_Event.em_in_event[ch/2] > 20)
                {
                    current_Event.chnumber++;
                    if (current_Event.em_in_event[ch/2] > 1900)
                        current_Event.record_flag = 1;
                    current_Event.esum += current_Event.em_in_event[ch/2];
                }	
			}
			printf("\n");
			current_Event.trig_time = EventInfo.TriggerTimeTag / 125;
			GetLocalTime(&current_Event.time);
			current_Event.time_ms = my_get_time();
			for (i = 0; i < 16; i++)
				current_Event.nnumber += current_Event.n_in_event[i];
            if (current_Event.nnumber > 5 || current_Event.esum > 4000)
                current_Event.record_flag = 1;
			if (current_Event.master == 0)
				current_Event.record_flag = 0;
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