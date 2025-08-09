#include <stdio.h>
#include <pthread.h>
#include <unistd.h>  // for sleep()

// This function will run in a new thread
void* worker_function(void* arg) {
    int id = *(int*)arg;  // Get the thread ID passed
    printf("Worker thread %d started.\n", id);

    // Simulate work
    for (int i = 0; i < 3; ++i) {
        printf("Thread %d working...\n", id);
        sleep(1);  // Sleep for 1 second
    }

    printf("Worker thread %d finished.\n", id);
    return NULL;
}

int main() {
    pthread_t threads[2];  // Two thread identifiers
    int ids[2] = {1, 2};

    printf("Main thread starting.\n");

    // Create two threads
    for (int i = 0; i < 2; ++i) {
        pthread_create(&threads[i], NULL, worker_function, &ids[i]);
    }

    // Wait for both threads to finish
    for (int i = 0; i < 2; ++i) {
        pthread_join(threads[i], NULL);
    }

    printf("Main thread finished.\n");
    return 0;
}
