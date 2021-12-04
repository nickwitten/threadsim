/*
 * student.c
 * Multithreaded OS Simulation for CS 2200 and ECE 3058
 *
 * This file contains the CPU scheduler for the simulation.
 */

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os-sim.h"

//
unsigned int cpu_count;
int RR = 0;
int LR = 0;
int TSLICE = -1;
//

/** Function prototypes **/
extern void idle(unsigned int cpu_id);
extern void preempt(unsigned int cpu_id);
extern void yield(unsigned int cpu_id);
extern void terminate(unsigned int cpu_id);
extern void wake_up(pcb_t *process);


/*
 * current[] is an array of pointers to the currently running processes.
 * There is one array element corresponding to each CPU in the simulation.
 *
 * current[] should be updated by schedule() each time a process is scheduled
 * on a CPU.  Since the current[] array is accessed by multiple threads, you
 * will need to use a mutex to protect it.  current_mutex has been provided
 * for your use.
 */
static pcb_t **current;
static pthread_mutex_t current_mutex;


/*
 * schedule() is your CPU scheduler.  It should perform the following tasks:
 *
 *   1. Select and remove a runnable process from your ready queue which 
 *	you will have to implement with a linked list or something of the sort.
 *
 *   2. Set the process state to RUNNING
 *
 *   3. Call context_switch(), to tell the simulator which process to execute
 *      next on the CPU.  If no process is runnable, call context_switch()
 *      with a pointer to NULL to select the idle process.
 *	The current array (see above) is how you access the currently running process indexed by the cpu id. 
 *	See above for full description.
 *	context_switch() is prototyped in os-sim.h. Look there for more information 
 *	about it and its parameters.
 */
pcb_t *readyq = NULL;
static void schedule(unsigned int cpu_id)
{
    pcb_t* process;
    pthread_mutex_lock(&current_mutex);

        process = readyq;
        if (LR) {

            unsigned int longest_time;
            unsigned int time;
            pcb_t* prev_process = NULL;
            pcb_t* lrt_process = process;
            longest_time = (lrt_process != NULL) ? lrt_process->time_remaining : 0;
            pcb_t* lrt_prev_process = NULL;
            while (process != NULL) {
                time = process->time_remaining;
                if (time > longest_time) {
                    lrt_process = process;
                    lrt_prev_process = prev_process;
                    longest_time = time;
                }
                prev_process = process;
                process = process->next;
            }
            if (lrt_prev_process != NULL) {
                lrt_prev_process->next = lrt_process->next;
            } else {
                readyq = (lrt_process) ? lrt_process->next : NULL;
            }
            process = lrt_process;

        } else {
            while (process != NULL && process->state != PROCESS_READY) {
                process = process->next;
            }
            readyq = (process != NULL) ? process->next : NULL;
        }

        if (process) {
            process->state = PROCESS_RUNNING;
        }
        current[cpu_id] = process;

    pthread_mutex_unlock(&current_mutex);
    context_switch(cpu_id, process, TSLICE);

}


/*
 * idle() is your idle process.  It is called by the simulator when the idle
 * process is scheduled.
 *
 * This function should block until a process is added to your ready queue.
 * It should then call schedule() to select the process to run on the CPU.
 */
pthread_cond_t p_ready;
pthread_mutex_t idle_mutex;
extern void idle(unsigned int cpu_id)
{
    pthread_mutex_lock(&idle_mutex);
        while (readyq == NULL) {
            pthread_cond_wait(&p_ready, &idle_mutex);
        }
    pthread_mutex_unlock(&idle_mutex);
    schedule(cpu_id);
}


/*
 * preempt() is the handler called by the simulator when a process is
 * preempted due to its timeslice expiring.
 *
 * This function should place the currently running process back in the
 * ready queue, and call schedule() to select a new runnable process.
 */
extern void preempt(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
        pcb_t *process = current[cpu_id];
    pthread_mutex_unlock(&current_mutex);
    schedule(cpu_id);
    wake_up(process); 
}


/*
 * yield() is the handler called by the simulator when a process yields the
 * CPU to perform an I/O request.
 *
 * It should mark the process as WAITING, then call schedule() to select
 * a new process for the CPU.
 */
extern void yield(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
        pcb_t *process = current[cpu_id];
    pthread_mutex_unlock(&current_mutex);
    process->state = PROCESS_WAITING;
    schedule(cpu_id);
}


/*
 * terminate() is the handler called by the simulator when a process completes.
 * It should mark the process as terminated, then call schedule() to select
 * a new process for the CPU.
 */
extern void terminate(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
        pcb_t *process = current[cpu_id];
    pthread_mutex_unlock(&current_mutex);

    process->state = PROCESS_TERMINATED;
    schedule(cpu_id);
}


/*
 * wake_up() is the handler called by the simulator when a process's I/O
 * request completes.  It should perform the following tasks:
 *
 *   1. Mark the process as READY, and insert it into the ready queue.
 *
 *   2. If the scheduling algorithm is LRTF, wake_up() may need
 *      to preempt the CPU with lower remaining time left to allow it to
 *      execute the process which just woke up with higher reimaing time.
 * 	However, if any CPU is currently running idle,
* 	or all of the CPUs are running processes
 *      with a higher remaining time left than the one which just woke up, wake_up()
 *      should not preempt any CPUs.
 *	To preempt a process, use force_preempt(). Look in os-sim.h for 
 * 	its prototype and the parameters it takes in.
 */
extern void wake_up(pcb_t *process)
{
    process->state = PROCESS_READY;
    process->next = NULL;

    unsigned int least_time = process->time_remaining;
    int least_time_i = -1;
    pthread_mutex_lock(&current_mutex);

        if (LR) {
            for (unsigned int i=0; i < cpu_count; i++) {
                if (current[i]) {
                    if (current[i]->time_remaining < least_time) {
                        least_time = current[i]->time_remaining;
                        least_time_i = (int)i;
                    }
                }
            }
        }
    
        pcb_t* curr_pcb = readyq;
        if (curr_pcb == NULL) {
            readyq = process;
        } else {
            while (curr_pcb->next != NULL) {
                curr_pcb = curr_pcb->next;
            }
            curr_pcb->next = process;
        }

    pthread_mutex_unlock(&current_mutex);
    pthread_cond_signal(&p_ready);

    if (least_time_i != -1) {
        force_preempt((unsigned int)least_time_i);
    }
}


/*
 * main() simply parses command line arguments, then calls start_simulator().
 * You will need to modify it to support the -l and -r command-line parameters.
 */
int main(int argc, char *argv[])
{
    //unsigned int cpu_count;

    /* Parse command-line arguments */
    if (argc < 2)
    {
        fprintf(stderr, "Multithreaded OS Simulator\n"
            "Usage: ./os-sim <# CPUs> [ -l | -r <time slice> ]\n"
            "    Default : FIFO Scheduler\n"
	    "         -l : Longest Remaining Time First Scheduler\n"
            "         -r : Round-Robin Scheduler\n\n");
        return -1;
    }
    cpu_count = strtoul(argv[1], NULL, 0);

    if (argc > 2) {
        if (strcmp(argv[2], "-r") == 0) {
            if (argc != 4) {
                fprintf(stderr, "Multithreaded OS Simulator\n"
                    "Usage: ./os-sim <# CPUs> [ -l | -r <time slice> ]\n"
                    "    Default : FIFO Scheduler\n"
                "         -l : Longest Remaining Time First Scheduler\n"
                    "         -r : Round-Robin Scheduler\n\n");
                return -1;
            }
            RR = 1;
            TSLICE = atoi(argv[3]);
        } else if (strcmp(argv[2], "-l") == 0) {
            LR = 1;
        } else {
            fprintf(stderr, "Multithreaded OS Simulator\n"
             "Usage: ./os-sim <# CPUs> [ -l | -r <time slice> ]\n"
             "    Default : FIFO Scheduler\n"
            "         -l : Longest Remaining Time First Scheduler\n"
             "         -r : Round-Robin Scheduler\n\n");
            return -1;
        }
    }



    /* Allocate the current[] array and its mutex */
    current = malloc(sizeof(pcb_t*) * cpu_count);
    assert(current != NULL);
    pthread_mutex_init(&current_mutex, NULL);

    /* Start the simulator in the library */
    start_simulator(cpu_count);

    return 0;
}


