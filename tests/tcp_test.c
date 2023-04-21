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


#define NETPERF_PORT 8424
#define N 1000


static struct netaddr raddr;
static int buf_size;

static void do_client(void *arg){
	
	tcpconn_t *c;
	struct netaddr laddr;
	ssize_t ret;

    char* buf = malloc(buf_size);
	if(buf == NULL){
		printf("error!");
		return;
	}
    memset(buf, 0xAB, buf_size);
	laddr.ip = 0;
	laddr.port = 0;
	tcp_dial(laddr, raddr, &c);


    for(int i = 0; i < N; i++){
		ret = tcp_write(c, buf, buf_size); 
		if(ret < 0){
			continue;
		}
    }

	free(buf);
	tcp_abort(c);
	tcp_close(c);

}



static void do_server(void *arg)
{	

	struct netaddr laddr;
	tcpqueue_t *q;
	int ret;
    char* buf = malloc(buf_size);
    FILE *f = fopen("//home//blueadmin//caladan//bmarks//TCP//result.txt", "w");

	laddr.ip = 0;
	laddr.port = NETPERF_PORT;


	ret = tcp_listen(laddr, 4096, &q);

    tcpconn_t* c;
    ret = tcp_accept(q, &c);


    uint64_t read = 0;
	tcp_read(c, buf, buf_size);

	uint64_t start = microtime();

    for(int i = 1; i < N; i++){  
	   ret = tcp_read(c, buf, buf_size);  
	   if(ret < 0){
			continue;
       }
       read += ret;
	     
    }

	uint64_t end = microtime();

    fprintf(f, "%lf",(read * (8e-9))/((end - start) * (1e-6)));
    fclose(f);
    free(buf);

	tcp_abort(c);
	tcp_close(c);
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
    buf_size = atoi(argv[3]);

    ret = runtime_init(argv[1], fn, NULL);
	if (ret) {
		printf("failed to start runtime\n");
		return ret;
	}

    return 0;
}
