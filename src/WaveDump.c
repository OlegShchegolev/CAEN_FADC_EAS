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

extern int dc_file[MAX_CH];
extern int thr_file[MAX_CH];
int cal_ok[MAX_CH] = { 0 };

/* Error messages */
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


#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

static CAEN_DGTZ_IRQMode_t INTERRUPT_MODE = CAEN_DGTZ_IRQ_MODE_ROAK;

/* ###########################################################################
*  Functions
*  ########################################################################### */
/*! \fn      static long get_time()
*   \brief   Get time in milliseconds
*
*   \return  time in msec
*/
static long get_time()
{
    long time_ms;
    struct _timeb timebuffer;
    _ftime( &timebuffer );
    time_ms = (long)timebuffer.time * 1000 + (long)timebuffer.millitm;
    return time_ms;
}

/*! \fn      int GetMoreBoardNumChannels(CAEN_DGTZ_BoardInfo_t BoardInfo,  WaveDumpConfig_t *WDcfg)
*   \brief   calculate num of channels, num of bit and sampl period according to the board type
*
*   \param   BoardInfo   Board Type
*   \param   WDcfg       pointer to the config. struct
*   \return  0 = Success; -1 = unknown board type
*/
int GetMoreBoardInfo(int handle, CAEN_DGTZ_BoardInfo_t BoardInfo, WaveDumpConfig_t *WDcfg)
{
    int ret;
    switch(BoardInfo.FamilyCode) {
        CAEN_DGTZ_DRS4Frequency_t freq;

    case CAEN_DGTZ_XX724_FAMILY_CODE:
    case CAEN_DGTZ_XX781_FAMILY_CODE:
    case CAEN_DGTZ_XX780_FAMILY_CODE: WDcfg->Nbit = 14; WDcfg->Ts = 10.0; break;
    case CAEN_DGTZ_XX720_FAMILY_CODE: WDcfg->Nbit = 12; WDcfg->Ts = 4.0;  break;
    case CAEN_DGTZ_XX721_FAMILY_CODE: WDcfg->Nbit =  8; WDcfg->Ts = 2.0;  break;
    case CAEN_DGTZ_XX731_FAMILY_CODE: WDcfg->Nbit =  8; WDcfg->Ts = 2.0;  break;
    case CAEN_DGTZ_XX751_FAMILY_CODE: WDcfg->Nbit = 10; WDcfg->Ts = 1.0;  break;
    case CAEN_DGTZ_XX761_FAMILY_CODE: WDcfg->Nbit = 10; WDcfg->Ts = 0.25;  break;
    case CAEN_DGTZ_XX740_FAMILY_CODE: WDcfg->Nbit = 12; WDcfg->Ts = 16.0; break;
    case CAEN_DGTZ_XX725_FAMILY_CODE: WDcfg->Nbit = 14; WDcfg->Ts = 4.0; break;
    case CAEN_DGTZ_XX730_FAMILY_CODE: WDcfg->Nbit = 14; WDcfg->Ts = 2.0; break;
    case CAEN_DGTZ_XX742_FAMILY_CODE: 
        WDcfg->Nbit = 12; 
        if ((ret = CAEN_DGTZ_GetDRS4SamplingFrequency(handle, &freq)) != CAEN_DGTZ_Success) return CAEN_DGTZ_CommError;
        switch (freq) {
        case CAEN_DGTZ_DRS4_1GHz:
            WDcfg->Ts = 1.0;
            break;
        case CAEN_DGTZ_DRS4_2_5GHz:
            WDcfg->Ts = (float)0.4;
            break;
        case CAEN_DGTZ_DRS4_5GHz:
            WDcfg->Ts = (float)0.2;
            break;
		case CAEN_DGTZ_DRS4_750MHz:
            WDcfg->Ts = (float)(1.0/750.0)*1000.0;
            break;
        }
        switch(BoardInfo.FormFactor) {
        case CAEN_DGTZ_VME64_FORM_FACTOR:
        case CAEN_DGTZ_VME64X_FORM_FACTOR:
            WDcfg->MaxGroupNumber = 4;
            break;
        case CAEN_DGTZ_DESKTOP_FORM_FACTOR:
        case CAEN_DGTZ_NIM_FORM_FACTOR:
        default:
            WDcfg->MaxGroupNumber = 2;
            break;
        }
        break;
    default: return -1;
    }
    if (((BoardInfo.FamilyCode == CAEN_DGTZ_XX751_FAMILY_CODE) ||
        (BoardInfo.FamilyCode == CAEN_DGTZ_XX731_FAMILY_CODE) ) && WDcfg->DesMode)
        WDcfg->Ts /= 2;

    switch(BoardInfo.FamilyCode) {
    case CAEN_DGTZ_XX724_FAMILY_CODE:
    case CAEN_DGTZ_XX781_FAMILY_CODE:
    case CAEN_DGTZ_XX780_FAMILY_CODE:
    case CAEN_DGTZ_XX720_FAMILY_CODE:
    case CAEN_DGTZ_XX721_FAMILY_CODE:
    case CAEN_DGTZ_XX751_FAMILY_CODE:
    case CAEN_DGTZ_XX761_FAMILY_CODE:
    case CAEN_DGTZ_XX731_FAMILY_CODE:
        switch(BoardInfo.FormFactor) {
        case CAEN_DGTZ_VME64_FORM_FACTOR:
        case CAEN_DGTZ_VME64X_FORM_FACTOR:
            WDcfg->Nch = 8;
            break;
        case CAEN_DGTZ_DESKTOP_FORM_FACTOR:
        case CAEN_DGTZ_NIM_FORM_FACTOR:
            WDcfg->Nch = 4;
            break;
        }
        break;
    case CAEN_DGTZ_XX725_FAMILY_CODE:
    case CAEN_DGTZ_XX730_FAMILY_CODE:
        switch(BoardInfo.FormFactor) {
        case CAEN_DGTZ_VME64_FORM_FACTOR:
        case CAEN_DGTZ_VME64X_FORM_FACTOR:
            WDcfg->Nch = 16;
            break;
        case CAEN_DGTZ_DESKTOP_FORM_FACTOR:
        case CAEN_DGTZ_NIM_FORM_FACTOR:
            WDcfg->Nch = 8;
            break;
        }
        break;
    case CAEN_DGTZ_XX740_FAMILY_CODE:
        switch( BoardInfo.FormFactor) {
        case CAEN_DGTZ_VME64_FORM_FACTOR:
        case CAEN_DGTZ_VME64X_FORM_FACTOR:
            WDcfg->Nch = 64;
            break;
        case CAEN_DGTZ_DESKTOP_FORM_FACTOR:
        case CAEN_DGTZ_NIM_FORM_FACTOR:
            WDcfg->Nch = 32;
            break;
        }
        break;
    case CAEN_DGTZ_XX742_FAMILY_CODE:
        switch( BoardInfo.FormFactor) {
        case CAEN_DGTZ_VME64_FORM_FACTOR:
        case CAEN_DGTZ_VME64X_FORM_FACTOR:
            WDcfg->Nch = 36;
            break;
        case CAEN_DGTZ_DESKTOP_FORM_FACTOR:
        case CAEN_DGTZ_NIM_FORM_FACTOR:
            WDcfg->Nch = 16;
            break;
        }
        break;
    default:
        return -1;
    }
    return 0;
}

/*! \fn      int WriteRegisterBitmask(int32_t handle, uint32_t address, uint32_t data, uint32_t mask)
*   \brief   writes 'data' on register at 'address' using 'mask' as bitmask
*
*   \param   handle :   Digitizer handle
*   \param   address:   Address of the Register to write
*   \param   data   :   Data to Write on the Register
*   \param   mask   :   Bitmask to use for data masking
*   \return  0 = Success; negative numbers are error codes
*/
int WriteRegisterBitmask(int32_t handle, uint32_t address, uint32_t data, uint32_t mask) {
    int32_t ret = CAEN_DGTZ_Success;
    uint32_t d32 = 0xFFFFFFFF;

    ret = CAEN_DGTZ_ReadRegister(handle, address, &d32);
    if(ret != CAEN_DGTZ_Success)
        return ret;

    data &= mask;
    d32 &= ~mask;
    d32 |= data;
    ret = CAEN_DGTZ_WriteRegister(handle, address, d32);
    return ret;
}

/*! \fn      int ProgramDigitizer(int handle, WaveDumpConfig_t WDcfg)
*   \brief   configure the digitizer according to the parameters read from
*            the cofiguration file and saved in the WDcfg data structure
*
*   \param   handle   Digitizer handle
*   \param   WDcfg:   WaveDumpConfig data structure
*   \return  0 = Success; negative numbers are error codes
*/
int ProgramDigitizer(int handle, WaveDumpConfig_t WDcfg, CAEN_DGTZ_BoardInfo_t BoardInfo)
{
    int i, j, ret = 0;

    /* reset the digitizer */
    ret |= CAEN_DGTZ_Reset(handle);
    if (ret != 0) {
        printf("Error: Unable to reset digitizer.\nPlease reset digitizer manually then restart the program\n");
        return -1;
    }

    // Set the waveform test bit for debugging
    if (WDcfg.TestPattern)
        ret |= CAEN_DGTZ_WriteRegister(handle, CAEN_DGTZ_BROAD_CH_CONFIGBIT_SET_ADD, 1<<3);
    // custom setting for X742 boards
    if (BoardInfo.FamilyCode == CAEN_DGTZ_XX742_FAMILY_CODE) {
        ret |= CAEN_DGTZ_SetFastTriggerDigitizing(handle,WDcfg.FastTriggerEnabled);
        ret |= CAEN_DGTZ_SetFastTriggerMode(handle,WDcfg.FastTriggerMode);
    }
    if ((BoardInfo.FamilyCode == CAEN_DGTZ_XX751_FAMILY_CODE) || (BoardInfo.FamilyCode == CAEN_DGTZ_XX731_FAMILY_CODE)) {
        ret |= CAEN_DGTZ_SetDESMode(handle, WDcfg.DesMode);
    }
    ret |= CAEN_DGTZ_SetRecordLength(handle, WDcfg.RecordLength);
    ret |= CAEN_DGTZ_GetRecordLength(handle, &WDcfg.RecordLength);

    if (BoardInfo.FamilyCode == CAEN_DGTZ_XX740_FAMILY_CODE || BoardInfo.FamilyCode == CAEN_DGTZ_XX724_FAMILY_CODE) {
        ret |= CAEN_DGTZ_SetDecimationFactor(handle, WDcfg.DecimationFactor);
    }

    ret |= CAEN_DGTZ_SetPostTriggerSize(handle, WDcfg.PostTrigger);
    if(BoardInfo.FamilyCode != CAEN_DGTZ_XX742_FAMILY_CODE) {
        uint32_t pt;
        ret |= CAEN_DGTZ_GetPostTriggerSize(handle, &pt);
        WDcfg.PostTrigger = pt;
    }
    ret |= CAEN_DGTZ_SetIOLevel(handle, WDcfg.FPIOtype);
    if( WDcfg.InterruptNumEvents > 0) {
        // Interrupt handling
        if( ret |= CAEN_DGTZ_SetInterruptConfig( handle, CAEN_DGTZ_ENABLE,
            VME_INTERRUPT_LEVEL, VME_INTERRUPT_STATUS_ID,
            (uint16_t)WDcfg.InterruptNumEvents, INTERRUPT_MODE)!= CAEN_DGTZ_Success) {
                printf( "\nError configuring interrupts. Interrupts disabled\n\n");
                WDcfg.InterruptNumEvents = 0;
        }
    }
	
    ret |= CAEN_DGTZ_SetMaxNumEventsBLT(handle, WDcfg.NumEvents);
    ret |= CAEN_DGTZ_SetAcquisitionMode(handle, CAEN_DGTZ_SW_CONTROLLED);
    ret |= CAEN_DGTZ_SetExtTriggerInputMode(handle, WDcfg.ExtTriggerMode);

    if ((BoardInfo.FamilyCode == CAEN_DGTZ_XX740_FAMILY_CODE) || (BoardInfo.FamilyCode == CAEN_DGTZ_XX742_FAMILY_CODE)){
        ret |= CAEN_DGTZ_SetGroupEnableMask(handle, WDcfg.EnableMask);
        for(i=0; i<(WDcfg.Nch/8); i++) {
            if (WDcfg.EnableMask & (1<<i)) {
                if (BoardInfo.FamilyCode == CAEN_DGTZ_XX742_FAMILY_CODE) {
                    for(j=0; j<8; j++) {
                        if (WDcfg.DCoffsetGrpCh[i][j] != -1)
                            ret |= CAEN_DGTZ_SetChannelDCOffset(handle,(i*8)+j, WDcfg.DCoffsetGrpCh[i][j]);
						else
                            ret |= CAEN_DGTZ_SetChannelDCOffset(handle, (i * 8) + j, WDcfg.DCoffset[i]);

                    }
                }
                else {
                    if(WDcfg.Version_used[i] == 1)
						ret |= Set_calibrated_DCO(handle, i, &WDcfg, BoardInfo);
					else
						ret |= CAEN_DGTZ_SetGroupDCOffset(handle, i, WDcfg.DCoffset[i]);
                    ret |= CAEN_DGTZ_SetGroupSelfTrigger(handle, WDcfg.ChannelTriggerMode[i], (1<<i));
                    ret |= CAEN_DGTZ_SetGroupTriggerThreshold(handle, i, WDcfg.Threshold[i]);
                    ret |= CAEN_DGTZ_SetChannelGroupMask(handle, i, WDcfg.GroupTrgEnableMask[i]);
                } 
                ret |= CAEN_DGTZ_SetTriggerPolarity(handle, i, WDcfg.PulsePolarity[i]); //.TriggerEdge

            }
        }
    } else {
        ret |= CAEN_DGTZ_SetChannelEnableMask(handle, WDcfg.EnableMask);
        for (i = 0; i < WDcfg.Nch; i++) {
            if (WDcfg.EnableMask & (1<<i)) {
				if (WDcfg.Version_used[i] == 1)
					ret |= Set_calibrated_DCO(handle, i, &WDcfg, BoardInfo);
				else
					ret |= CAEN_DGTZ_SetChannelDCOffset(handle, i, WDcfg.DCoffset[i]);
                if (BoardInfo.FamilyCode != CAEN_DGTZ_XX730_FAMILY_CODE &&
                    BoardInfo.FamilyCode != CAEN_DGTZ_XX725_FAMILY_CODE)
                    ret |= CAEN_DGTZ_SetChannelSelfTrigger(handle, WDcfg.ChannelTriggerMode[i], (1<<i));
                ret |= CAEN_DGTZ_SetChannelTriggerThreshold(handle, i, WDcfg.Threshold[i]);
                ret |= CAEN_DGTZ_SetTriggerPolarity(handle, i, WDcfg.PulsePolarity[i]); //.TriggerEdge
            }
        }
        if (BoardInfo.FamilyCode == CAEN_DGTZ_XX730_FAMILY_CODE ||
            BoardInfo.FamilyCode == CAEN_DGTZ_XX725_FAMILY_CODE) {
            // channel pair settings for x730 boards
            for (i = 0; i < WDcfg.Nch; i += 2) {
                if (WDcfg.EnableMask & (0x3 << i)) {
                    CAEN_DGTZ_TriggerMode_t mode = WDcfg.ChannelTriggerMode[i];
                    uint32_t pair_chmask = 0;

                    // Build mode and relevant channelmask. The behaviour is that,
                    // if the triggermode of one channel of the pair is DISABLED,
                    // this channel doesn't take part to the trigger generation.
                    // Otherwise, if both are different from DISABLED, the one of
                    // the even channel is used.
                    if (WDcfg.ChannelTriggerMode[i] != CAEN_DGTZ_TRGMODE_DISABLED) {
                        if (WDcfg.ChannelTriggerMode[i + 1] == CAEN_DGTZ_TRGMODE_DISABLED)
                            pair_chmask = (0x1 << i);
                        else
                            pair_chmask = (0x3 << i);
                    }
                    else {
                        mode = WDcfg.ChannelTriggerMode[i + 1];
                        pair_chmask = (0x2 << i);
                    }

                    pair_chmask &= WDcfg.EnableMask;
                    ret |= CAEN_DGTZ_SetChannelSelfTrigger(handle, mode, pair_chmask);
                }
            }
        }
    }
    if (BoardInfo.FamilyCode == CAEN_DGTZ_XX742_FAMILY_CODE) {
        for(i=0; i<(WDcfg.Nch/8); i++) {
            ret |= CAEN_DGTZ_SetDRS4SamplingFrequency(handle, WDcfg.DRS4Frequency);
            ret |= CAEN_DGTZ_SetGroupFastTriggerDCOffset(handle,i,WDcfg.FTDCoffset[i]);
            ret |= CAEN_DGTZ_SetGroupFastTriggerThreshold(handle,i,WDcfg.FTThreshold[i]);
        }
    }

    /* execute generic write commands */
    for(i=0; i<WDcfg.GWn; i++)
        ret |= WriteRegisterBitmask(handle, WDcfg.GWaddr[i], WDcfg.GWdata[i], WDcfg.GWmask[i]);

    if (ret)
        printf("Warning: errors found during the programming of the digitizer.\nSome settings may not be executed\n");

    return 0;
}

/*! \fn      void GoToNextEnabledGroup(WaveDumpRun_t *WDrun, WaveDumpConfig_t *WDcfg)
*   \brief   selects the next enabled group for plotting
*
*   \param   WDrun:   Pointer to the WaveDumpRun_t data structure
*   \param   WDcfg:   Pointer to the WaveDumpConfig_t data structure
*/
void GoToNextEnabledGroup(WaveDumpRun_t *WDrun, WaveDumpConfig_t *WDcfg) {
    if ((WDcfg->EnableMask) && (WDcfg->Nch>8)) {
        int orgPlotIndex = WDrun->GroupPlotIndex;
        do {
            WDrun->GroupPlotIndex = (++WDrun->GroupPlotIndex)%(WDcfg->Nch/8);
        } while( !((1 << WDrun->GroupPlotIndex)& WDcfg->EnableMask));
        if( WDrun->GroupPlotIndex != orgPlotIndex) {
            printf("Plot group set to %d\n", WDrun->GroupPlotIndex);
        }
    }
    ClearPlot();
}

/*! \brief   return TRUE if board descriped by 'BoardInfo' supports
*            calibration or not.
*
*   \param   BoardInfo board descriptor
*/
int32_t BoardSupportsCalibration(CAEN_DGTZ_BoardInfo_t BoardInfo) {
    return
		BoardInfo.FamilyCode == CAEN_DGTZ_XX761_FAMILY_CODE ||
        BoardInfo.FamilyCode == CAEN_DGTZ_XX751_FAMILY_CODE ||
        BoardInfo.FamilyCode == CAEN_DGTZ_XX730_FAMILY_CODE ||
        BoardInfo.FamilyCode == CAEN_DGTZ_XX725_FAMILY_CODE;
}

/*! \brief   return TRUE if board descriped by 'BoardInfo' supports
*            temperature read or not.
*
*   \param   BoardInfo board descriptor
*/
int32_t BoardSupportsTemperatureRead(CAEN_DGTZ_BoardInfo_t BoardInfo) {
    return
        BoardInfo.FamilyCode == CAEN_DGTZ_XX751_FAMILY_CODE ||
        BoardInfo.FamilyCode == CAEN_DGTZ_XX730_FAMILY_CODE ||
        BoardInfo.FamilyCode == CAEN_DGTZ_XX725_FAMILY_CODE;
}

void calibrate(int handle, WaveDumpRun_t *WDrun, CAEN_DGTZ_BoardInfo_t BoardInfo) {
    printf("\n");
    if (BoardSupportsCalibration(BoardInfo)) {
        if (WDrun->AcqRun == 0) {
            int32_t ret = CAEN_DGTZ_Calibrate(handle);
            if (ret == CAEN_DGTZ_Success) {
                printf("ADC Calibration successfully executed.\n");
            }
            else {
                printf("ADC Calibration failed. CAENDigitizer ERR %d\n", ret);
            }
            printf("\n");
        }
        else {
            printf("Can't run ADC calibration while acquisition is running.\n");
        }
    }
    else {
        printf("ADC Calibration not needed for this board family.\n");
    }
}


/*! \fn      void Calibrate_XX740_DC_Offset(int handle, WaveDumpConfig_t WDcfg, CAEN_DGTZ_BoardInfo_t BoardInfo)
*   \brief   calibrates DAC of enabled channel groups (only if BASELINE_SHIFT is in use)
*
*   \param   handle   Digitizer handle
*   \param   WDcfg:   Pointer to the WaveDumpConfig_t data structure
*   \param   BoardInfo: structure with the board info
*/
void Calibrate_XX740_DC_Offset(int handle, WaveDumpConfig_t *WDcfg, CAEN_DGTZ_BoardInfo_t BoardInfo){
	float cal[MAX_CH];
	float offset[MAX_CH] = { 0 };
	int i = 0, acq = 0, k = 0, p=0, g = 0;
	for (i = 0; i < MAX_CH; i++)
		cal[i] = 1;
	CAEN_DGTZ_ErrorCode ret;
	CAEN_DGTZ_AcqMode_t mem_mode;
	uint32_t  AllocatedSize;

	ERROR_CODES ErrCode = ERR_NONE;
	uint32_t BufferSize;
	CAEN_DGTZ_EventInfo_t       EventInfo;
	char *buffer = NULL;
	char *EventPtr = NULL;
	CAEN_DGTZ_UINT16_EVENT_t    *Event16 = NULL;

	float avg_value[NPOINTS][MAX_CH] = { 0 };
	uint32_t dc[NPOINTS] = { 25,75 }; //test values (%)
	uint32_t groupmask = 0;

	ret = CAEN_DGTZ_GetAcquisitionMode(handle, &mem_mode);//chosen value stored
	if (ret)
		printf("Error trying to read acq mode!!\n");
	ret = CAEN_DGTZ_SetAcquisitionMode(handle, CAEN_DGTZ_SW_CONTROLLED);
	if (ret)
		printf("Error trying to set acq mode!!\n");
	ret = CAEN_DGTZ_SetExtTriggerInputMode(handle, CAEN_DGTZ_TRGMODE_DISABLED);
	if (ret)
		printf("Error trying to set ext trigger!!\n");
	ret = CAEN_DGTZ_SetMaxNumEventsBLT(handle, 1);
	if (ret)
		printf("Warning: error setting max BLT number\n");
	ret = CAEN_DGTZ_SetDecimationFactor(handle, 1);
	if (ret)
		printf("Error trying to set decimation factor!!\n");
	for (g = 0; g< (int32_t)BoardInfo.Channels; g++) //BoardInfo.Channels is number of groups for x740 boards
		groupmask |= (1 << g);
	ret = CAEN_DGTZ_SetGroupSelfTrigger(handle, CAEN_DGTZ_TRGMODE_DISABLED, groupmask);
	if (ret)
		printf("Error disabling self trigger\n");
	ret = CAEN_DGTZ_SetGroupEnableMask(handle, groupmask);
	if (ret)
		printf("Error enabling channel groups.\n");
	///malloc
	ret = CAEN_DGTZ_MallocReadoutBuffer(handle, &buffer, &AllocatedSize);
	if (ret) {
		ErrCode = ERR_MALLOC;
		goto QuitProgram;
	}

	ret = CAEN_DGTZ_AllocateEvent(handle, (void**)&Event16);
	if (ret != CAEN_DGTZ_Success) {
		ErrCode = ERR_MALLOC;
		goto QuitProgram;
	}

	printf("Starting DAC calibration...\n");

	for (p = 0; p < NPOINTS; p++){
		for (i = 0; i < (int32_t)BoardInfo.Channels; i++) { //BoardInfo.Channels is number of groups for x740 boards
				ret = CAEN_DGTZ_SetGroupDCOffset(handle, (uint32_t)i, (uint32_t)((float)(abs(dc[p] - 100))*(655.35)));
				if (ret)
					printf("Error setting group %d test offset\n", i);
		}
	#ifdef _WIN32
				Sleep(200);
	#else
				usleep(200000);
	#endif

	CAEN_DGTZ_ClearData(handle);

	ret = CAEN_DGTZ_SWStartAcquisition(handle);
	if (ret) {
		printf("Error starting X740 acquisition\n");
		goto QuitProgram;
	}

	int value[NACQS][MAX_CH] = { 0 }; //baseline values of the NACQS
	for (acq = 0; acq < NACQS; acq++) {
		CAEN_DGTZ_SendSWtrigger(handle);

		ret = CAEN_DGTZ_ReadData(handle, CAEN_DGTZ_SLAVE_TERMINATED_READOUT_MBLT, buffer, &BufferSize);
		if (ret) {
			ErrCode = ERR_READOUT;
			goto QuitProgram;
		}
		if (BufferSize == 0)
			continue;
		ret = CAEN_DGTZ_GetEventInfo(handle, buffer, BufferSize, 0, &EventInfo, &EventPtr);
		if (ret) {
			ErrCode = ERR_EVENT_BUILD;
			goto QuitProgram;
		}
		// decode the event //
		ret = CAEN_DGTZ_DecodeEvent(handle, EventPtr, (void**)&Event16);
		if (ret) {
			ErrCode = ERR_EVENT_BUILD;
			goto QuitProgram;
		}
		for (g = 0; g < (int32_t)BoardInfo.Channels; g++) {
				for (k = 1; k < 21; k++) //mean over 20 samples
					value[acq][g] += (int)(Event16->DataChannel[g * 8][k]);

				value[acq][g] = (value[acq][g] / 20);
		}

	}//for acq

	///check for clean baselines
	for (g = 0; g < (int32_t)BoardInfo.Channels; g++) {
			int max = 0;
			int mpp = 0;
			int size = (int)pow(2, (double)BoardInfo.ADC_NBits);
			int *freq = calloc(size, sizeof(int));
			//find the most probable value mpp
			for (k = 0; k < NACQS; k++) {
				if (value[k][g] > 0 && value[k][g] < size) {
					freq[value[k][g]]++;
					if (freq[value[k][g]] > max) {
						max = freq[value[k][g]];
						mpp = value[k][g];
					}
				}
			}
			free(freq);
			//discard values too far from mpp
			int ok = 0;
			for (k = 0; k < NACQS; k++) {
				if (value[k][g] >= (mpp - 5) && value[k][g] <= (mpp + 5)) {
					avg_value[p][g] = avg_value[p][g] + (float)value[k][g];
					ok++;
				}
			}
			avg_value[p][g] = (avg_value[p][g] / (float)ok)*100. / (float)size;
	}

	CAEN_DGTZ_SWStopAcquisition(handle);
	}//close for p

	for (g = 0; g < (int32_t)BoardInfo.Channels; g++) {
			cal[g] = ((float)(avg_value[1][g] - avg_value[0][g]) / (float)(dc[1] - dc[0]));
			offset[g] = (float)(dc[1] * avg_value[0][g] - dc[0] * avg_value[1][g]) / (float)(dc[1] - dc[0]);
			printf("Group %d DAC calibration ready.\n",g);
			printf("Cal %f   offset %f\n", cal[g], offset[g]);

			WDcfg->DAC_Calib.cal[g] = cal[g];
			WDcfg->DAC_Calib.offset[g] = offset[g];
	}

	CAEN_DGTZ_ClearData(handle);

	///free events e buffer
	CAEN_DGTZ_FreeReadoutBuffer(&buffer);

	CAEN_DGTZ_FreeEvent(handle, (void**)&Event16);

	ret |= CAEN_DGTZ_SetMaxNumEventsBLT(handle, WDcfg->NumEvents);
	ret |= CAEN_DGTZ_SetDecimationFactor(handle,WDcfg->DecimationFactor);
	ret |= CAEN_DGTZ_SetPostTriggerSize(handle, WDcfg->PostTrigger);
	ret |= CAEN_DGTZ_SetAcquisitionMode(handle, mem_mode);
	ret |= CAEN_DGTZ_SetExtTriggerInputMode(handle, WDcfg->ExtTriggerMode);
	ret |= CAEN_DGTZ_SetGroupEnableMask(handle, WDcfg->EnableMask);
	for (i = 0; i < BoardInfo.Channels; i++) {
		if (WDcfg->EnableMask & (1 << i))
			ret |= CAEN_DGTZ_SetGroupSelfTrigger(handle, WDcfg->ChannelTriggerMode[i], (1 << i));
	}
	if (ret)
		printf("Error setting recorded parameters\n");

	Save_DAC_Calibration_To_Flash(handle, *WDcfg, BoardInfo);

QuitProgram:
		if (ErrCode) {
			printf("\a%s\n", ErrMsg[ErrCode]);
#ifdef WIN32
			printf("Press a key to quit\n");
			getch();
#endif
		}
}


/*! \fn      void Set_relative_Threshold(int handle, WaveDumpConfig_t *WDcfg, CAEN_DGTZ_BoardInfo_t BoardInfo)
*   \brief   sets the threshold relative to the baseline (only if BASELINE_SHIFT is in use)
*
*   \param   handle   Digitizer handle
*   \param   WDcfg:   Pointer to the WaveDumpConfig_t data structure
*   \param   BoardInfo: structure with the board info
*/
void Set_relative_Threshold(int handle, WaveDumpConfig_t *WDcfg, CAEN_DGTZ_BoardInfo_t BoardInfo){
	int ch = 0, i = 0;

	//preliminary check: if baseline shift is not enabled for any channel quit
	int should_start = 0;
	for (ch = 0; ch < (int32_t)BoardInfo.Channels; ch++) {
		if (WDcfg->EnableMask & (1 << ch) && WDcfg->Version_used[ch] == 1) {
			should_start = 1;
			break;
		}
	}
	if (!should_start)
		return;

	CAEN_DGTZ_ErrorCode ret;
	uint32_t  AllocatedSize;
	ERROR_CODES ErrCode = ERR_NONE;
	uint32_t BufferSize;
	CAEN_DGTZ_EventInfo_t       EventInfo;
	char *buffer = NULL;
	char *EventPtr = NULL;
	CAEN_DGTZ_UINT16_EVENT_t    *Event16 = NULL;
	CAEN_DGTZ_UINT8_EVENT_t     *Event8 = NULL;
	uint32_t custom_posttrg = 50, dco, custom_thr;
	float expected_baseline;
	float dco_percent;
	int baseline[MAX_CH] = { 0 }, size = 0, samples = 0;
	int no_self_triggered_event[MAX_CH] = {0};
	int sw_trigger_needed = 0;
	int event_ch;

	///malloc
	ret = CAEN_DGTZ_MallocReadoutBuffer(handle, &buffer, &AllocatedSize);
	if (ret) {
		ErrCode = ERR_MALLOC;
		goto QuitProgram;
	}
	if (WDcfg->Nbit == 8)
		ret = CAEN_DGTZ_AllocateEvent(handle, (void**)&Event8);
	else {
		ret = CAEN_DGTZ_AllocateEvent(handle, (void**)&Event16);
	}
	if (ret != CAEN_DGTZ_Success) {
		ErrCode = ERR_MALLOC;
		goto QuitProgram;
	}

	//some custom settings
	ret = CAEN_DGTZ_SetPostTriggerSize(handle, custom_posttrg);
	if (ret) {
		printf("Threshold calc failed. Error trying to set post trigger!!\n");
		return;
	}
	//try to set a small threshold to get a self triggered event
	for (ch = 0; ch < (int32_t)BoardInfo.Channels; ch++) {
		if (WDcfg->EnableMask & (1 << ch) && WDcfg->Version_used[ch] == 1) {
			if (BoardInfo.FamilyCode == CAEN_DGTZ_XX740_FAMILY_CODE)
				ret = CAEN_DGTZ_GetGroupDCOffset(handle, ch, &dco);
			else
				ret = CAEN_DGTZ_GetChannelDCOffset(handle, ch, &dco);
			if (ret) {
				printf("Threshold calc failed. Error trying to get DCoffset values!!\n");
				return;
			}
			dco_percent = (float)dco / 65535.;
			expected_baseline = pow(2, (double)BoardInfo.ADC_NBits) * (1.0 - dco_percent);

			custom_thr = (WDcfg->PulsePolarity[ch] == CAEN_DGTZ_PulsePolarityPositive) ? ((uint32_t)expected_baseline + 100) : ((uint32_t)expected_baseline - 100);
			
			if (BoardInfo.FamilyCode == CAEN_DGTZ_XX740_FAMILY_CODE)
				ret = CAEN_DGTZ_SetGroupTriggerThreshold(handle, ch, custom_thr);
			else
				ret = CAEN_DGTZ_SetChannelTriggerThreshold(handle, ch, custom_thr);
			if (ret) {
				printf("Threshold calc failed. Error trying to set custom threshold value!!\n");
				return;
			}
		}
	}

	CAEN_DGTZ_SWStartAcquisition(handle);
#ifdef _WIN32
	Sleep(300);
#else
	usleep(300000);
#endif

	ret = CAEN_DGTZ_ReadData(handle, CAEN_DGTZ_SLAVE_TERMINATED_READOUT_MBLT, buffer, &BufferSize);
	if (ret) {
		ErrCode = ERR_READOUT;
		goto QuitProgram;
	}
	//we have some self-triggered event 
	if (BufferSize > 0) {
		ret = CAEN_DGTZ_GetEventInfo(handle, buffer, BufferSize, 0, &EventInfo, &EventPtr);
		if (ret) {
			ErrCode = ERR_EVENT_BUILD;
			goto QuitProgram;
		}
		// decode the event //
		if (WDcfg->Nbit == 8)
			ret = CAEN_DGTZ_DecodeEvent(handle, EventPtr, (void**)&Event8);
		else
			ret = CAEN_DGTZ_DecodeEvent(handle, EventPtr, (void**)&Event16);

		if (ret) {
			ErrCode = ERR_EVENT_BUILD;
			goto QuitProgram;
		}

		for (ch = 0; ch < (int32_t)BoardInfo.Channels; ch++) {
			if (WDcfg->EnableMask & (1 << ch) && WDcfg->Version_used[ch] == 1) {
				event_ch = (BoardInfo.FamilyCode == CAEN_DGTZ_XX740_FAMILY_CODE) ? (ch * 8) : ch;//for x740 boards shift to channel 0 of next group
				size = (WDcfg->Nbit == 8) ? Event8->ChSize[event_ch] : Event16->ChSize[event_ch];
				if (size == 0) {//no data from channel ch
					no_self_triggered_event[ch] = 1;
					sw_trigger_needed = 1;
					continue;
				}
				
				else {//use only one tenth of the pre-trigger samples to calculate the baseline
					samples = (int)(size * ((100 - custom_posttrg) / 2) / 100);

					for (i = 0; i < samples; i++) //mean over some pre trigger samples
					{
						if (WDcfg->Nbit == 8)
							baseline[ch] += (int)(Event8->DataChannel[event_ch][i]);
						else
							baseline[ch] += (int)(Event16->DataChannel[event_ch][i]);
					}
					baseline[ch] = (baseline[ch] / samples);
				}

				if (WDcfg->PulsePolarity[ch] == CAEN_DGTZ_PulsePolarityPositive)
					WDcfg->Threshold[ch] = (uint32_t)baseline[ch] + thr_file[ch];
				else 	if (WDcfg->PulsePolarity[ch] == CAEN_DGTZ_PulsePolarityNegative)
					WDcfg->Threshold[ch] = (uint32_t)baseline[ch] - thr_file[ch];

				if (WDcfg->Threshold[ch] < 0) WDcfg->Threshold[ch] = 0;
				size = (int)pow(2, (double)BoardInfo.ADC_NBits);
				if (WDcfg->Threshold[ch] > (uint32_t)size) WDcfg->Threshold[ch] = size;

				if (BoardInfo.FamilyCode == CAEN_DGTZ_XX740_FAMILY_CODE)
					ret = CAEN_DGTZ_SetGroupTriggerThreshold(handle, ch, WDcfg->Threshold[ch]);
				else
					ret = CAEN_DGTZ_SetChannelTriggerThreshold(handle, ch, WDcfg->Threshold[ch]);
				if (ret)
					printf("Warning: error setting ch %d corrected threshold\n", ch);
			}
		}
	}
	else {
		sw_trigger_needed = 1;
		for(ch = 0; ch < (int32_t)BoardInfo.Channels; ch++)
			no_self_triggered_event[ch] = 1;
	}

	CAEN_DGTZ_ClearData(handle);

	//if from some channels we had no self triggered event, we send a software trigger
	if (sw_trigger_needed) {
		CAEN_DGTZ_SendSWtrigger(handle);

		ret = CAEN_DGTZ_ReadData(handle, CAEN_DGTZ_SLAVE_TERMINATED_READOUT_MBLT, buffer, &BufferSize);
		if (ret) {
			ErrCode = ERR_READOUT;
			goto QuitProgram;
		}
		if (BufferSize == 0)
			return;

		ret = CAEN_DGTZ_GetEventInfo(handle, buffer, BufferSize, 0, &EventInfo, &EventPtr);
		if (ret) {
			ErrCode = ERR_EVENT_BUILD;
			goto QuitProgram;
		}
		// decode the event //
		if (WDcfg->Nbit == 8)
			ret = CAEN_DGTZ_DecodeEvent(handle, EventPtr, (void**)&Event8);
		else
			ret = CAEN_DGTZ_DecodeEvent(handle, EventPtr, (void**)&Event16);

		if (ret) {
			ErrCode = ERR_EVENT_BUILD;
			goto QuitProgram;
		}

		for (ch = 0; ch < (int32_t)BoardInfo.Channels; ch++) {
			if (WDcfg->EnableMask & (1 << ch) && WDcfg->Version_used[ch] == 1) {
				event_ch = (BoardInfo.FamilyCode == CAEN_DGTZ_XX740_FAMILY_CODE) ? (ch * 8) : ch;//for x740 boards shift to channel 0 of next group
				size = (WDcfg->Nbit == 8) ? Event8->ChSize[event_ch] : Event16->ChSize[event_ch];
				if (!no_self_triggered_event[ch])//we already have a good baseline for channel ch
					continue;

				//use some samples to calculate the baseline
				for (i = 1; i < 11; i++){ //mean over 10 samples
					if (WDcfg->Nbit == 8)
						baseline[ch] += (int)(Event8->DataChannel[event_ch][i]);
					else
						baseline[ch] += (int)(Event16->DataChannel[event_ch][i]);
				}
					baseline[ch] = (baseline[ch] / 10);
			}

			if (WDcfg->PulsePolarity[ch] == CAEN_DGTZ_PulsePolarityPositive)
				WDcfg->Threshold[ch] = (uint32_t)baseline[ch] + thr_file[ch];
			else 	if (WDcfg->PulsePolarity[ch] == CAEN_DGTZ_PulsePolarityNegative)
				WDcfg->Threshold[ch] = (uint32_t)baseline[ch] - thr_file[ch];

			if (WDcfg->Threshold[ch] < 0) WDcfg->Threshold[ch] = 0;
				size = (int)pow(2, (double)BoardInfo.ADC_NBits);
			if (WDcfg->Threshold[ch] > (uint32_t)size) WDcfg->Threshold[ch] = size;

			if (BoardInfo.FamilyCode == CAEN_DGTZ_XX740_FAMILY_CODE)
				ret = CAEN_DGTZ_SetGroupTriggerThreshold(handle, ch, WDcfg->Threshold[ch]);
			else
				ret = CAEN_DGTZ_SetChannelTriggerThreshold(handle, ch, WDcfg->Threshold[ch]);
			if (ret)
					printf("Warning: error setting ch %d corrected threshold\n", ch);
		}
	}//end sw trigger event analysis

	CAEN_DGTZ_SWStopAcquisition(handle);

	//reset posttrigger
	ret = CAEN_DGTZ_SetPostTriggerSize(handle, WDcfg->PostTrigger);
	if (ret)
		printf("Error resetting post trigger.\n");

	CAEN_DGTZ_ClearData(handle);

	CAEN_DGTZ_FreeReadoutBuffer(&buffer);
	if (WDcfg->Nbit == 8)
		CAEN_DGTZ_FreeEvent(handle, (void**)&Event8);
	else
		CAEN_DGTZ_FreeEvent(handle, (void**)&Event16);


QuitProgram:
	if (ErrCode) {
		printf("\a%s\n", ErrMsg[ErrCode]);
#ifdef WIN32
		printf("Press a key to quit\n");
		getch();
#endif
	}
}

/*! \fn      void Calibrate_DC_Offset(int handle, WaveDumpConfig_t WDcfg, CAEN_DGTZ_BoardInfo_t BoardInfo)
*   \brief   calibrates DAC of enabled channels (only if BASELINE_SHIFT is in use)
*
*   \param   handle   Digitizer handle
*   \param   WDcfg:   Pointer to the WaveDumpConfig_t data structure
*   \param   BoardInfo: structure with the board info
*/
void Calibrate_DC_Offset(int handle, WaveDumpConfig_t *WDcfg, CAEN_DGTZ_BoardInfo_t BoardInfo){
	float cal[MAX_CH];
	float offset[MAX_CH] = { 0 };
	int i = 0, k = 0, p = 0, acq = 0, ch = 0;
	for (i = 0; i < MAX_CH; i++)
		cal[i] = 1;
	CAEN_DGTZ_ErrorCode ret;
	CAEN_DGTZ_AcqMode_t mem_mode;
	uint32_t  AllocatedSize;

	ERROR_CODES ErrCode = ERR_NONE;
	uint32_t BufferSize;
	CAEN_DGTZ_EventInfo_t       EventInfo;
	char *buffer = NULL;
	char *EventPtr = NULL;
	CAEN_DGTZ_UINT16_EVENT_t    *Event16 = NULL;
	CAEN_DGTZ_UINT8_EVENT_t     *Event8 = NULL;

	float avg_value[NPOINTS][MAX_CH] = { 0 };
	uint32_t dc[NPOINTS] = { 25,75 }; //test values (%)
	uint32_t chmask = 0;

	ret = CAEN_DGTZ_GetAcquisitionMode(handle, &mem_mode);//chosen value stored
	if (ret)
		printf("Error trying to read acq mode!!\n");
	ret = CAEN_DGTZ_SetAcquisitionMode(handle, CAEN_DGTZ_SW_CONTROLLED);
	if (ret)
		printf("Error trying to set acq mode!!\n");
	ret = CAEN_DGTZ_SetExtTriggerInputMode(handle, CAEN_DGTZ_TRGMODE_DISABLED);
	if (ret)
		printf("Error trying to set ext trigger!!\n");
	for (ch = 0; ch < (int32_t)BoardInfo.Channels; ch++)
		chmask |= (1 << ch);
	ret = CAEN_DGTZ_SetChannelSelfTrigger(handle, CAEN_DGTZ_TRGMODE_DISABLED, chmask);
	if (ret)
		printf("Warning: error disabling channels self trigger\n");
	ret = CAEN_DGTZ_SetChannelEnableMask(handle, chmask);
	if (ret)
		printf("Warning: error enabling channels.\n");
	ret = CAEN_DGTZ_SetMaxNumEventsBLT(handle, 1);
	if (ret)
		printf("Warning: error setting max BLT number\n");
	if (BoardInfo.FamilyCode == CAEN_DGTZ_XX740_FAMILY_CODE || BoardInfo.FamilyCode == CAEN_DGTZ_XX724_FAMILY_CODE) {
		ret = CAEN_DGTZ_SetDecimationFactor(handle, 1);
		if (ret)
			printf("Error trying to set decimation factor!!\n");
	}

	///malloc
	ret = CAEN_DGTZ_MallocReadoutBuffer(handle, &buffer, &AllocatedSize);
	if (ret) {
		ErrCode = ERR_MALLOC;
		goto QuitProgram;
	}
	if (WDcfg->Nbit == 8)
		ret = CAEN_DGTZ_AllocateEvent(handle, (void**)&Event8);
	else {
		ret = CAEN_DGTZ_AllocateEvent(handle, (void**)&Event16);
	}
	if (ret != CAEN_DGTZ_Success) {
		ErrCode = ERR_MALLOC;
		goto QuitProgram;
	}

	printf("Starting DAC calibration...\n");
	
	for (p = 0; p < NPOINTS; p++){
		//set new dco  test value to all channels
		for (ch = 0; ch < (int32_t)BoardInfo.Channels; ch++) {
				ret = CAEN_DGTZ_SetChannelDCOffset(handle, (uint32_t)ch, (uint32_t)((float)(abs(dc[p] - 100))*(655.35)));
				if (ret)
					printf("Error setting ch %d test offset\n", ch);
		}
#ifdef _WIN32
					Sleep(200);
#else
					usleep(200000);
#endif
		CAEN_DGTZ_ClearData(handle);

		ret = CAEN_DGTZ_SWStartAcquisition(handle);
		if (ret){
			printf("Error starting acquisition\n");
			goto QuitProgram;
		}
		
		int value[NACQS][MAX_CH] = { 0 };//baseline values of the NACQS
		for (acq = 0; acq < NACQS; acq++){
			CAEN_DGTZ_SendSWtrigger(handle);

			ret = CAEN_DGTZ_ReadData(handle, CAEN_DGTZ_SLAVE_TERMINATED_READOUT_MBLT, buffer, &BufferSize);
			if (ret) {
					ErrCode = ERR_READOUT;
					goto QuitProgram;
			}
			if (BufferSize == 0)
				continue;
			ret = CAEN_DGTZ_GetEventInfo(handle, buffer, BufferSize, 0, &EventInfo, &EventPtr);
			if (ret) {
					ErrCode = ERR_EVENT_BUILD;
					goto QuitProgram;
			}
			// decode the event //
			if (WDcfg->Nbit == 8)
				ret = CAEN_DGTZ_DecodeEvent(handle, EventPtr, (void**)&Event8);
				else
				ret = CAEN_DGTZ_DecodeEvent(handle, EventPtr, (void**)&Event16);

			if (ret) {
				ErrCode = ERR_EVENT_BUILD;
				goto QuitProgram;
			}

			for (ch = 0; ch < (int32_t)BoardInfo.Channels; ch++){
					for (i = 1; i < 21; i++) //mean over 20 samples
					{
						if (WDcfg->Nbit == 8)
							value[acq][ch] += (int)(Event8->DataChannel[ch][i]);
						else
							value[acq][ch] += (int)(Event16->DataChannel[ch][i]);
					}
					value[acq][ch] = (value[acq][ch] / 20);
			}

		}//for acq

		///check for clean baselines
		for (ch = 0; ch < (int32_t)BoardInfo.Channels; ch++) {
				int max = 0, ok = 0;
				int mpp = 0;
				int size = (int)pow(2, (double)BoardInfo.ADC_NBits);
				int *freq = calloc(size, sizeof(int));

				//find most probable value mpp
				for (k = 0; k < NACQS; k++) {
					if (value[k][ch] > 0 && value[k][ch] < size) {
						freq[value[k][ch]]++;
						if (freq[value[k][ch]] > max) {
							max = freq[value[k][ch]];
							mpp = value[k][ch];
						}
					}
				}
				free(freq);
				//discard values too far from mpp
				for (k = 0; k < NACQS; k++) {
					if (value[k][ch] >= (mpp - 5) && value[k][ch] <= (mpp + 5)) {
						avg_value[p][ch] = avg_value[p][ch] + (float)value[k][ch];
						ok++;
					}
				}
				//calculate final best average value
				avg_value[p][ch] = (avg_value[p][ch] / (float)ok)*100. / (float)size;
		}

		CAEN_DGTZ_SWStopAcquisition(handle);
	}//close for p

	for (ch = 0; ch < (int32_t)BoardInfo.Channels; ch++) {
			cal[ch] = ((float)(avg_value[1][ch] - avg_value[0][ch]) / (float)(dc[1] - dc[0]));
			offset[ch] = (float)(dc[1] * avg_value[0][ch] - dc[0] * avg_value[1][ch]) / (float)(dc[1] - dc[0]);
			printf("Channel %d DAC calibration ready.\n", ch);
			//printf("Channel %d --> Cal %f   offset %f\n", ch, cal[ch], offset[ch]);

			WDcfg->DAC_Calib.cal[ch] = cal[ch];
			WDcfg->DAC_Calib.offset[ch] = offset[ch];
	}

	CAEN_DGTZ_ClearData(handle);

	 ///free events e buffer
	CAEN_DGTZ_FreeReadoutBuffer(&buffer);
	if (WDcfg->Nbit == 8)
		CAEN_DGTZ_FreeEvent(handle, (void**)&Event8);
	else
		CAEN_DGTZ_FreeEvent(handle, (void**)&Event16);

	//reset settings
	ret |= CAEN_DGTZ_SetMaxNumEventsBLT(handle, WDcfg->NumEvents);
	ret |= CAEN_DGTZ_SetPostTriggerSize(handle, WDcfg->PostTrigger);
	ret |= CAEN_DGTZ_SetAcquisitionMode(handle, mem_mode);
	ret |= CAEN_DGTZ_SetExtTriggerInputMode(handle, WDcfg->ExtTriggerMode);
	ret |= CAEN_DGTZ_SetChannelEnableMask(handle, WDcfg->EnableMask);
	if (BoardInfo.FamilyCode == CAEN_DGTZ_XX740_FAMILY_CODE || BoardInfo.FamilyCode == CAEN_DGTZ_XX724_FAMILY_CODE)
		ret |= CAEN_DGTZ_SetDecimationFactor(handle, WDcfg->DecimationFactor);
	if (ret)
		printf("Error resetting some parameters after DAC calibration\n");

	//reset self trigger mode settings
	if (BoardInfo.FamilyCode == CAEN_DGTZ_XX730_FAMILY_CODE || BoardInfo.FamilyCode == CAEN_DGTZ_XX725_FAMILY_CODE) {
		// channel pair settings for x730 boards
		for (i = 0; i < WDcfg->Nch; i += 2) {
			if (WDcfg->EnableMask & (0x3 << i)) {
				CAEN_DGTZ_TriggerMode_t mode = WDcfg->ChannelTriggerMode[i];
				uint32_t pair_chmask = 0;

				if (WDcfg->ChannelTriggerMode[i] != CAEN_DGTZ_TRGMODE_DISABLED) {
					if (WDcfg->ChannelTriggerMode[i + 1] == CAEN_DGTZ_TRGMODE_DISABLED)
						pair_chmask = (0x1 << i);
					else
						pair_chmask = (0x3 << i);
				}
				else {
					mode = WDcfg->ChannelTriggerMode[i + 1];
					pair_chmask = (0x2 << i);
				}

				pair_chmask &= WDcfg->EnableMask;
				ret |= CAEN_DGTZ_SetChannelSelfTrigger(handle, mode, pair_chmask);
			}
		}
	}
	else {
		for (i = 0; i < WDcfg->Nch; i++) {
			if (WDcfg->EnableMask & (1 << i))
				ret |= CAEN_DGTZ_SetChannelSelfTrigger(handle, WDcfg->ChannelTriggerMode[i], (1 << i));
		}
	}
	if (ret)
		printf("Error resetting self trigger mode after DAC calibration\n");

	Save_DAC_Calibration_To_Flash(handle, *WDcfg, BoardInfo);

QuitProgram:
	if (ErrCode) {
		printf("\a%s\n", ErrMsg[ErrCode]);
#ifdef WIN32
		printf("Press a key to quit\n");
		getch();
#endif
	}

}

/*! \fn      void Set_calibrated_DCO(int handle, WaveDumpConfig_t *WDcfg, CAEN_DGTZ_BoardInfo_t BoardInfo)
*   \brief   sets the calibrated DAC value using calibration data (only if BASELINE_SHIFT is in use)
*
*   \param   handle   Digitizer handle
*   \param   WDcfg:   Pointer to the WaveDumpConfig_t data structure
*   \param   BoardInfo: structure with the board info
*/
int Set_calibrated_DCO(int handle, int ch, WaveDumpConfig_t *WDcfg, CAEN_DGTZ_BoardInfo_t BoardInfo) {
	int ret = CAEN_DGTZ_Success;
	if (WDcfg->Version_used[ch] == 0) //old DC_OFFSET config, skip calibration
		return ret;
	if (WDcfg->PulsePolarity[ch] == CAEN_DGTZ_PulsePolarityPositive) {
		WDcfg->DCoffset[ch] = (uint32_t)((float)(fabs((((float)dc_file[ch] - WDcfg->DAC_Calib.offset[ch]) / WDcfg->DAC_Calib.cal[ch]) - 100.))*(655.35));
		if (WDcfg->DCoffset[ch] > 65535) WDcfg->DCoffset[ch] = 65535;
		if (WDcfg->DCoffset[ch] < 0) WDcfg->DCoffset[ch] = 0;
	}
	else if (WDcfg->PulsePolarity[ch] == CAEN_DGTZ_PulsePolarityNegative) {
		WDcfg->DCoffset[ch] = (uint32_t)((float)(fabs(((fabs(dc_file[ch] - 100.) - WDcfg->DAC_Calib.offset[ch]) / WDcfg->DAC_Calib.cal[ch]) - 100.))*(655.35));
		if (WDcfg->DCoffset[ch] < 0) WDcfg->DCoffset[ch] = 0;
		if (WDcfg->DCoffset[ch] > 65535) WDcfg->DCoffset[ch] = 65535;
	}

	if (BoardInfo.FamilyCode == CAEN_DGTZ_XX740_FAMILY_CODE) {
		ret = CAEN_DGTZ_SetGroupDCOffset(handle, (uint32_t)ch, WDcfg->DCoffset[ch]);
		if (ret)
			printf("Error setting group %d offset\n", ch);
	}
	else {
		ret = CAEN_DGTZ_SetChannelDCOffset(handle, (uint32_t)ch, WDcfg->DCoffset[ch]);
		if (ret)
			printf("Error setting channel %d offset\n", ch);
	}

	return ret;
}


/*! \fn      void CheckKeyboardCommands(WaveDumpRun_t *WDrun)
*   \brief   check if there is a key pressed and execute the relevant command
*
*   \param   WDrun:   Pointer to the WaveDumpRun_t data structure
*   \param   WDcfg:   Pointer to the WaveDumpConfig_t data structure
*   \param   BoardInfo: structure with the board info
*/
void CheckKeyboardCommands(int handle, WaveDumpRun_t *WDrun, WaveDumpConfig_t *WDcfg, CAEN_DGTZ_BoardInfo_t BoardInfo)
{
    int c = 0;
	uint8_t percent;
    if(!kbhit())
        return;

    c = getch();
    if ((c < '9') && (c >= '0')) {
        int ch = c-'0';
        if ((BoardInfo.FamilyCode == CAEN_DGTZ_XX740_FAMILY_CODE) || (BoardInfo.FamilyCode == CAEN_DGTZ_XX742_FAMILY_CODE)){
			WDrun->ChannelPlotMask ^= (1 << ch);
            
			if ((BoardInfo.FamilyCode == CAEN_DGTZ_XX740_FAMILY_CODE) && (ch == 8)) printf("Channel %d belongs to a different group\n", ch + WDrun->GroupPlotIndex * 8);
			else
			if (WDrun->ChannelPlotMask & (1 << ch))
                printf("Channel %d enabled for plotting\n", ch + WDrun->GroupPlotIndex*8);
            else
                printf("Channel %d disabled for plotting\n", ch + WDrun->GroupPlotIndex*8);
        } 		
    } else {
        switch(c) {
        case 'g' :
            // Update the group plot index
            if ((WDcfg->EnableMask) && (WDcfg->Nch>8))
                GoToNextEnabledGroup(WDrun, WDcfg);
            break;
        case 'q' :
            WDrun->Quit = 1;
            break;
        case 'R' :
            WDrun->Restart = 1;
            break;
        case 't' :
            if (!WDrun->ContinuousTrigger) {
                CAEN_DGTZ_SendSWtrigger(handle);
                printf("Single Software Trigger issued\n");
            }
            break;
        case 'T' :
            WDrun->ContinuousTrigger ^= 1;
            if (WDrun->ContinuousTrigger)
                printf("Continuous trigger is enabled\n");
            else
                printf("Continuous trigger is disabled\n");
            break;
        case 'P' :
            if (WDrun->ChannelPlotMask == 0)
                printf("No channel enabled for plotting\n");
            else
                WDrun->ContinuousPlot ^= 1;
            break;
        case 'p' :
            if (WDrun->ChannelPlotMask == 0)
                printf("No channel enabled for plotting\n");
            else
                WDrun->SinglePlot = 1;
            break;
		case 'l' :
			WDrun->PlotYscaleLog ^= 1;
			break;
        case 'f' :
            WDrun->PlotType = (WDrun->PlotType == PLOT_FFT) ? PLOT_WAVEFORMS : PLOT_FFT;
            WDrun->SetPlotOptions = 1;
            break;
        case 'h' :
            WDrun->PlotType = (WDrun->PlotType == PLOT_HISTOGRAM) ? PLOT_WAVEFORMS : PLOT_HISTOGRAM;
            WDrun->RunHisto = (WDrun->PlotType == PLOT_HISTOGRAM);
            WDrun->SetPlotOptions = 1;
            break;
		case 'z' :
			WDrun->PlotType = (WDrun->PlotType == PLOT_TIME_HISTOGRAM) ? PLOT_WAVEFORMS : PLOT_TIME_HISTOGRAM;
			WDrun->RunHisto = (WDrun->PlotType == PLOT_TIME_HISTOGRAM);
			WDrun->SetPlotOptions = 1;
			break;
        case 'w' :
            if (!WDrun->ContinuousWrite)
                WDrun->SingleWrite = 1;
            break;
        case 'W' :
            WDrun->ContinuousWrite ^= 1;
            if (WDrun->ContinuousWrite)
                printf("Continuous writing is enabled\n");
            else
                printf("Continuous writing is disabled\n");
            break;
        case 's' :
            if (WDrun->AcqRun == 0) {

				if (BoardInfo.FamilyCode != CAEN_DGTZ_XX742_FAMILY_CODE)//XX742 not considered
					Set_relative_Threshold(handle, WDcfg, BoardInfo);

				if (BoardInfo.FamilyCode == CAEN_DGTZ_XX730_FAMILY_CODE || BoardInfo.FamilyCode == CAEN_DGTZ_XX725_FAMILY_CODE)
					WDrun->GroupPlotSwitch = 0;
				
                printf("Acquisition started\n");

                CAEN_DGTZ_SWStartAcquisition(handle);

                WDrun->AcqRun = 1;

            } else {
                printf("Acquisition stopped\n");
                CAEN_DGTZ_SWStopAcquisition(handle);
                WDrun->AcqRun = 0;
				//WDrun->Restart = 1;
            }
            break;
        case 'm' :
            if (BoardSupportsTemperatureRead(BoardInfo)) {
                if (WDrun->AcqRun == 0) {
                    int32_t ch;
                    for (ch = 0; ch < (int32_t)BoardInfo.Channels; ch++) {
                        uint32_t temp;
                        int32_t ret = CAEN_DGTZ_ReadTemperature(handle, ch, &temp);
                        printf("CH%02d: ", ch);
                        if (ret == CAEN_DGTZ_Success)
                            printf("%u C\n", temp);
                        else
                            printf("CAENDigitizer ERR %d\n", ret);
                    }
                    printf("\n");
                }
                else {
                    printf("Can't run temperature monitor while acquisition is running.\n");
                }
            }
            else {
                printf("Board Family doesn't support ADC Temperature Monitor.\n");
            }
            break;
        case 'c' :
            calibrate(handle, WDrun, BoardInfo);
            break;
		case 'D':
			if (WDrun->AcqRun == 0) {
				printf("Disconnect input signal from all channels and press any key to start.\n");
				getch();
				if (BoardInfo.FamilyCode == CAEN_DGTZ_XX740_FAMILY_CODE)//XX740 specific
					Calibrate_XX740_DC_Offset(handle, WDcfg, BoardInfo);
				else if (BoardInfo.FamilyCode != CAEN_DGTZ_XX742_FAMILY_CODE)//XX742 not considered
					Calibrate_DC_Offset(handle, WDcfg, BoardInfo);

				int i = 0;
				CAEN_DGTZ_ErrorCode err;
				//set new dco values using calibration data
				for (i = 0; i < BoardInfo.Channels; i++) {
					if (WDcfg->EnableMask & (1 << i)) {
						if(WDcfg->Version_used[i] == 1)
							Set_calibrated_DCO(handle, i, WDcfg, BoardInfo);
						else {
							err = CAEN_DGTZ_SetChannelDCOffset(handle, (uint32_t)i, WDcfg->DCoffset[i]);
							if (err)
								printf("Error setting channel %d offset\n", i);
						}
					}
				}
#ifdef _WIN32
				Sleep(200);
#else
				usleep(200000);
#endif
				printf("DAC calibration ready!!\n");
			}
			else {
				printf("Acquisition is running. Stop acquisition to start DAC calibration.\n");
			}
			break;
        case ' ' :
            printf("\n                            Bindkey help                                \n");
            printf("--------------------------------------------------------------------------\n");;
            printf("  [q]   Quit\n");
            printf("  [R]   Reload configuration file and restart\n");
            printf("  [s]   Start/Stop acquisition\n");
            printf("  [t]   Send a software trigger (single shot)\n");
            printf("  [T]   Enable/Disable continuous software trigger\n");
            printf("  [w]   Write one event to output file\n");
            printf("  [W]   Enable/Disable continuous writing to output file\n");
            printf("  [p]   Plot one event\n");
            printf("  [P]   Enable/Disable continuous plot\n");
            printf("  [f]   Toggle between FFT and Waveform plot\n");
            printf("  [h]   Toggle between Histogram and Waveform plot\n");
            printf("  [g]   Change the index of the group to plot (XX740 family)\n");
            printf("  [m]   Single ADC temperature monitor (XX751/30/25 only)\n");
            printf("  [c]   ADC Calibration (XX751/30/25 only)\n");
			printf("  [D]   DAC offset calibration\n");
            printf(" [0-7]  Enable/Disable one channel on the plot\n");
            printf("        For x740 family this is the plotted group's relative channel index\n");
            printf("[SPACE] This help\n");
            printf("--------------------------------------------------------------------------\n");
            printf("Press a key to continue\n");
            getch();
            break;
        default :   break;
        }
    }
}


