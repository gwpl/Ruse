/* ruse - show memory usage and wall-clock time for a process
 *
 * Copyright 2017 Jan Moren
 *
 * This file is part of Ruse.
 *
 * Ruse is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Ruse is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Ruse.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <error.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>

#include "proc.h"
#include "options.h"
#include "output.h"

#define KB 1024
#define MAX(x,y) ((x) > (y) ? (x): (y))
#define CLOCKID CLOCK_REALTIME
#define SIG SIGUSR1

#ifdef DEBUG
double time_diff_micro(struct timespec *toc, struct timespec *tic) {
    return (1e6*(toc->tv_sec-tic->tv_sec)+
	    (toc->tv_nsec-tic->tv_nsec)/1e3);
}
#endif

/* Our defined timer */
timer_t timerid;
struct timespec tic, toc;

/* get the signal ID out from the callback */
volatile sig_atomic_t sigtype;


/* Signal handler. Accepts a timer signal with the right timer ID or a SIGCHLD
 * event.
*/
void
sighandler(int signal, siginfo_t *siginfo, void *uc) {
    
    if (signal==SIG &&
        *(timer_t*)(siginfo->si_value.sival_ptr) == timerid) {
	    sigtype = signal;
	
    } else if (signal==SIGCHLD) {
	sigtype=signal;

    } else {
	sigtype=0;
    }
}

/* Set up signal handling.
 *
 * Catch SIGCHLD so we know when our child process is done
 * Set up a periodic timer and catch SIGUSR1 each time it ticks.
*/

void
set_signals(int sectime) {

    struct sigaction sa_time, sa_chld;
    struct sigevent sev;
    struct itimerspec its; 

    /* Get child exit signals, but not stop or cont */
    sa_chld.sa_flags = SA_SIGINFO|SA_NOCLDSTOP;
    sa_chld.sa_sigaction = sighandler;
    sigemptyset(&sa_chld.sa_mask);
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
	perror("sigaction");
	exit(EXIT_FAILURE);
    }

    /* catch SIG from timer */
    sa_time.sa_flags = SA_SIGINFO;
    sa_time.sa_sigaction = sighandler;
    sigemptyset(&sa_time.sa_mask);
    if (sigaction(SIG, &sa_time, NULL) == -1) {
	perror("sigaction");
	exit(EXIT_FAILURE);
    }

    /* set up a timer */
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIG;
    sev.sigev_value.sival_ptr = &timerid;
    if (timer_create(CLOCKID, &sev, &timerid) == -1) {
	perror("timer_create");
	exit(EXIT_FAILURE);
    }

    /* Start the timer */
    its.it_value.tv_sec = sectime;
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;
    if (timer_settime(timerid, 0, &its, NULL) == -1){
	perror("timer_settime");
	exit(EXIT_FAILURE);
    }
}

int 
main(int argc, char *argv[])
{
    
    time_t t1,t2;
    size_t mem = 0;
    size_t rssmem = 0;
    sigset_t mask;
    sigset_t old_mask;


    syspagesize = getpagesize()/KB;

    /* block signals from the timer and the child until we're ready 
     * to receive them
    */
    sigemptyset(&mask);
    sigaddset(&mask, SIG);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &old_mask);

    options *opts = get_options(&argc, &argv);

    /* Time the process*/
    time(&t1);

    pid_t pid = fork();
    if (pid < 0)
    {
	error(EXIT_FAILURE, 0, "fork failed");
    }
    if (pid == 0)
    {
	/* We're the child */
	execvp(argv[0], &argv[0]);
	perror("execvp() failed");
	exit(EXIT_FAILURE);
    }

    /* We're the parent */

    mem = get_RSS(pid);
    print_header(opts);
    print_steps(opts, mem, 0); 

    set_signals(opts->time);

    while(1) {
	sigsuspend(&old_mask);

	if (sigtype == SIG) {
#ifdef DEBUG
	    clock_gettime(CLOCK_REALTIME, &tic);
#endif
	    rssmem = get_RSS(pid);
#ifdef DEBUG    
	    clock_gettime(CLOCK_REALTIME, &toc);
	    printf("mem: %li %.2fms\n", rssmem, time_diff_micro(&toc, &tic)/1000.0);
#endif
	    mem = MAX(mem, rssmem); 

	    if (opts->steps) {
		time(&t2);
		print_steps(opts, rssmem, (t2-t1)); 
	    }

	} else if (sigtype == SIGCHLD) {
	    break;	
	}
    }

    time(&t2);
    long int runtime = (t2-t1);
    int status;
    waitpid(pid, &status, 0);
    if (!opts->nosum) {
	print_summary(opts, mem, runtime);
    }
    if (!opts->nofile) {
	fclose(opts->fhandle);
    }
//    printf("Mem(MB): %li\nTime(s): %li\n", mem, runtime);

    return EXIT_SUCCESS;
}
