/* proc.c - helper functions for the /proc filesystem
 *
 * 2017 Jan Moren
 *
 */
#include "proc.h"

int syspagesize=0;


typedef struct {
    int pid;
    int parent;
    long int rss;
} procstr;

bool
read_RSS(int pid, long int *rss, int *parent) {

    int i;
    int res;
    char *line = NULL;
    size_t len=1023;
    char *field;
    char *fname;
    FILE *f;
    
    res = asprintf(&fname, "/proc/%i/stat", pid);
    if (res == -1) {
	fprintf(stderr, "Failed to convert proc file name\n");
	return false;
    }
    
    f = fopen(fname, "r");
    if (!f) {
	fprintf(stderr, "Failed to open '%s': %s\n", fname, strerror(errno));
	return false;
    }

    if (getline(&line, &len, f) == -1) {
	fprintf(stderr, "Failed to read '%s': %s\n", fname, strerror(errno));
	return false;
    }
    
    char *line_tmp = line;

    for(i=0; i<24; i++) {
	field = strsep(&line_tmp, " ");
	switch(i) {
	    case 3:	// parent
		res = sscanf(field, "%i", parent);
		continue;
	    case 23:	// rss
		res = sscanf(field, "%li", rss);
		continue;
	}
	if (res == EOF) {
	    fprintf(stderr, "Failed to scan '%s': %s\n", fname, strerror(errno));
	    return false;
	}
    }

    if(line)
	free(line);
    
    *rss = (*rss)*syspagesize;
    return true;
}

iarr *
get_all_pids() {
    
    DIR *df;
    struct dirent *dir;
    long int res;
    char *eptr;
    iarr *plist;

    if ((df = opendir("/proc")) == NULL){
	perror("get_all_pids:");
	exit(EXIT_FAILURE);
    }
    
    if ((plist = iarr_create(4)) == NULL) {
	exit(EXIT_FAILURE);
    }

    while ((dir = readdir(df)) != NULL) {
	
	if (dir->d_type != DT_DIR) {
	    continue;
	}
	
	/* any entry in /proc that begins with a digit is _probably_ a pid.
	 * But I've not seen a formal guarantee, so let's play it safe 
	*/
	 
	// begins with a digit? 
	if ((dir->d_name[0]<'0') || (dir->d_name[0]>'9')) {
	    continue;
	}

	res = strtol(dir->d_name, &eptr, 10);
	
	// not purely numeric, so not a pid
	if (*eptr != '\0') {
	    continue;
	}
	
	if (iarr_insert(plist, (int)res) == false) {
	    exit(EXIT_FAILURE);
        }
    }
//    for (int i = 0; i<plist->len; i++) {
//	printf("%d\n", plist->ilist[i]);
//    }
   
    return plist;
}


int
get_all_procs(procdata *procs, iarr *plist) {
  
    int elems;
    int pidc, procc;
    long int rss;
    int parent;
    int pid;
	
    elems = plist->len;

    // look up, fill in data for all procs
    
    pidc = 0;
    procc = 0;
    while (pidc<elems) {
	pid = plist->ilist[pidc];
	read_RSS(pid, &rss, &parent);

	// filter out kernel processes
	if (parent != 2) {
	    procs[procc].pid = pid;
	    procs[procc].rss = rss;
	    procs[procc].parent = parent;
	    //printf("%d \t%ld \t%d\n", procs[procc].pid, procs[procc].rss,procs[procc].parent);
	    procc++;
	}
	pidc++; 
    }
    return procc;
}


long int
get_RSS(int pid) {

    int elems;
    iarr *plist;
    procdata *procs;

    if ((plist = get_all_pids()) == NULL) {
	exit(EXIT_FAILURE);
    }
    if (plist->len < 1) {
	fprintf(stderr, "failed to get process pid list.\n");
	exit(EXIT_FAILURE);
    }

    if ((procs = calloc(plist->len, sizeof(procdata))) == NULL) {
	perror("get_all_procs");
	exit(EXIT_FAILURE);
    }

    elems = get_all_procs(procs, plist);
    for (int i=0; i<elems; i++) {
	printf("sdfsdfdsf %d \t%ld \t%d\n", procs[i].pid, procs[i].rss,procs[i].parent);
    }
    printf("total: %d \t elems: %d\n", plist->len, elems);

    return 1;
}
