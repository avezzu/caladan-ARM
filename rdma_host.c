#include "rdma.h"

void send_result(struct resources* res, void* data, int data_size){
   
    /* Now the client performs an RDMA read and then write on server.
       Note that the server has no idea these events have occured */
    if (config.server_name)
    {

        /* Now we replace what's in the server's buffer */
        memcpy(res->buf, data, data_size);
        if (post_send(res, IBV_WR_RDMA_WRITE))
        {
            fprintf(stderr, "failed to post SR 3\n");
            return;
        }
        if (poll_completion(res))
        {
            fprintf(stderr, "poll completion failed 3\n");
            return;
        }
    }
}

int main(int argc, char *argv[]){
    
    int N;
    
    if(argc < 2){
        printf("arg must be: [#amount_workes]\n");
        return -1;
    }

    N = atoi(argv[1]);
    int size = 10000;
    config.dev_name = "mlx5_0";

    struct resources res[N];
    for(int i = 0; i < N; i++){
        config.tcp_port += i;
        setup_connection(0, "", &res[i], sizeof(uint64_t) * size);
    }
    
    printf("Connection has been established. [Ctrl+C to quit]");
    while(1);

    return 0;
}

