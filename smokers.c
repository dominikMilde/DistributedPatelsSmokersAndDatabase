#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <time.h> 

#define SALESMAN 3 //for color
#define SIGNAL_CLEAR 789 // signal that ingredients are taken from the counter

// 2 message queues
#define MQ_ing_key 13131   
#define MQ_badge_key 14141

#define SLEEP_OUTPUT 2

struct msgbuf_ingredient {
    long mtype;
    int msg;
};

struct msgbuf_badge {
    long mytype;
    int msg;
};

// Function declarations
void smoker(int, int, int);
void agent(int, int);

const char* INGREDIENTS[3] = {"PAPER and MATCHES", "TOBACCO and MATCHES", "TOBACCO and PAPER"};
const char* INGREDIENTS_EACH[3] = {"TOBACCO", "PAPER", "MATCHES"};

const char* COLOR[4] = {"\033[0;34m", "\033[0;35m", "\033[0;31m", "\033[0;32m"};

const char* COLOR_END = "\033[0m";


int main()
{
    srand(time(0));

    for (int i = 0; i<3; i++){
        printf("SMOKER %d has %s\n", i+1, INGREDIENTS_EACH[i]);
    }

    printf("\n------------------------------------------------\n\n");
    
    int mq_badge, mq_ing;

    // Create 2 message queues
    if ((mq_badge = msgget(MQ_ing_key, IPC_CREAT | 0600)) == -1){
        perror("msgget");
        exit(1);
    }
    if ((mq_ing = msgget(MQ_badge_key, IPC_CREAT | 0600)) == -1){
        perror("msgget");
        exit(1);
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        agent(mq_badge, mq_ing);
        exit(0);
    }

    // Create 3 smokers
    for (int i = 0; i < 3; i++)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            smoker(i, mq_badge, mq_ing);
            exit(0);
        }
    }

    // Wait for all processes to finish
    while (wait(NULL) > 0);

    // Destroy message queues
    msgctl(mq_badge, IPC_RMID, NULL);
    msgctl(mq_ing, IPC_RMID, NULL);

    return 0;
}

// Function definitions
void smoker(int smoker_id, int mq_badge, int mq_ing)
{
    struct msgbuf_badge badge;
    struct msgbuf_ingredient ingredient;

    for(int i=0; i<5; i++){
        if(msgrcv(mq_badge, (struct msgbuf_badge *) &badge, sizeof(struct msgbuf_badge), smoker_id+1, 0) == -1){ //BLOCKING
            perror("smoker msgrvc badge");
            exit(1);
        }
        printf("%sSMOKER %d recieved badge. %s\n", COLOR[smoker_id], smoker_id+1, COLOR_END);
        fflush( stdout );

        if(msgrcv(mq_ing, (struct msgbuf_ingredient *) &ingredient, sizeof(struct msgbuf_ingredient), smoker_id+1, IPC_NOWAIT) > 0){ //NOT BLOCKING
            printf("%sSMOKER %d takes %s from counter. He already has %s.%s\n", COLOR[smoker_id], smoker_id+1, INGREDIENTS[smoker_id], INGREDIENTS_EACH[smoker_id], COLOR_END);
            fflush( stdout );

            //Tell salesman that counter is clear
            ingredient.mtype = SIGNAL_CLEAR;
            ingredient.msg = 0; //WHATEVER
            if(msgsnd(mq_ing, (struct msgbuf_badge *) &ingredient, sizeof(struct msgbuf_ingredient), 0) == -1){ 
                perror("smoker msgsnd clear to salesman");
                exit(1);
            }
            printf("%sSMOKER %d signals to SALESMAN that counter is clear.\n%s", COLOR[smoker_id], smoker_id+1, COLOR_END);
            fflush( stdout );

            //Send badge further
            badge.msg = 0; // WHATEVER
            badge.mytype = (badge.mytype) % 3 + 1;
        
            if(msgsnd(mq_badge, (struct msgbuf_badge *) &badge, sizeof(struct msgbuf_badge), 0) == -1){ 
                perror("smoker msgrcv badge to next");
                exit(1);
            } 
            printf("%sSMOKER %d sends badge to SMOKER %ld. %s\n", COLOR[smoker_id], smoker_id+1, badge.mytype, COLOR_END);
            fflush( stdout );

            printf("%sSMOKER %d starts smoking.%s\n", COLOR[smoker_id], smoker_id+1, COLOR_END);
            fflush( stdout );

            sleep(rand() % 2 + 1);

            printf("%sSMOKER %d ends smoking.\n%s", COLOR[smoker_id], smoker_id+1, COLOR_END);
            fflush( stdout );
        }

        else{
            badge.msg = 0; // WHATEVER
            badge.mytype = (badge.mytype) % 3 + 1;

            if(msgsnd(mq_badge, (struct msgbuf_badge *) &badge, sizeof(struct msgbuf_badge), 0) == -1){ 
                perror("smoker msgrcv badge to next");
                exit(1);
            } 
            printf("%sSMOKER %d sends badge to SMOKER %ld. %s\n", COLOR[smoker_id], smoker_id+1, badge.mytype, COLOR_END);
            fflush( stdout );
        }
    }
    
}


    

void agent(int mq_badge, int mq_ing)
{
    struct msgbuf_badge badge;
    struct msgbuf_ingredient ingredient;

    badge.mytype = 1; // Give first badge permission to smoker1
    badge.msg = 0; // WHATEVER

    //Pass the badge to the first smoker
    if (msgsnd(mq_badge, (struct msgbuf_badge *) &badge, sizeof(struct msgbuf_badge), 0) == -1) {
        perror("agent msgsnd badge");
        exit(1);
    }
    printf("%sSALESMAN: Sent first badge to SMOKER 1 (bootstraping).%s\n", COLOR[SALESMAN], COLOR_END);
    fflush( stdout );

    ingredient.mtype = rand() % 3 + 1;
    ingredient.msg = 0; //WHATEVER
    
    if(msgsnd(mq_ing, (struct msgbuf_ingredient *) &ingredient, sizeof(struct msgbuf_ingredient),0) == -1){
        perror("agent msgsnd ingredients");
        exit(1);
    }

    printf("%sSALESMAN: Putting %s on the counter. %s\n", COLOR[SALESMAN] ,INGREDIENTS[ingredient.mtype-1], COLOR_END);
    fflush( stdout );

    for(int i=0; i<5; i++){
        if(msgrcv(mq_ing, (struct msgbuf_ingredient *) &ingredient, sizeof(struct msgbuf_ingredient), SIGNAL_CLEAR, 0) == -1){ //BLOCKING
            perror("msgrecv signal clear");
            exit(1);
        }   
        printf("%sSALESMAN: Recieved clear counter signal. %s\n", COLOR[SALESMAN], COLOR_END); 
        fflush( stdout );

        ingredient.mtype = rand() % 3 + 1;
        ingredient.msg = 0; //WHATEVER
        
        if(msgsnd(mq_ing, (struct msgbuf_ingredient *) &ingredient, sizeof(struct msgbuf_ingredient),0) == -1){
            perror("agent msgsnd ingredients");
            exit(1);
        }

        printf("%sSALESMAN: Putting %s on the counter. %s\n", COLOR[SALESMAN] ,INGREDIENTS[ingredient.mtype-1], COLOR_END);
        fflush( stdout );
    }
}