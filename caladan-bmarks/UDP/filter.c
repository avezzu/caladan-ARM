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

#define NETPERF_PORT 8400
#define N 1000

static struct netaddr raddr;
static int buf_size;
static int amount_clients;
static int amount_workes;

#define KEY_SIZE 8
#define TOT_WORDS 10000
#define SEED 6598736

#define FAST_MOD(a, b) ((a) & ((b)-1))

static inline time_t getTime(void)
{
    struct timeval st;
    gettimeofday(&st, NULL);
    return st.tv_sec * (1e6) + st.tv_usec;
}

uint16_t hashmult(const uint16_t key)
{
    uint16_t hash = 0;
    uint8_t *key_byte = (uint8_t *)&key;
    for (uint16_t i = 0; i < sizeof(uint16_t); i++)
    {
        hash = hash * 31 + (key_byte[i]);
    }
    return hash;
}

void fill_htable(uint32_t *vec, uint32_t length)
{
    for (uint32_t i = 0; i < length; i++)
    {
        vec[hashmult((uint16_t)i)] = i;
    }
}

#define HASH_JEN_MIX(a, b, c) \
    do                        \
    {                         \
        a -= b;               \
        a -= c;               \
        a ^= (c >> 13);       \
        b -= c;               \
        b -= a;               \
        b ^= (a << 8);        \
        c -= a;               \
        c -= b;               \
        c ^= (b >> 13);       \
        a -= b;               \
        a -= c;               \
        a ^= (c >> 12);       \
        b -= c;               \
        b -= a;               \
        b ^= (a << 16);       \
        c -= a;               \
        c -= b;               \
        c ^= (b >> 5);        \
        a -= b;               \
        a -= c;               \
        a ^= (c >> 3);        \
        b -= c;               \
        b -= a;               \
        b ^= (a << 10);       \
        c -= a;               \
        c -= b;               \
        c ^= (b >> 15);       \
    } while (0)

#define HASH_JEN(key, keylen, hashv)                                                                                             \
    do                                                                                                                           \
    {                                                                                                                            \
        unsigned _hj_i, _hj_j, _hj_k;                                                                                            \
        unsigned const char *_hj_key = (unsigned const char *)(key);                                                             \
        hashv = 0xfeedbeefu;                                                                                                     \
        _hj_i = _hj_j = 0x9e3779b9u;                                                                                             \
        _hj_k = (unsigned)(keylen);                                                                                              \
        while (_hj_k >= 12U)                                                                                                     \
        {                                                                                                                        \
            _hj_i += (_hj_key[0] + ((unsigned)_hj_key[1] << 8) + ((unsigned)_hj_key[2] << 16) + ((unsigned)_hj_key[3] << 24));   \
            _hj_j += (_hj_key[4] + ((unsigned)_hj_key[5] << 8) + ((unsigned)_hj_key[6] << 16) + ((unsigned)_hj_key[7] << 24));   \
            hashv += (_hj_key[8] + ((unsigned)_hj_key[9] << 8) + ((unsigned)_hj_key[10] << 16) + ((unsigned)_hj_key[11] << 24)); \
                                                                                                                                 \
            HASH_JEN_MIX(_hj_i, _hj_j, hashv);                                                                                   \
                                                                                                                                 \
            _hj_key += 12;                                                                                                       \
            _hj_k -= 12U;                                                                                                        \
        }                                                                                                                        \
        hashv += (unsigned)(keylen);                                                                                             \
        switch (_hj_k)                                                                                                           \
        {                                                                                                                        \
        case 11:                                                                                                                 \
            hashv += ((unsigned)_hj_key[10] << 24); /* FALLTHROUGH */                                                            \
        case 10:                                                                                                                 \
            hashv += ((unsigned)_hj_key[9] << 16); /* FALLTHROUGH */                                                             \
        case 9:                                                                                                                  \
            hashv += ((unsigned)_hj_key[8] << 8); /* FALLTHROUGH */                                                              \
        case 8:                                                                                                                  \
            _hj_j += ((unsigned)_hj_key[7] << 24); /* FALLTHROUGH */                                                             \
        case 7:                                                                                                                  \
            _hj_j += ((unsigned)_hj_key[6] << 16); /* FALLTHROUGH */                                                             \
        case 6:                                                                                                                  \
            _hj_j += ((unsigned)_hj_key[5] << 8); /* FALLTHROUGH */                                                              \
        case 5:                                                                                                                  \
            _hj_j += _hj_key[4]; /* FALLTHROUGH */                                                                               \
        case 4:                                                                                                                  \
            _hj_i += ((unsigned)_hj_key[3] << 24); /* FALLTHROUGH */                                                             \
        case 3:                                                                                                                  \
            _hj_i += ((unsigned)_hj_key[2] << 16); /* FALLTHROUGH */                                                             \
        case 2:                                                                                                                  \
            _hj_i += ((unsigned)_hj_key[1] << 8); /* FALLTHROUGH */                                                              \
        case 1:                                                                                                                  \
            _hj_i += _hj_key[0];                                                                                                 \
        }                                                                                                                        \
        HASH_JEN_MIX(_hj_i, _hj_j, hashv);                                                                                       \
    } while (0)

static void do_client(void *arg)
{

    udpconn_t *c;
    struct netaddr laddr;

    uint32_t *buf = malloc(buf_size * sizeof(uint32_t));
    if (buf == NULL)
    {
        printf("error!");
        return;
    }

    memset(buf, 0xAB, buf_size * sizeof(uint32_t));

    laddr.ip = 0;
    laddr.port = 0;
    udp_dial(laddr, raddr, &c);

    srand(SEED);
    for (int i = 0; i < N; i++)
    {
        for (int i = 0; i < KEY_SIZE / sizeof(uint32_t); i++)
        {
            ((uint32_t *)buf)[i] = rand();
        }
        udp_write(c, buf, buf_size);
    }

    free(buf);
    udp_close(c);
}

waitgroup_t wg;
waitgroup_t start_threads;

atomic64_t counter;
atomic64_t tp;
atomic64_t done;
atomic64_t start;
atomic64_t end;
atomic64_t read_pl;

struct input
{
    udpconn_t *c;
    struct resources res;
};

static void *read_from(void *connection)
{

    struct input *conn = (struct input *)connection;
    int ret;
    int first = 1;

    udpconn_t *c = conn->c;
    struct resources res = conn->res;

    char *buf = malloc(buf_size);

    uint32_t htable[TOT_WORDS];
    fill_htable(htable, TOT_WORDS);

    waitgroup_wait(&start_threads);
    atomic64_cmpxchg_val(&start, 0, getTime());

    while (atomic64_read(&counter) < N * amount_clients)
    {

        ret = udp_read(c, buf, buf_size);
        if (ret < 0)
        {
            atomic64_add_and_fetch(&counter, 1);
            continue;
        }
        atomic64_add_and_fetch(&read_pl, ret);
        uint32_t hash;
        HASH_JEN(buf, KEY_SIZE, hash);
        hash = FAST_MOD(hash, TOT_WORDS);

        if (!htable[hash])
        {
            if (first)
            {
                first = 0;
                memcpy(res.buf, buf, buf_size);
                if (post_send(&res, IBV_WR_RDMA_WRITE))
                {
                    fprintf(stderr, "failed to post SR 3\n");
                }
            }
            else
            {
                send_result(&res, buf, buf_size);
            }
        }

        atomic64_add_and_fetch(&counter, 1);
    }

    atomic64_cmpxchg_val(&end, 0, getTime());

    if (!atomic64_read(&done))
    {
        atomic64_add_and_fetch(&done, 1);
        waitgroup_done(&wg);
    }

    free(buf);
    return NULL;
}

static void do_server(void *arg)
{

    struct netaddr laddr;

    udpconn_t *c;
    int ret;

    laddr.ip = 0;
    laddr.port = NETPERF_PORT;

    atomic64_write(&counter, 0);
    atomic64_write(&tp, 0);
    atomic64_write(&done, 0);
    atomic64_write(&start, 0);
    atomic64_write(&end, 0);
    atomic64_write(&read_pl, 0);

    waitgroup_init(&wg);
    waitgroup_add(&wg, 1);

    waitgroup_init(&start_threads);
    waitgroup_add(&start_threads, 1);

    udp_listen(laddr, &c);

    char *buf = malloc(buf_size);

    struct input in[amount_workes];
    for (int i = 0; i < amount_workes; i++)
    {
        config.tcp_port += i;
        setup_connection(1, "129.132.85.196", &in[i].res, buf_size);
        in[i].c = c;
        sleep(1);
    }

    for (int i = 0; i < amount_workes; i++)
    {
        thread_spawn(read_from, &in[i]);
    }

    udp_read(c, buf, buf_size);
    atomic64_add_and_fetch(&counter, 1);

    waitgroup_done(&start_threads);
    waitgroup_wait(&wg);

    uint64_t a_start = atomic64_read(&start);
    uint64_t a_end = atomic64_read(&end);
    uint64_t a_read = atomic64_read(&read_pl);

    log_info("Throughput: %lf [Gbit/s]", (a_read * (8e-9)) / ((a_end - a_start) * (1e-6)));
    for (int i = 0; i < amount_workes; i++)
    {
        resources_destroy(&in[i].res);
    }
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
