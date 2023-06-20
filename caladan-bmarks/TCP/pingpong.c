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
#include <runtime/tcp.h>

#include "../../rdma.h"

#define NETPERF_PORT 8400 


static struct netaddr raddr;
static struct netaddr caddr;
static int buf_size;


static inline time_t getTime(void){
    struct timeval st;
    gettimeofday(&st, NULL);
    return st.tv_usec;
}

static void do_client(void *arg){
	
	tcpconn_t *c;
	struct netaddr laddr;

    char* buf = malloc(buf_size);
    memset(buf, 0xAB, buf_size);

	laddr.ip = 0;
	laddr.port = 0;

	tcp_dial(laddr, raddr, &c);	

	tcp_write(c, buf, buf_size);
    tcp_read(c, buf, buf_size);

	
	uint64_t start = getTime();

    tcp_write(c, buf, buf_size);
    tcp_read(c, buf, buf_size);

	uint64_t end = getTime();


	log_info("Latency: %lu [us]", end - start);
    
    tcp_abort(c);
	tcp_close(c);
	free(buf);
}



static void do_server(void *arg)
{	

	struct netaddr laddr;
	tcpqueue_t *q;
    tcpconn_t* c;

	int ret;
    char* buf = malloc(buf_size);

	laddr.ip = 0;
	laddr.port = NETPERF_PORT;

	tcp_listen(laddr, 4096, &q);	
    tcp_accept(q, &c);

    tcp_read(c, buf, buf_size);  
	tcp_write(c, buf, buf_size);

	tcp_read(c, buf, buf_size);
    tcp_write(c, buf, buf_size);

    tcp_abort(c);
	tcp_close(c);
	free(buf);
}

int main(int argc, char *argv[]){

    int ret;
	uint32_t addr;
	thread_fn_t fn;

    if (argc < 4){
        printf("arg must be: [config_file] [mode] [buffer size]\n");
		return -EINVAL;
    }

    if (!strcmp(argv[2], "client")) {
		fn = do_client;
	} else if (!strcmp(argv[2], "server")) {
		fn = do_server;
	} else {
		printf("invalid mode '%s'\n", argv[2]);
		return -EINVAL;
	}


    raddr.ip = MAKE_IP_ADDR(192,168,1,3);
    raddr.port = NETPERF_PORT;

	caddr.ip = MAKE_IP_ADDR(192,168,1,7);
	caddr.port = NETPERF_PORT;

    buf_size = atoi(argv[3]);

    ret = runtime_init(argv[1], fn, NULL);
	if (ret) {
		printf("failed to start runtime\n");
		return ret;
	}

    return 0;
}
