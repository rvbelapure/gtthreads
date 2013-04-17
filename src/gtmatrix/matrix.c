#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <setjmp.h>
#include <signal.h>

#include <gt_thread.h>
#include <gt_signal.h>

/* comment this out to make the application single threaded */
#define USE_GTTHREADS

int matrix_size = 512;
int cpu_count = 0;
int greedy_thread_count = 0;
int total_thread_count = 0;
char sched_switch = 'p';
extern pid_t *k_tid;
extern int k_count;

typedef struct square_matrix {
	int **buf;
	int size;
} square_matrix_t;

typedef struct uthread_arg {
	square_matrix_t *a;
	square_matrix_t *b;
	square_matrix_t *c;
	uthread_attr_t *attr;
	uthread_tid tid;
	uthread_pid pid;
	struct timeval create_time;
	struct timeval start_time;
	struct timeval end_time;
} uthread_arg_t;

/* malloc that fails */
static inline void *emalloc(size_t size)
{
	void *p = malloc(size);
	if (!p) {
		fprintf(stderr, "Malloc failure");
		exit(EXIT_FAILURE);
	}
	return p;
}

/* prints a matrix; for debugging */
static void print_matrix(square_matrix_t *m)
{
	int i, j;
	for (i = 0; i < m->size; i++) {
		for (j = 0; j < m->size; j++)
			printf(" %d ", m->buf[i][j]);
		printf("\n");
	}
	return;
}

/* converts a timeval to integral microseconds */
static inline unsigned long tv2us(struct timeval *tv)
{
	return (tv->tv_sec * 1000000) + tv->tv_usec;
}

/* converts an integer in microseconds to a struct timeval */
static inline void us2tv(unsigned long us, struct timeval *tv)
{
	tv->tv_sec = us / 1000000;
	tv->tv_usec = us % 1000000;
}

/* formats and prints the timeval into up to `n` characters of string `s`.
 * Returns as in sprintf() */
int timeval_snprintf(char *s, int n, struct timeval *tv)
{
	return snprintf(s, n, "%ld.%06ld", tv->tv_sec, tv->tv_usec);
}

/* performs "final - initial", puts result in `result`. returns 1 if answer is negative,
 * 0 otherwise. taken from gnu.org */
static int timeval_subtract(struct timeval *result, struct timeval *final,
                            struct timeval *initial)
{
	/* Perform the carry for the later subtraction by updating y. */
	if (final->tv_usec < initial->tv_usec) {
		int nsec = (initial->tv_usec - final->tv_usec) / 1000000 + 1;
		initial->tv_usec -= 1000000 * nsec;
		initial->tv_sec += nsec;
	}
	if (final->tv_usec - initial->tv_usec > 1000000) {
		int nsec = (final->tv_usec - initial->tv_usec) / 1000000;
		initial->tv_usec += 1000000 * nsec;
		initial->tv_sec -= nsec;
	}

	/* Compute the time remaining to wait. tv_usec is certainly positive. */
	result->tv_sec = final->tv_sec - initial->tv_sec;
	result->tv_usec = final->tv_usec - initial->tv_usec;

	/* Return 1 if result is negative. */
	return final->tv_sec < initial->tv_sec;
}

/* Alloc's and initializes a struct matrix */
static square_matrix_t *matrix_create(int size, int val)
{
	int i, j;
	square_matrix_t *m = emalloc(sizeof(*m));
	m->size = size;

	m->buf = emalloc(sizeof(*m->buf) * size);
	for (i = 0; i < m->size; i++) {
		m->buf[i] = emalloc(sizeof(**m->buf) * size);
	}

	for (i = 0; i < m->size; i++) {
		for (j = 0; j < m->size; ++j) {
			m->buf[i][j] = val;
		}
	}
	return m;
}

static int mulmat(void *arg_)
{
	uthread_arg_t *arg = arg_;
	gettimeofday(&arg->start_time, NULL);

	square_matrix_t *a, *b, *c;
	a = arg->a;
	b = arg->b;
	c = arg->c;

	int size = a->size; // should be the same for all arrays
	for (int i = 0; i < size; ++i) {
		if (!(i % 100))
			printf("working...\n");
		for (int j = 0; j < size; ++j) {
			for (int k = 0; k < size; ++k) {
				c->buf[i][j] += a->buf[i][k] * b->buf[k][j];
			}
		}
	}

	gettimeofday(&arg->end_time, NULL);
	return 0;
}

int main(int argc, char **argv)
{
	cpu_count = (int) sysconf(_SC_NPROCESSORS_CONF);
	if(argc <= 1 || argc > 4)
	{		
		printf("\n\nUsage: prompt> %s <p/t> <extra_thread_count_in_greedy> <matrix_size>\n",argv[0]);
		printf("\np/t indicate the process level or thread level scheduling\n");
		printf("default extra thread count = # of optimal threads ...(greedy = 2 * optimal)\ndefault matrix size = 512\n");
		printf("Please view README for more details\n\n\n");
		exit(1);
	}
	if(argc == 2)
	{
		sched_switch = argv[1][0];
		greedy_thread_count = -1;
		matrix_size = 512;
		printf("Assuming matrix size = 512\n");	
		printf("Assuming extra thread count = %d\n",cpu_count);
	}
	else if(argc == 3)
	{
		sched_switch = argv[1][0];
		greedy_thread_count = atoi(argv[2]);
		matrix_size = 512;
		printf("Assuming matrix size = 512\n");	
	}
	else if(argc == 4)
	{	
		sched_switch = argv[1][0];
		matrix_size = atoi(argv[3]);
		greedy_thread_count = atoi(argv[2]);
	}
	/* Take these values from args */
	if(greedy_thread_count < 0)
	{
		greedy_thread_count = cpu_count;
	}
	total_thread_count = (2 * cpu_count) + greedy_thread_count;

	printf("# of threads in optimal process = %d\n",cpu_count);
	printf("# of threads in greedy process = %d\n",total_thread_count - cpu_count);
	if(total_thread_count > MAX_THREAD_COUNT)
	{
		int max = MAX_THREAD_COUNT;
		printf("Total number of threads greater than GTLibrary limit : %d\n",max);
		printf("Details : Optimal = %d, Greedy = %d, Total = %d\n",cpu_count, (total_thread_count - cpu_count),total_thread_count);
		exit(1);
	}

	uthread_arg_t thread_args[total_thread_count];

	uthread_arg_t *thread_arg = thread_args;

	for(int i = 0 ; i < total_thread_count ; i++)
	{
		int val = random();
		thread_arg->a = matrix_create(matrix_size, val);
		thread_arg->b = matrix_create(matrix_size, val);
		thread_arg->c = matrix_create(matrix_size, val);
		thread_arg->attr = uthread_attr_create();
		uthread_attr_init(thread_arg->attr);
		thread_arg++;
	}

	for(int i = 0 ; i < 2 ; i++)
	{
		processes[i].pid = i;
		processes[i].is_greedy = i;
		processes[i].thread_count = 0;
	}

	gtthread_options_t opt;
	gtthread_options_init(&opt);
	opt.scheduler_type = SCHEDULER_CFS;	// completely fair
	gtthread_app_init(&opt);

//	sig_install_handler_and_unblock(SIGUSR2,finalize);
	struct timeval app_start_time, app_end_time, app_elapsed_time;
	gettimeofday(&app_start_time, NULL);
	uthread_tid tmptid = 0;
	for(tmptid=0 ; tmptid < cpu_count ; tmptid++)	// threads for optimal processes
	{
		(thread_args[tmptid]).pid =  processes[0].is_greedy ? 1 : 0;
		gettimeofday(&thread_args[tmptid].create_time, NULL);
		printf("creating uthread\n");
		uthread_create(thread_args[tmptid].pid, &thread_args[tmptid].tid, thread_args[tmptid].attr, &mulmat, &thread_args[tmptid]);
	}

	for ( ; tmptid < total_thread_count ; tmptid++) 	// threads for greedy processes
	{
		thread_args[tmptid].pid = processes[1].is_greedy ? 1 : 0;
		gettimeofday(&thread_args[tmptid].create_time, NULL);
		printf("creating uthread\n");
		uthread_create(thread_args[tmptid].pid, &thread_args[tmptid].tid, thread_args[tmptid].attr, &mulmat, &thread_args[tmptid]);
	}
	gtthread_app_exit();

	gettimeofday(&app_end_time, NULL);

	/* use as tmp storage for printing timevals */
	int time_str_size = 1024;
	char time_str[time_str_size];

	/* get total application time */
	timeval_subtract(&app_elapsed_time, &app_end_time, &app_start_time);
	timeval_snprintf(time_str, time_str_size, &app_elapsed_time);
	printf("Total application elapsed time: %s s\n", time_str);

	/* all times in microseconds */
	unsigned long thread_cpu_times[total_thread_count];
	unsigned long thread_elapsed_times[total_thread_count];

	struct timeval thread_cpu_time, thread_elapsed_time;


	for(int p = 0 ; p < 2 ; p++)
		for(int i = 0 ; i < processes[p].thread_count ; i++)
				gettimeofday(&((uthread_arg_t *)((processes[p].threads[i])->arg))->end_time, NULL);

	for(int t = 0 ; t < total_thread_count ; t++)
	{
		thread_arg = &thread_args[t];
		/* Wall time as seen by the thread being scheduled */
		timeval_subtract(&thread_elapsed_time,
				 &thread_arg->end_time,
				 &thread_arg->start_time);

		/* CPU time as calculated by gtthreads */
		uthread_attr_getcputime(thread_arg->attr,
					&thread_cpu_time);

		thread_cpu_times[t] = tv2us(&thread_cpu_time);
		thread_elapsed_times[t] = tv2us(&thread_elapsed_time);

		printf("Thread %3d CPU time: %7lu us, elapsed time: %10lu us\n",
		       thread_args[t].tid,
		       tv2us(&thread_cpu_time),
		       tv2us(&thread_elapsed_time));

	}

	unsigned long time_p1_cpu = 0 , time_p2_cpu = 0 , time_p1_elapsed = 0, time_p2_elapsed = 0;
	
	for(int i = 0 ; i < cpu_count ; i++)
	{
		time_p1_cpu += thread_cpu_times[i];
		time_p1_elapsed += thread_elapsed_times[i];
	}

	for(int i = cpu_count ; i < total_thread_count ; i++)
	{		
		time_p2_cpu += thread_cpu_times[i];
		time_p2_elapsed += thread_elapsed_times[i];
	}

	printf("\n\nProcess 0: Optimal process, threadcount = %d\n",cpu_count);
	printf("CPU Time : %lu us, elapsed time : %lu us\n",time_p1_cpu,time_p1_elapsed);
	printf("Process 1: Greedy process, threadcount= %d\n",(total_thread_count - cpu_count));
	printf("CPU Time : %lu us, elapsed time : %lu us\n",time_p2_cpu,time_p2_elapsed);
	return 0;
}


