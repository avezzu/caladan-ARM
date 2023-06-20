#include "rdma.h"

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
    
    fprintf(stdout, "Connection has been established. [Ctrl+C to quit]\n");
    while(1);

    return 0;
}

