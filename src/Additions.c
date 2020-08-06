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