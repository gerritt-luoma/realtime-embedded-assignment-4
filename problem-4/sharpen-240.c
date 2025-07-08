#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <syslog.h>

#define IMG_HEIGHT (300)
#define IMG_WIDTH (400)
#define NUM_ITERATIONS 240

// Comment this line to disable image output (used for timing only)
#define WRITE_IMAGE

typedef double FLOAT;
typedef unsigned int UINT32;
typedef unsigned long long int UINT64;
typedef unsigned char UINT8;

UINT8 header[22];
UINT8 R[IMG_HEIGHT * IMG_WIDTH];
UINT8 G[IMG_HEIGHT * IMG_WIDTH];
UINT8 B[IMG_HEIGHT * IMG_WIDTH];
UINT8 convR[IMG_HEIGHT * IMG_WIDTH];
UINT8 convG[IMG_HEIGHT * IMG_WIDTH];
UINT8 convB[IMG_HEIGHT * IMG_WIDTH];

#define K 4.0
FLOAT PSF[9] = {
    -K / 8.0, -K / 8.0, -K / 8.0,
    -K / 8.0, K + 1.0,  -K / 8.0,
    -K / 8.0, -K / 8.0, -K / 8.0
};

void sharpen_image()
{
    int i, j;
    FLOAT temp;

    for (i = 1; i < IMG_HEIGHT - 1; i++)
    {
        for (j = 1; j < IMG_WIDTH - 1; j++)
        {
            int idx = i * IMG_WIDTH + j;

            // Red
            temp = 0.0;
            temp += PSF[0] * R[((i - 1) * IMG_WIDTH) + j - 1];
            temp += PSF[1] * R[((i - 1) * IMG_WIDTH) + j];
            temp += PSF[2] * R[((i - 1) * IMG_WIDTH) + j + 1];
            temp += PSF[3] * R[((i) * IMG_WIDTH) + j - 1];
            temp += PSF[4] * R[((i) * IMG_WIDTH) + j];
            temp += PSF[5] * R[((i) * IMG_WIDTH) + j + 1];
            temp += PSF[6] * R[((i + 1) * IMG_WIDTH) + j - 1];
            temp += PSF[7] * R[((i + 1) * IMG_WIDTH) + j];
            temp += PSF[8] * R[((i + 1) * IMG_WIDTH) + j + 1];
            if (temp < 0.0) temp = 0.0;
            if (temp > 255.0) temp = 255.0;
            convR[idx] = (UINT8)temp;

            // Green
            temp = 0.0;
            temp += PSF[0] * G[((i - 1) * IMG_WIDTH) + j - 1];
            temp += PSF[1] * G[((i - 1) * IMG_WIDTH) + j];
            temp += PSF[2] * G[((i - 1) * IMG_WIDTH) + j + 1];
            temp += PSF[3] * G[((i) * IMG_WIDTH) + j - 1];
            temp += PSF[4] * G[((i) * IMG_WIDTH) + j];
            temp += PSF[5] * G[((i) * IMG_WIDTH) + j + 1];
            temp += PSF[6] * G[((i + 1) * IMG_WIDTH) + j - 1];
            temp += PSF[7] * G[((i + 1) * IMG_WIDTH) + j];
            temp += PSF[8] * G[((i + 1) * IMG_WIDTH) + j + 1];
            if (temp < 0.0) temp = 0.0;
            if (temp > 255.0) temp = 255.0;
            convG[idx] = (UINT8)temp;

            // Blue
            temp = 0.0;
            temp += PSF[0] * B[((i - 1) * IMG_WIDTH) + j - 1];
            temp += PSF[1] * B[((i - 1) * IMG_WIDTH) + j];
            temp += PSF[2] * B[((i - 1) * IMG_WIDTH) + j + 1];
            temp += PSF[3] * B[((i) * IMG_WIDTH) + j - 1];
            temp += PSF[4] * B[((i) * IMG_WIDTH) + j];
            temp += PSF[5] * B[((i) * IMG_WIDTH) + j + 1];
            temp += PSF[6] * B[((i + 1) * IMG_WIDTH) + j - 1];
            temp += PSF[7] * B[((i + 1) * IMG_WIDTH) + j];
            temp += PSF[8] * B[((i + 1) * IMG_WIDTH) + j + 1];
            if (temp < 0.0) temp = 0.0;
            if (temp > 255.0) temp = 255.0;
            convB[idx] = (UINT8)temp;
        }
    }
}

int main(int argc, char *argv[])
{
    int fdin, fdout;
    struct timespec start, end;

    if (argc < 3)
    {
        printf("Usage: %s input.ppm output.ppm\n", argv[0]);
        return -1;
    }

    openlog("sharpen", LOG_PID | LOG_CONS, LOG_USER);

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < NUM_ITERATIONS; ++i)
    {
        if ((fdin = open(argv[1], O_RDONLY)) < 0)
        {
            perror("Error opening input file");
            return -1;
        }

        int bytesLeft = 21, bytesRead = 0;
        do
        {
            bytesRead = read(fdin, header + (21 - bytesLeft), bytesLeft);
            bytesLeft -= bytesRead;
        }
        while (bytesLeft > 0);
        header[21] = '\0';

        for (int i = 0; i < IMG_HEIGHT * IMG_WIDTH; i++)
        {
            read(fdin, &R[i], 1);
            read(fdin, &G[i], 1);
            read(fdin, &B[i], 1);
        }
        close(fdin);

        sharpen_image();

#ifdef WRITE_IMAGE
        if ((fdout = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0)
        {
            perror("Error opening output file");
            return -1;
        }

        write(fdout, header, 21);
        for (int i = 0; i < IMG_HEIGHT * IMG_WIDTH; i++)
        {
            write(fdout, &convR[i], 1);
            write(fdout, &convG[i], 1);
            write(fdout, &convB[i], 1);
        }
        close(fdout);
#endif
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    long seconds = end.tv_sec - start.tv_sec;
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    double elapsed = seconds + nanoseconds * 1e-9;

    syslog(LOG_CRIT, "Total time for %d iterations: %.3f seconds", NUM_ITERATIONS, elapsed);
    closelog();

    return 0;
}