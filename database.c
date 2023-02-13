#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <time.h> 
#include <sys/shm.h>

#define LOW_TRESH 2
#define HIGH_TRESH 10

#define REQUEST 0
#define RESPONSE 1

#define KEY 232323

#define ITERS 5

const char* COLOR[10] = {"\033[0;31m", "\033[0;32m", "\033[0;33m", "\033[0;34m", "\033[0;35m", "\033[0;36m", "\033[0;31m", "\033[0;32m", "\033[0;33m", "\033[0;34m"};
const char* COLOR_END = "\033[0m";

//STRUCTS
struct entry{
    int id;
    int clock;
    int entry_ctr;
};

struct message{
    long mtype;
    int id;
    int clock;
};

//GLOBAL
int N;
int shm_id;
int enter_flg = 1; //Process wants to enter C.S

//PTRS
struct entry * db;
int* pipelines;
int *responses;

int bigger_priority(int clock_curr, int clock_mess, int id_o, int id_mess){
    return clock_curr > clock_mess || (clock_curr == clock_mess && id_o > id_mess);
}

int max(int a, int b){
    if(a > b) return a;
    return b;
}

void parse(){
    printf("Input number of processes. [%d,%d]: \n", LOW_TRESH, HIGH_TRESH);
    int tmp;
    scanf("%d", &tmp);
    if (tmp < LOW_TRESH || tmp > HIGH_TRESH){
        printf("Not in range [%d,%d]. Exiting.\n", LOW_TRESH, HIGH_TRESH);
        exit(1);
    }
    N = tmp;
}

void make_shared_mem(){
    //Get memory
    if ((shm_id = shmget(KEY, N * sizeof(struct entry), IPC_CREAT | 0600)) == -1) {
        printf("Couldn't get memory. Exiting.\n");
        exit(1);
    }
    //Attach memory
    db = (struct entry *) shmat(shm_id, NULL, 0);
    if (*((int *) db) == -1){
        printf("Couldn't attach. Exiting.\n");
        exit(1);
    }
}

void cleanup(){
    //Clean pointers and memory
    free(responses);
    free(pipelines);
    //fprintf(stdout, "\nResponses and freed.\n");

    //Shared memory deletion
    if  (shmdt(db) == -1){
        fprintf(stdout, "Couldn't free the memory. Exiting.\n");
        exit(1);
    }
    if(shmctl(shm_id, IPC_RMID, NULL) == -1){
        fprintf(stdout, "Couldn't free the  segment. Exiting.\n");
        exit(1);
    }

    fprintf(stdout, "\nShared memory deleted and segment freed.\n");
    exit(0);
}

void signal_prepare(){
    signal(SIGINT, cleanup);
}

void make_pipelines(){
    pipelines = (int *)malloc(2 * N * sizeof(int)); // two for process (r, w)
    for (int i=0; i<N; i++){
        int fd_iter[2];
        if (pipe(fd_iter) == -1){
            fprintf(stdout, "Error creating pipe. Exiting.\n");
            exit(1);
        }
        pipelines[2*i] = fd_iter[0]; //READ
        pipelines[2*i + 1] = fd_iter[1]; //WRITE
    }
}

void reqsnd(struct entry* entry){
    if(!enter_flg) //No need for entering C.S
        return;
    
    struct message mess = {
        REQUEST, entry->id, entry->clock
    };

    int id_o = entry->id;
    int clock_o = entry->clock;

    for (int i=0; i<N; i++){
        if (i != id_o){ // Same process no send
            if (write(pipelines[2*i + 1], &mess, sizeof(mess)) == -1){
                fprintf(stdout, "Error sending request. Exiting.\n");
                exit(1);
            }
            fprintf(stdout, "%sPROCESS %d (%d) sends REQUEST(%d,%d) to PROCESS %d.%s\n", COLOR[id_o], id_o, clock_o, id_o, clock_o, i, COLOR_END);
            fflush(stdout);
        }
    }
}

void respsnd(struct entry * entry, struct entry * entry_mess){
    struct message mess = {
        RESPONSE, entry->id, entry_mess->clock
    };

    if(write(pipelines[entry_mess->id * 2 + 1], &mess, sizeof(mess)) == -1){
        fprintf(stdout, "Error rending response. Exiting.\n");
        exit(1);
    }
}

void reqrecv(struct entry * entry){
    int res_ctr = 0;
    int clock_curr = entry->clock;
    int id_o = entry->id;

    struct message mess;

    for(;;){
        if(read(pipelines[2*id_o], &mess, sizeof(mess)) == -1){
            fprintf(stdout, "Error reading message. Exiting.\n");
            exit(1);
        }
        
        struct entry mess_entry = {mess.id, mess.clock, 0}; //dont need entries here so 0

        int id_mess = mess_entry.id;
        int clock_mess = mess_entry.clock;
        int clock_upd = max(clock_curr, clock_mess) + 1; //updating clock

        fprintf(stdout, "%sPROCESS %d recieves REQUEST(%d,%d) from PROCESS %d. Clock is max(%d,%d)+1=%d%s\n", COLOR[id_o], id_o, id_mess, clock_mess, id_mess, clock_curr, clock_mess, clock_upd, COLOR_END);
        fflush(stdout);

        //did i recieve REQ or RES
        if(mess.mtype == REQUEST){
            // if process doestn want or cant enter bc of priority
            if(!enter_flg || bigger_priority(clock_curr, clock_mess, id_o, id_mess)){
                respsnd(entry, &mess_entry);
                fprintf(stdout, "%sPROCESS %d send RESPONSE(%d,%d) to PROCESS %d.%s\n", COLOR[id_o], id_o, id_o, clock_mess, id_mess, COLOR_END);
                fflush(stdout);
            }
            else{ // i want to go into C.S to i just save response
                responses[id_mess] = mess.clock;
                fprintf(stdout, "%sPROCESS %d saves request.%s\n", COLOR[id_o], id_o, COLOR_END);
                fflush(stdout);
            }
        }
        else{
            res_ctr++;
            fprintf(stdout, "%sPROCESS %d recieves RESPONSE(%d,%d) from %d. Clock is max(%d,%d)+1=%d. %d responses.%s\n", COLOR[id_o], id_o, id_mess, clock_mess, id_mess, clock_curr, clock_mess, clock_upd, res_ctr, COLOR_END);
            fflush(stdout);
        }
        entry->clock = clock_upd;
        if(res_ctr == N - 1) break;
    }
}

void critical(struct entry * entry){
    fprintf(stdout, "%sPROCESS %d enters CRITICAL.%s\n", COLOR[entry->id], entry->id, COLOR_END);
    fflush(stdout);

    for(int i=0; i<N; i++){
        struct entry * entry_iter = db + i;
        if(i == entry->id){
            entry_iter->id = entry->id;
            entry_iter->clock = entry->clock;
            entry_iter->entry_ctr++;

            if(entry_iter->entry_ctr == ITERS){
                enter_flg = 0;
            }
        }
        fprintf(stdout, "P%d  c =%2d  entries=%d\n", entry_iter->id, entry_iter->clock, entry_iter->entry_ctr);
        fflush(stdout);
    }
    usleep(100000 + rand() % 1900000);
    fprintf(stdout, "%sPROCESS %d exits CRITICAL.\n%s", COLOR[entry->id], entry->id, COLOR_END);
    fflush(stdout);
}

void send_resp_to_waiting(struct entry * entry){
    for(int i=0; i<N; i++){
        if (i != entry->id){
            int clock_r = responses[i];
            if (clock_r == 0) continue;

            struct entry entry_r = {
                i,
                clock_r,
                0 //doestn matter
            };

            respsnd(entry, &entry_r);
            responses[i] = 0;

            fprintf(stdout, "%sPROCESS %d sends RESPONSE(%d,%d) to PROCESS %d%s.\n", COLOR[entry->id], entry->id, entry->id, clock_r, i, COLOR_END);
            fflush(stdout);
        }
    }
}


void do_process(int i){
    srand(time(0) + i); //To avoid same sleep times

    responses = (int *) malloc(N * sizeof(int));

    struct entry entry = {i, rand() % 10 + 1, 0}; // id, clk, entries
    db[i] = entry;

    for(;;){
        reqsnd(&entry);
        reqrecv(&entry);
        critical(&entry);
        send_resp_to_waiting(&entry);
    }
}


int main(void){
    //Signal for cleanup
    signal_prepare(); 
    //Num of processes
    parse();
    //Make shared memory
    make_shared_mem();
    //Make and store descriptors
    make_pipelines();

    //Fork processes
    for (int i = 0; i < N; i++){
        pid_t pid = fork();

        if (pid == 0){
            do_process(i);
        }
        else if(pid == -1){
            fprintf(stdout ,"Error PID.\n");
        }
    }

    while (wait(NULL) > 0);
    cleanup();
}