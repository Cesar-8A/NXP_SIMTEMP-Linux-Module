#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>  // Añadir esta línea

#define DEVICE "/dev/simtemp"

void *read_temperature(void *arg) {
    int fd = open(DEVICE, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open device");
        return NULL;
    }

    char buffer[32];
    while (1) {
        ssize_t len = read(fd, buffer, sizeof(buffer));
        if (len > 0) {
            buffer[len] = '\0';
            printf("Temperature: %s", buffer);
        } else {
            perror("Failed to read temperature");
            break;
        }
        usleep(500000); // Sleep 0.5 seconds
    }

    close(fd);
    return NULL;
}

int main() {
    pthread_t thread;
    pthread_create(&thread, NULL, read_temperature, NULL);

    // Simulate setting new values for sampling interval or threshold
    int fd = open(DEVICE, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open device");
        return -1;
    }

    while (1) {
        sleep(5);
        const char *new_sampling_ms = "2000"; // Change sampling interval
        write(fd, new_sampling_ms, strlen(new_sampling_ms)); // Ahora sin error
        printf("Changed sampling interval to 2000ms\n");
    }

    pthread_join(thread, NULL);
    close(fd);
    return 0;
}
