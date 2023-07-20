#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <sys/socket.h>
#include <arpa/inet.h>

#include <base/stddef.h>
#include <base/log.h>
#include <base/time.h>
#include <net/ip.h>
#include <sched.h>
#include <runtime/runtime.h>
#include <runtime/sync.h>
#include <runtime/udp.h>

#include <pthread.h>
#include <base/thread.h>

#include "../../rdma.h"

#define NETPERF_PORT 8424
#define N 1000

static int amount_nummbers = 10000;

static struct netaddr raddr;
static int buf_size;
static int amount_clients;
static int amount_workes;

static inline time_t getTime(void)
{
    struct timeval st;
    gettimeofday(&st, NULL);
    return st.tv_sec * (1e6) + st.tv_usec;
}

static void do_client(void *arg)
{

    udpconn_t *c;
    struct netaddr laddr;

    laddr.ip = 0;
    laddr.port = 0;

    udp_dial(laddr, raddr, &c);

    int *buf = calloc(buf_size, sizeof(int));

    for (int i = 0; i < buf_size; i++)
    {
        buf[i] = rand() % amount_nummbers;
    }

    for (int i = 0; i < N; i++)
    {
        udp_write(c, buf, buf_size * sizeof(int));
    }

    udp_close(c);
}

waitgroup_t wg;
waitgroup_t start_threads;

atomic64_t counter;
atomic64_t tp;
atomic64_t done;
atomic64_t start;
atomic64_t read_pl;

atomic64_t sum;

static void *read_from(void *connection)
{

    int ret;
    uint64_t local_sum;
    udpconn_t *c = (udpconn_t *)connection;
    int *buf = calloc(buf_size, sizeof(int));

    waitgroup_wait(&start_threads);
    atomic64_cmpxchg_val(&start, 0, getTime());

    while (atomic64_read(&counter) < N * amount_clients)
    {
        local_sum = 0;
        ret = udp_read(c, buf, buf_size * sizeof(int));
        if (ret >= 0)
        {
            atomic64_add_and_fetch(&read_pl, ret);
        }

        atomic64_add_and_fetch(&counter, 1);
        for (int k = 0; k < buf_size; k++)
        {
            local_sum += buf[k];
        }
        atomic64_add_and_fetch(&sum, local_sum);
    }

    if (!(atomic64_add_and_fetch(&done, 1) - 1))
    {
        waitgroup_done(&wg);
    }

    free(buf);
    return NULL;
}

static void do_server(void *arg)
{

    struct netaddr laddr;
    udpconn_t *c;
    struct resources res;
    int ret;

    laddr.ip = 0;
    laddr.port = NETPERF_PORT;

    atomic64_write(&counter, 0);
    atomic64_write(&tp, 0);
    atomic64_write(&done, 0);
    atomic64_write(&start, 0);
    atomic64_write(&read_pl, 0);
    atomic64_write(&sum, 0);

    waitgroup_init(&wg);
    waitgroup_add(&wg, 1);

    waitgroup_init(&start_threads);
    waitgroup_add(&start_threads, 1);

    setup_connection(1, "129.132.85.196", &res, sizeof(uint64_t));
    udp_listen(laddr, &c);

    int *buf = calloc(buf_size, sizeof(int));

    for (int i = 0; i < amount_workes; i++)
    {
        thread_spawn(read_from, c);
    }

    udp_read(c, buf, buf_size * sizeof(int));
    atomic64_add_and_fetch(&counter, 1);

    waitgroup_done(&start_threads);
    waitgroup_wait(&wg);

    uint64_t result = atomic64_read(&sum);
    memcpy((&res)->buf, &result, sizeof(uint64_t));

    if (post_send(&res, IBV_WR_RDMA_WRITE))
    {
        fprintf(stderr, "failed to post SR 3\n");
    }

    if (poll_completion(&res))
    {
        fprintf(stderr, "poll completion failed 3\n");
    }

    uint64_t a_end = getTime();

    uint64_t a_start = atomic64_read(&start);
    uint64_t a_read = atomic64_read(&read_pl);

    log_info("Throughput: %lf [Gbit/s]", (a_read * (8e-9)) / ((a_end - a_start) * (1e-6)));
    resources_destroy(&res);
    free(buf);
}

int main(int argc, char *argv[])
{

    int ret;
    uint32_t addr;
    thread_fn_t fn;

    if (argc < 6)
    {
        printf("arg must be: [config_file] [mode] [#buf_size] [#amount_clients] [#amount_workes]\n");
        return -EINVAL;
    }

    if (!strcmp(argv[2], "client"))
    {
        fn = do_client;
    }
    else if (!strcmp(argv[2], "server"))
    {
        fn = do_server;
    }
    else
    {
        printf("invalid mode '%s'\n", argv[2]);
        return -EINVAL;
    }

    raddr.port = NETPERF_PORT;
    raddr.ip = MAKE_IP_ADDR(192, 168, 1, 3);
    buf_size = atoi(argv[3]);
    amount_clients = atoi(argv[4]);
    amount_workes = atoi(argv[5]);

    ret = runtime_init(argv[1], fn, NULL);
    if (ret)
    {
        printf("failed to start runtime\n");
        return ret;
    }

    return 0;
}
