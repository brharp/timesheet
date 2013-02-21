#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>



/* Utilities */

#if !defined(__BOOL_DEFINED)
typedef int bool;
#define false 0
#define true !false
#endif


/* strsplt - splits a string on sep */

char *strsplt(char *src, const char *sep, char **rest)
{
    char *start = src != 0 ? src : *rest;
    char *end;

    /*
     * Skip over leading delimiters.
     */
    start += strspn(start, sep);
    if (*start == 0) {
		if (rest != 0) {
			*rest = start;
		}
		return 0;
    }

    /*
     * Separate off one token.
     */
    end = start + strcspn(start, sep);
    if (*end != 0) {
		*end++ = 0;
	}
	if (rest != 0) {
		*rest = end;
	}
    return start;
}



/* The timesheet part. */ 


time_t start = 0; /* start of report range */ 
time_t end = 0; /* end of report range */ 


typedef struct group GROUP; 
typedef time_t CELL; 
typedef CELL *ROW; 
typedef ROW *SHEET; 


struct group { 
	size_t row; 
	const char *name; 
	GROUP *next; 
}; 


SHEET sheet = 0; 
size_t nrows = 0; 
size_t ncols = 0; 

GROUP *groups = 0; 
size_t ngroups = 0; 

void add_rows(size_t delta) 
{ 
	size_t i, j; 
	sheet = (SHEET)realloc(sheet, (nrows + delta) * sizeof(ROW)); 
	for (i = 0; i < delta; i++) { 
		sheet[nrows + i] = (CELL *)malloc(ncols * sizeof(CELL)); 
		for (j = 0; j < ncols; j++) { 
			sheet[nrows + i][j] = 0; 
		} 
	} 
	nrows += delta; 
} 


GROUP *add_group(const char *s) 
{ 
	GROUP *grp;
	if (ngroups == nrows) { 
		add_rows(256); 
	} 
	grp = (GROUP *)malloc(sizeof(GROUP)); 
	grp->name = _strdup(s); 
	grp->row = ngroups++; 
	grp->next = groups; 
	groups = grp; 
	return grp; 
} 



GROUP *find_group(const char *s) 
{ 
	GROUP *grp; 
	for (grp = groups; grp != 0; grp = grp->next) { 
		if (strcmp(s, grp->name) == 0) { 
			return grp; 
		} 
	} 
	return add_group(s); 
} 







/* The iCalendar part. */ 


struct rrule {
	const char *freq;
	int max_count;
	int count;
	time_t until;
	int interval;
	int *by_day;
	int *day;
};


time_t parse_date(const char *s, bool isdate);
time_t parse_duration(const char *s);
void   parse_rrule(const char *s, struct rrule *rrule);
time_t rrepeat(time_t, struct rrule *);

const char *BEGIN = "BEGIN", *END = "END", *SUMMARY = "SUMMARY",
	*DTSTART = "DTSTART", *DTEND = "DTEND", *DURATION = "DURATION",
	*DATE = "DATE", *VALUE = "VALUE", *VEVENT = "VEVENT",
	*RRULE = "RRULE", *FREQ = "FREQ", *YEARLY = "YEARLY",
	*MONTHLY = "MONTHLY", *WEEKLY = "WEEKLY", *DAILY = "DAILY",
	*INTERVAL = "INTERVAL", *COUNT = "COUNT", *UNTIL = "UNTIL",
	*BYDAY = "BYDAY";


void begin(const char *context) 
{ 
	char *summary = 0; 
	time_t dtstart = 0; 
	time_t dtend = 0; 
	time_t duration = 0;
	struct rrule rrule = {0};
	bool isdate = false; 
	char *line = 0;
	char *value, *name, *params, *pname, *pvalue;
	int ch;

	line = (char *)malloc(80);
	while (!feof(stdin)) { 
		fgets(line, 80, stdin); 
		while ((ch = fgetc(stdin)) == ' ') { 
			line = (char *)realloc(line, strlen(line) + 80); 
			fgets(line + strlen(line), 80, stdin); 
		}
		ungetc(ch, stdin);

		strsplt(line, ":", &value); 
		name = strsplt(line, ";", &params);
		value = strsplt(value, "\r\n", 0);

		/* BEGIN */
		if (strcmp(name, BEGIN) == 0) { 
			begin(value); 
		/* SUMMARY */
		} else if (strcmp(name, SUMMARY) == 0){
			size_t sz = strlen(value) + 1;
			summary = (char *)realloc(summary, sz);
			strcpy_s(summary, sz, value);
		/* DTSTART */
		} else if (strcmp(name, DTSTART) == 0) { 
			while ((pname = strsplt(0, "=", &params)) != 0) { 
				pvalue = strsplt(0, ";", &params); 
				if (strcmp(pname, VALUE) == 0) { 
					isdate = !(strcmp(pvalue, DATE)); 
				} 
			} 
			dtstart = parse_date(value, isdate); 
		/* DTEND */
		} else if (strcmp(name, DTEND) == 0) { 
			while ((pname = strsplt(0, "=", &params)) != 0) { 
				pvalue = strsplt(0, ";", &params); 
				if (strcmp(pname, VALUE) == 0) { 
					isdate = !(strcmp(pvalue, DATE)); 
				} 
			} 
			dtend = parse_date(value, isdate); 
		} else if (strcmp(name, DURATION) == 0) {
			/* DURATION */
			duration = parse_duration(value);
		} else if (strcmp(name, RRULE) == 0) {
			/* RRULE */
			parse_rrule(value, &rrule);
		} else if (strcmp(name, END) == 0) { 
			/* END */
			break; 
		} 
	} 

	if (strcmp(context, VEVENT) == 0) { 
		if (duration == 0) { 
			duration = dtend - dtstart; 
		} 
		do {
			dtend = dtstart + duration; 
			if (start <= dtstart && dtend <= end) {
				GROUP *group; 
				size_t day; 
				group = find_group(summary); 
				day = (size_t)((dtstart - start) / 86400); 
				sheet[group->row][day] += duration; 
			}
		} while ((dtstart = rrepeat(dtstart, &rrule)) != -1);
	}
} 




time_t rrepeat(time_t dtstart, struct rrule *rrule)
{
	static int *by_day;
	struct tm broken_down_time;
	static int count;

	if (dtstart > (*rrule).until || (*rrule).count > (*rrule).max_count) {
		return -1;
	}

	if (rrule->freq == YEARLY) {
		localtime_s(&broken_down_time, &dtstart);
		broken_down_time.tm_year += rrule->interval;
		dtstart = mktime(&broken_down_time);
	}

	else if (rrule->freq == MONTHLY) {
		localtime_s(&broken_down_time, &dtstart);
		broken_down_time.tm_mon += rrule->interval;
		dtstart = mktime(&broken_down_time);
	}

	else if (rrule->freq == WEEKLY) {
		/* if there are no by_day expansions, or we have
		 * past the end of the by_day array, add interval
		 * to the start date */
		if (rrule->by_day == 0 || *rrule->day == -1) {
			dtstart += rrule->interval * 7 * 24 * 60 * 60;
		}
		/* any by_day expansions? */
		if (rrule->by_day != 0) {
			/* if past the end, reset pointer */
			if (rrule->day == 0 || *rrule->day == -1) {
				rrule->day = rrule->by_day;
			}
			/* set date from by_day array and increment pointer */
			localtime_s(&broken_down_time, &dtstart);
			broken_down_time.tm_mday += (*rrule->day++ - broken_down_time.tm_wday);
			dtstart = mktime(&broken_down_time);
		}
	}

	else {
		return -1;
	}

	++rrule->count;

	return dtstart;	
}


void parse_rrule(const char *string, struct rrule *rrule)
{
	char *first, *rest, *name, *value, *s;
	const char **f, **d;

	rrule->max_count = 200; //default
	
	s = _strdup(string);
	rest = s;
	while ((first = strsplt(0, ";", &rest)) != 0) {
		name = strsplt(first, "=", &value);
		if (strcmp(name, FREQ) == 0) {
			const char *frq[] = { YEARLY, MONTHLY, WEEKLY, DAILY, 0 };
			for (f = frq; *f != 0; f++) {
				if (strcmp(value, *f) == 0) {
					rrule->freq = *f;
					break;
				}
			}
		} else if (strcmp(name, INTERVAL) == 0) {
			rrule->interval = atoi(value);
		} else if (strcmp(name, COUNT) == 0) {
			rrule->max_count = atoi(value);
		} else if (strcmp(name, UNTIL) == 0) {
			rrule->until = parse_date(value, true);
		} else if (strcmp(name, BYDAY) == 0) {
			const char *days[] = { "SU", "MO", "TU", "WE", "TH", "FR", "SA", 0 };
			char *first_day, *rest_days = value;
			int count_days = 0;
			while ((first_day = strsplt(0, ",", &rest_days)) != 0) {
				rrule->by_day = (int *)realloc(rrule->by_day, (count_days + 2) * sizeof(int));
				for (d = days; *d != 0; d++) {
					if (strcmp(first_day, *d) == 0) {
						rrule->by_day[count_days++] = (int)(d - days);
						break;
					}
				}
			}
			rrule->by_day[count_days] = -1;
			rrule->day = rrule->by_day;
		}
	}

	free(s);
}


time_t parse_date(const char *s, bool isdate) 
{ 
	struct tm tm = {0}; 
	int n; 

	n = sscanf_s(s, "%4d%2d%2dT%2d%2d%2d", &tm.tm_year, &tm.tm_mon, 
		&tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec); 

	tm.tm_year -= 1900;
	tm.tm_mon  -= 1;

	if (n == 6 || n == 3 && isdate) { 
		return mktime(&tm); 
	} else { 
		return -1; 
	} 
} 





time_t parse_duration(const char *s) 
{ 
	return 0; 
} 



char *content_disposition;
char *boundary;
char *control_name;

void parse_headers()
{
	/* parse headers */
	while ((lp = fgets(line, stdin, sizeof(line))) != 0 && strlen(line) > 0) {
		name = mystrtok(&lp, ": ");
		if (strcmp(name, CONTENT_DISPOSITION) == 0) {
			content_disposition = mystrtok(&lp, "; \r\n");
			while ((pname = mystrtok(&value, "=")) != 0) {
				pvalue = mystrtok(&value, "; ");
				if (strcmp(pvalue, NAME) == 0) {
					control = strdup(pvalue);
				}
			}
		}
	}
}

void process_form()
{
	while (fgets(line, stdin, sizeof(line))) {
		if (strlen(line) > 2 && line[0] == '-' && line[1] == '-' && strcmp(line + 2, boundary) == 0) {
		}
		/* parse value */
		
	}
}

/* The main program */


void set_option(struct option *opt)
{
	if (strcmp(opt->name, "start") == 0) {
		start = parse_date(opt->value, false);
	} else if (strcmp(opt->name, "end") == 0) {
		end = parse_date(opt->value, false);
	} else if (strcmp(opt->name, "file") == 0) {
		file = opt->filename;
	}
}

void insert_event(struct event *evt)
{
	GROUP *group; 
	size_t day; 
	
	group = find_group(evt->summary); 
	day = (size_t)((evt->dtstart - start) / 86400); 
	sheet[group->row][day] += evt->duration;
}

int main(int argc, char *argv[]) 
{
	GROUP *group;
	size_t col;
	char buffer[256];
	struct tm tm;

	if (argc < 3) {
		exit(EXIT_FAILURE);
	}

	start = parse_date(argv[1], true);
	end   = parse_date(argv[2], true);

	ncols = (size_t)((end - start) / 86400); /* number of days in report range */ 
	add_rows(256); /* allocate some rows for the timesheet */ 
	begin("<stdin>"); /* start parse */ 

	/* print timesheet */
	printf("%d\n", ncols);
	localtime_s(&tm, &start);
	asctime_s(buffer, sizeof(buffer), &tm);
	printf("%s", buffer);
	for (group = groups; group != 0; group = group->next) { 
		printf("%s", group->name); 
		for (col = 0; col < ncols; col++) { 
			printf(",%f", sheet[group->row][col] / 3600.0); 
		}
		printf("\n");
	} 

	
} 



