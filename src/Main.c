#include "Additions.h"

int main(int argc, char *argv[])
{
    WaveDumpConfig_t   			WDcfg;
    WaveDumpRun_t      			WDrun;
	CAEN_DGTZ_BoardInfo_t       BoardInfo;
    CAEN_DGTZ_ErrorCode 		ret = CAEN_DGTZ_Success;
	CAEN_DGTZ_EventInfo_t		EventInfo;
    int  						handle = -1;
    uint32_t 					BufferSize, NumEvents;
    char 						*buffer = NULL;
    char 						*EventPtr = NULL;
    int 						MajorNumber;
    int 						nCycles= 0;
    CAEN_DGTZ_UINT16_EVENT_t    *Event16=NULL; /* generic event struct with 16 bit data (10, 12, 14 and 16 bit digitizers */
    int 						ReloadCfgStatus = 0x7FFFFFFF; // Init to the bigger positive number
	int 						baseline_levels[32];
	int 						event_number, new_day;
	SYSTEMTIME 					str_time;
	t_EAS_Event 				current_Event;
	uint64_t 					CurrentTime, PrevRateTime, ElapsedTime;
	int 						key = 0;
	int 						amp_distrib[SIZE_OF_AMP_DISTRIB][16];
	int 						time_distrib[SIZE_OF_TIME_DISTRIB][16];
    int 						time_eas_distrib[SIZE_OF_TIME_EAS_DISTRIB][16];
    int 						is_zero_line = 0;
    int 						current_level = 0;
	int 						max_of_pulse = 0;
    int 						start_time = 0;
    int 						time_of_max_of_pulse = 0;
    int 						dead_time = 0;
    int 						current_data = 0;
    int 						floating_baseline = 0;
	int 						i, ch, Nb=0, Ne=0;

	BoardInfo = prepare_device(&WDcfg, &WDrun, &handle, &buffer, &Event16);
	WDrun.Restart = 0;
	start_device(&WDcfg, &WDrun, handle, &BoardInfo, ReloadCfgStatus, &buffer, &Event16);
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
			Set_relative_Threshold(handle, &WDcfg, BoardInfo);
			printf("Acquisition started\n");
			CAEN_DGTZ_SWStartAcquisition(handle);
			WDrun.AcqRun = 1;
			key = 1;
		}
		CheckKeyboardCommands(handle, &WDrun, &WDcfg, BoardInfo);
		if (WDrun.Restart)
			start_device(&WDcfg, &WDrun, handle, &BoardInfo, ReloadCfgStatus, &buffer, &Event16);
		if (WDrun.AcqRun == 0)
			continue;
		/* Send a software trigger */
		if (WDrun.ContinuousTrigger)
			CAEN_DGTZ_SendSWtrigger(handle);
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
				;
			else if (ret != CAEN_DGTZ_Success)
				quit_program(handle, ERR_INTERRUPT, &buffer, &Event16);
		}
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
		if (ret)
			quit_program(handle, ERR_READOUT, &buffer, &Event16);
		NumEvents = 0;
		if (BufferSize != 0) {
			ret = CAEN_DGTZ_GetNumEvents(handle, buffer, BufferSize, &NumEvents);
			if (ret)
				quit_program(handle, ERR_READOUT, &buffer, &Event16);
		}
		else {
			uint32_t lstatus;
			ret = CAEN_DGTZ_ReadRegister(handle, CAEN_DGTZ_ACQ_STATUS_ADD, &lstatus);
			if (ret)
				printf("Warning: Failure reading reg:%x (%d)\n", CAEN_DGTZ_ACQ_STATUS_ADD, ret);
			else {
				if (lstatus & (0x1 << 19))
					quit_program(handle, ERR_OVERTEMP, &buffer, &Event16);
			}
		}
		/* Analyze data */
		int detnum = 0;
		for (int e = 0; e < (int)NumEvents; e++) {
			// Get one event from the readout buffer 
			printf("master = %d\n",current_Event.master);
			ret = CAEN_DGTZ_GetEventInfo(handle, buffer, BufferSize, e, &EventInfo, &EventPtr);
			if (ret)
				quit_program(handle, ERR_EVENT_BUILD, &buffer, &Event16);
			// decode the event 
			ret = CAEN_DGTZ_DecodeEvent(handle, EventPtr, (void**)& Event16);
			if (ret)
				quit_program(handle, ERR_EVENT_BUILD, &buffer, &Event16);
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
	quit_program(handle, ERR_NONE, &buffer, &Event16);
    return 0;
}