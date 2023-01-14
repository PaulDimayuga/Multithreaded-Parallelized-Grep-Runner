#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>

typedef struct __node_t {
    char* value;
    struct __node_t *next;

} node_t;

typedef struct __queue_t{
  node_t *head;
  node_t *tail;
} queue_t;


void initialize_queue(queue_t *q) {
  node_t *tmp = malloc(sizeof(node_t));
  tmp->next = NULL;
  q->head = q->tail = tmp;
}

void dealloc_queue(queue_t *q){
    free(q->head);
}

int queue_is_empty(queue_t *q) {
    if (q->head->next == NULL){
        return 1;
    } else{
        return 0;
    }
}


void queue_enqueue(queue_t *q, char* path) {
    node_t *tmp = malloc(sizeof(node_t));
    assert(tmp != NULL);
    tmp->value = (char*) malloc(strlen(path) + 1);
    strcpy(tmp->value, path);
    tmp->next = NULL;

    q->tail->next = tmp;
    q->tail = tmp;
}

char * queue_dequeue(queue_t *q) {
    node_t *tmp = q->head;
    node_t *new_head = tmp->next;
    char *value;
    if (new_head == NULL){
        return NULL;
    }
    value = new_head->value;
    q->head = new_head;
    free(tmp);
    return value;
}

void child_dir(queue_t *q, char *path){
    queue_enqueue(q, path); // enqueue path to Task Queue
    printf("[0] ENQUEUE %s\n", path);   // prints out ENQUEUE
    return;
}


void child_file(char* search, char *filename, char * abs_path){
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
    int not_found = system(command);    // executes grep using concatenated string "command"
    if (not_found){
        printf("[0] ABSENT %s\n", abs_path);    // prints out ABSENT
    } else {
        printf("[0] PRESENT %s\n", abs_path);   // prints out PRESENT
    }
    return;

}


int main( int argc, char *argv[] ){
    

    if(argc != 4){
        printf("Not enough arguments passed, exiting\n");
        return -1;
    }

    queue_t TQ;
    initialize_queue(&TQ);    // Initialize Task Queue

    char abs_path[300];
    char *ptr;
    ptr = realpath(argv[2], abs_path);
    if (!(ptr)) {
        perror("realpath");
        return -1;
    }
    
    queue_enqueue(&TQ, abs_path);   // Enqueue rootpath to Task Queue
    char *dir_path;
    
    while(!(queue_is_empty(&TQ))){
        dir_path = queue_dequeue(&TQ);  // Gets Task in front of the queue
        printf("[0] DIR %s\n", dir_path);   // prints out DIR

        DIR* dir = opendir(dir_path);   // Opens directory of current task
        if (dir == NULL) {
            perror("opendir");
            return 1;
        }

        if (chdir(dir_path) == -1) {    // Changes to the directory of current task
            perror("chdir failed");
            return 1;
        }
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {     // Reads all entries of directory
            if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            {
                continue;    // Ignore ". or .."
            }

            ptr = realpath(entry->d_name, abs_path);

            if (entry->d_type == DT_DIR) {  // If entry is directory, enqueue using child_dir function
                child_dir(&TQ, abs_path);
            } else if (entry->d_type == DT_REG){    // If entry is file, execute grep using child_file function
                child_file(argv[3], entry->d_name, abs_path);
            } else {
                printf("Unknown Entry for this Project: %s\n", abs_path);
            }
        }
        free(dir_path);
        closedir(dir);  // close directory
    }
    dealloc_queue(&TQ);
    return 0;
}