#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <semaphore.h>
#include <assert.h>


pthread_t tid[8];
pthread_mutex_t lock[8];
pthread_cond_t cond[8];

typedef struct __node_t {
    char* value;
    struct __node_t *next;

} node_t;

typedef struct __queue_t{
  node_t *head;
  node_t *tail;
  pthread_mutex_t head_lock, tail_lock; // For locking when enqueuing and dequeuing
} queue_t;


void initialize_queue(queue_t *q) {
  node_t *tmp = malloc(sizeof(node_t));
  tmp->next = NULL;
  q->head = q->tail = tmp;
  pthread_mutex_init(&q->head_lock, NULL);
  pthread_mutex_init(&q->tail_lock, NULL);
}

void dealloc_queue(queue_t *q){
    free(q->head);
}

int queue_is_empty(queue_t *q) {
    pthread_mutex_lock(&q->head_lock); 
    if (q->head->next == NULL){
        pthread_mutex_unlock(&q->head_lock);
        return 1;
    } else{
        pthread_mutex_unlock(&q->head_lock);
        return 0;
    }
}


void queue_enqueue(queue_t *q, char* path) {
    node_t *tmp = malloc(sizeof(node_t));
    assert(tmp != NULL);
    tmp->value = (char*) malloc(strlen(path) + 1);
    strcpy(tmp->value, path);
    tmp->next = NULL;

    pthread_mutex_lock(&q->tail_lock);    // locks tail for enqueuing
    q->tail->next = tmp;
    q->tail = tmp;
    pthread_mutex_unlock(&q->tail_lock);
}

char * queue_dequeue(queue_t *q) {
    pthread_mutex_lock(&q->head_lock);  // locks head for dequeuing
    node_t *tmp = q->head;
    node_t *new_head = tmp->next;
    char *value;
    if (new_head == NULL){
        pthread_mutex_unlock(&q->head_lock);
        return NULL;
    }
    value = new_head->value;
    q->head = new_head;
    pthread_mutex_unlock(&q->head_lock);
    free(tmp);
    return value;
}

void child_dir(queue_t *q, char *path, int n){
    
    printf("[%d] ENQUEUE %s\n", n, path);
    queue_enqueue(q, path);
    return;
}


void child_file(char* search, int n, char * filename, char *abs_path){
    char command[500] = "";
    char s1[] = "grep ";
    char s2[] = " > /dev/null";
    strcat(command, s1);
    strcat(command, "\"");
    strcat(command, search);
    strcat(command, "\"");
    strcat(command, " ");
    strcat(command, "\"");
    strcat(command, filename);
    strcat(command, "\"");
    strcat(command, s2);
    int not_found = system(command);    // executes grep
    if (not_found){
        printf("[%d] ABSENT %s\n", n, abs_path);
    } else if(not_found == 0){
        printf("[%d] PRESENT %s\n", n, abs_path);
    }
    return;

}

queue_t TQ;
char search_string[300];    // string to be searched
int thread_num;             // number of threads
int waiting = 0;            // number of waiting threads 
int reading = 0;            // number of current reading threads
int end = 0;                // threads will terminate if end = 1
int standby = 0;            // standby the thread that enqueued to let other threads dequeue


void* t0(void *arg){
    int n = *((int *) arg);
    while(1){   // loops until any threads cannot dequeue anymore from the task queue
        pthread_mutex_lock(&lock[0]);   // acquires lock for updating "waiting" and checking of task queue
        while(queue_is_empty(&TQ)){
            waiting++;      // locked inside lock[0];
            if (waiting < thread_num){
                pthread_cond_wait(&cond[0], &lock[0]);  // sleeps on cond[0] "waiting queue" to wait for available tasks
                if (end == 1){
                    goto finish;                // Follows the first thread that terminates
                }
            } else{
                end = 1;
                goto finish;      // waiting threads are equal to max number of threads, current thread will terminate
            }
        }

        char *dir_path;
        dir_path = queue_dequeue(&TQ);      // gets task from task queue
        printf("[%d] DIR %s\n", n, dir_path);

        DIR* dir = opendir(dir_path);
        if (dir == NULL) {
            perror("opendir");
            pthread_mutex_unlock(&lock[0]);
            return NULL;
        }
         
        if (standby == 1){
            pthread_cond_signal(&cond[2]);  // newly awakened thread signals standby thread to wake up
            standby = 0;
        }
        pthread_mutex_unlock(&lock[0]); // releases lock for checking task queue

        pthread_mutex_lock(&lock[4]);   // acquires lock for updating and checking "reading"
        reading++;                      // locked inside lock[4]

        // enters reading directory loop
        while (1) {             // loops until thread finishes directory
            if (reading > 1){
                pthread_cond_wait(&cond[1], &lock[4]);  // if there is more than 1 reader, current thread sleeps on cond[1] "reading queue"
            }
            pthread_mutex_unlock(&lock[4]);      // releases lock for updating and checking "reading"

            pthread_mutex_lock(&lock[1]);       // acquires lock for reading directory
            if (chdir(dir_path) == -1) {    //cd to current directory
                perror("chdir failed");
                pthread_mutex_unlock(&lock[1]); // current thread releases lock for reading directory
                return NULL;
            }
            struct dirent* entry;
            read_again:
            entry = readdir(dir);
            if (entry == NULL){
                pthread_mutex_lock(&lock[4]);   // acquires lock for updating and checking "reading"
                reading--;
                if (reading > 0)
                    pthread_cond_signal(&cond[1]);   // current thread finished reading directory, wakes up one thread from the "reading queue"
                pthread_mutex_unlock(&lock[4]);   // releases lock for updating and checking "reading"
                break;
            }

            if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            {
                goto read_again;    // . or .. entry is ignored
            }
            
            char *ptr;
            char abs_path[300];
            ptr = realpath(entry->d_name, abs_path);    // gets absolute path of entry

            // checking entry
            if (entry->d_type == DT_DIR) {     // entry is directory
                pthread_mutex_lock(&lock[0]);    // acquires lock for updating "waiting" and Task queue
                child_dir(&TQ, abs_path, n);    // entry gets enqueued
                if (waiting > 0){
                    waiting--;
                    pthread_cond_signal(&cond[0]);  // wakes up one of the threads in "waiting queue"
                    standby = 1;
                    pthread_cond_wait(&cond[2], &lock[0]);  // standby to let the newly awakened thread to print DIR
                }
                pthread_mutex_unlock(&lock[0]);     // releases lock for updating "waiting" and Task queue

            } else if (entry->d_type == DT_REG) {   // entry is file
                pthread_mutex_lock(&lock[0]);       // acquires lock for printing (lock[0] also handles printing)
                child_file(search_string, n, entry->d_name, abs_path); // execute grep on file
                pthread_mutex_unlock(&lock[0]);     // releases lock for printing

            } else {
                pthread_mutex_lock(&lock[0]);   // acquires lock for printing (lock[0] also handles printing)
                printf("Unknown Entry for this Project: %s\n", abs_path);
                pthread_mutex_unlock(&lock[0]); // releases lock for printing 
            }
            pthread_mutex_lock(&lock[4]);   // acquires lock for checking "reading"
            if (reading > 1){
                pthread_cond_signal(&cond[1]);  // wakes up one of the threads in "reading queue"
            } 
            pthread_mutex_unlock(&lock[1]); // current thread releases lock for reading directory
        }
        pthread_mutex_lock(&lock[0]); 
        closedir(dir);         // Close directory
        free(dir_path);
        pthread_mutex_unlock(&lock[0]); 
        pthread_mutex_unlock(&lock[1]);  // current thread releases lock for reading directory
    }
    finish:
    if(waiting > 0){
        waiting--;
        pthread_cond_signal(&cond[0]); // terminating thread wakes up one of the remaining threads in "waiting queue"
    }
    pthread_mutex_unlock(&lock[0]); // releases lock for updating "waiting" and checking of task queue
    return NULL;
}


int main( int argc, char *argv[] ){
    if(argc != 4){
        printf("Not enough arguments passed. Exiting\n");
        return -1;
    }

    if (atoi(argv[1]) < 1 || atoi(argv[1]) > 8){
        printf("N is not between 1 and 8, Exiting");
        return -1;
    }

    initialize_queue(&TQ);

    strcpy(search_string, argv[3]);
    thread_num = atoi(argv[1]);

    for(int i=0;i<8;i++){
        if (pthread_mutex_init(&lock[i], NULL) != 0)
            printf("\n mutex init has failed\n");
    }

    char abs_path[PATH_MAX];
    char *ptr;

    ptr = realpath(argv[2], abs_path);
    if (!(ptr)) {
        perror("realpath");
        return -1;
    }
    
    queue_enqueue(&TQ, abs_path);   // main thread enqueues the rootpath into the task queue

    int *arg_arr[8];
    for(int i=0;i<thread_num;i++){
        int *arg = malloc(sizeof(*arg));
        arg_arr[i] = arg;
        *arg = i;
        pthread_create(&tid[i], NULL, (void* ) t0, arg);    // creates N number of threads
        
    }

    for (int i=0;i<thread_num;i++){
        pthread_join(tid[i], NULL);       // waits for N number of threads
        free(arg_arr[i]);
        
    }
    dealloc_queue(&TQ);

    return 0;
}