/*
 *
 *  Adapted by Sam Siewert for use with UVC web cameras and Bt878 frame
 *  grabber NTSC cameras to acquire digital video from a source,
 *  time-stamp each frame acquired, save to a PGM or PPM file.
 *
 *  The original code adapted was open source from V4L2 API and had the
 *  following use and incorporation policy:
 *
 *  This program can be used and distributed without restrictions.
 *
 *      This program is provided with the V4L2 API
 * see http://linuxtv.org/docs.php for more information
 *
 * sudo apt-get install sysstat
 *
 */

#include <syslog.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <getopt.h>             /* getopt_long() */

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <semaphore.h>
#include <signal.h>

#include <linux/videodev2.h>


#define CLEAR(x) memset(&(x), 0, sizeof(x))

#define HRES 640
#define VRES 480
#define HRES_STR "640"
#define VRES_STR "480"

//#define HRES 320
//#define VRES 240
//#define HRES_STR "320"
//#define VRES_STR "240"

#define START_UP_FRAMES (8)
#define LAST_FRAMES (1)
#define CAPTURE_FRAMES (1800+LAST_FRAMES)
#define FRAMES_TO_ACQUIRE (CAPTURE_FRAMES + START_UP_FRAMES + LAST_FRAMES)

//#define FRAMES_PER_SEC (1)
//#define FRAMES_PER_SEC (10)
//#define FRAMES_PER_SEC (20)
//#define FRAMES_PER_SEC (25)
#define FRAMES_PER_SEC (30)

// Uncomment to allow for dumping of both the RGB and the Sharpened frames
// #define DUMP_RGB

#define NUM_CPU_CORES (4)
#define TRUE (1)
#define FALSE (0)

#define NUM_THREADS (1)

// Format is used by a number of functions, so made as a file global
static struct v4l2_format fmt;

#define K 4.0

float PSF[9] = {-K/8.0, -K/8.0, -K/8.0, -K/8.0, K+1.0, -K/8.0, -K/8.0, -K/8.0, -K/8.0};

enum io_method
{
    IO_METHOD_READ,
    IO_METHOD_MMAP,
    IO_METHOD_USERPTR,
};

struct buffer
{
    void   *start;
    size_t  length;
};

// Sequencer information
int abortProcessing = FALSE;
sem_t semProc;
static unsigned long long seqCnt = 0;
static timer_t sequencer_timer;
static struct itimerspec itime = {{1,0}, {1,0}};
static struct itimerspec last_itime;

typedef struct
{
    int threadIdx;
} thread_params_t;

void* mainloop(void* threadp);
void Sequencer(int id);

static char            *dev_name;
//static enum io_method   io = IO_METHOD_USERPTR;
//static enum io_method   io = IO_METHOD_READ;
static enum io_method   io = IO_METHOD_MMAP;
static int              fd = -1;
struct buffer          *buffers;
static unsigned int     n_buffers;
static int              out_buf;
static int              force_format=1;

static int              frame_count = (FRAMES_TO_ACQUIRE);


static double fnow=0.0, fstart=0.0, fstop=0.0;
static struct timespec time_now, time_start, time_stop;

void Sequencer(int id)
{
    int flags = 0;
    seqCnt++;

    // Release the semaphore every iteration
    sem_post(&semProc);

    if (seqCnt >= frame_count)
    {
        // Stop the sequencer
        itime.it_interval.tv_sec = 0;
        itime.it_interval.tv_nsec = 0;
        itime.it_value.tv_sec = 0;
        itime.it_value.tv_nsec = 0;
        timer_settime(sequencer_timer, flags, &itime, &last_itime);

        abortProcessing = TRUE;
        sem_post(&semProc);
    }
}

static void errno_exit(const char *s)
{
        fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
        exit(EXIT_FAILURE);
}

static int xioctl(int fh, int request, void *arg)
{
        int r;

        do
        {
            r = ioctl(fh, request, arg);

        } while (-1 == r && EINTR == errno);

        return r;
}

char ppm_header[]="P6\n#9999999999 sec 9999999999 msec \n"HRES_STR" "VRES_STR"\n255\n";
char ppm_dumpname[]="frames/test0000.ppm";
char ppm_dumpname_sharpened[]="frames/post0000.ppm";

static void dump_ppm(const void *p, int size, unsigned int tag, struct timespec *time, int sharpened)
{
    int written, i, total, dumpfd;
    char* filename;

    if (sharpened)
    {
        filename = ppm_dumpname_sharpened;
    }
    else
    {
        filename = ppm_dumpname;
    }

    snprintf(&filename[11], 9, "%04d", tag);
    strncat(&filename[15], ".ppm", 5);
    dumpfd = open(filename, O_WRONLY | O_NONBLOCK | O_CREAT, 00666);

    snprintf(&ppm_header[4], 11, "%010d", (int)time->tv_sec);
    strncat(&ppm_header[14], " sec ", 5);
    snprintf(&ppm_header[19], 11, "%010d", (int)((time->tv_nsec)/1000000));
    strncat(&ppm_header[29], " msec \n"HRES_STR" "VRES_STR"\n255\n", 19);

    // subtract 1 from sizeof header because it includes the null terminator for the string
    written=write(dumpfd, ppm_header, sizeof(ppm_header)-1);

    total=0;

    do
    {
        written=write(dumpfd, p, size);
        total+=written;
    } while(total < size);

    clock_gettime(CLOCK_MONOTONIC, &time_now);
    fnow = (double)time_now.tv_sec + (double)time_now.tv_nsec / 1000000000.0;
    //printf("Frame written to flash at %lf, %d, bytes\n", (fnow-fstart), total);

    close(dumpfd);

}


char pgm_header[]="P5\n#9999999999 sec 9999999999 msec \n"HRES_STR" "VRES_STR"\n255\n";
char pgm_dumpname[]="frames/test0000.pgm";

static void dump_pgm(const void *p, int size, unsigned int tag, struct timespec *time)
{
    int written, i, total, dumpfd;

    snprintf(&pgm_dumpname[11], 9, "%04d", tag);
    strncat(&pgm_dumpname[15], ".pgm", 5);
    dumpfd = open(pgm_dumpname, O_WRONLY | O_NONBLOCK | O_CREAT, 00666);

    snprintf(&pgm_header[4], 11, "%010d", (int)time->tv_sec);
    strncat(&pgm_header[14], " sec ", 5);
    snprintf(&pgm_header[19], 11, "%010d", (int)((time->tv_nsec)/1000000));
    strncat(&pgm_header[29], " msec \n"HRES_STR" "VRES_STR"\n255\n", 19);

    // subtract 1 from sizeof header because it includes the null terminator for the string
    written=write(dumpfd, pgm_header, sizeof(pgm_header)-1);

    total=0;

    do
    {
        written=write(dumpfd, p, size);
        total+=written;
    } while(total < size);

    clock_gettime(CLOCK_MONOTONIC, &time_now);
    fnow = (double)time_now.tv_sec + (double)time_now.tv_nsec / 1000000000.0;
    //printf("Frame written to flash at %lf, %d, bytes\n", (fnow-fstart), total);

    close(dumpfd);

}


void yuv2rgb_float(float y, float u, float v,
                   unsigned char *r, unsigned char *g, unsigned char *b)
{
    float r_temp, g_temp, b_temp;

    // R = 1.164(Y-16) + 1.1596(V-128)
    r_temp = 1.164*(y-16.0) + 1.1596*(v-128.0);
    *r = r_temp > 255.0 ? 255 : (r_temp < 0.0 ? 0 : (unsigned char)r_temp);

    // G = 1.164(Y-16) - 0.813*(V-128) - 0.391*(U-128)
    g_temp = 1.164*(y-16.0) - 0.813*(v-128.0) - 0.391*(u-128.0);
    *g = g_temp > 255.0 ? 255 : (g_temp < 0.0 ? 0 : (unsigned char)g_temp);

    // B = 1.164*(Y-16) + 2.018*(U-128)
    b_temp = 1.164*(y-16.0) + 2.018*(u-128.0);
    *b = b_temp > 255.0 ? 255 : (b_temp < 0.0 ? 0 : (unsigned char)b_temp);
}


// This is probably the most acceptable conversion from camera YUYV to RGB
//
// Wikipedia has a good discussion on the details of various conversions and cites good references:
// http://en.wikipedia.org/wiki/YUV
//
// Also http://www.fourcc.org/yuv.php
//
// What's not clear without knowing more about the camera in question is how often U & V are sampled compared
// to Y.
//
// E.g. YUV444, which is equivalent to RGB, where both require 3 bytes for each pixel
//      YUV422, which we assume here, where there are 2 bytes for each pixel, with two Y samples for one U & V,
//              or as the name implies, 4Y and 2 UV pairs
//      YUV420, where for every 4 Ys, there is a single UV pair, 1.5 bytes for each pixel or 36 bytes for 24 pixels

void yuv2rgb(int y, int u, int v, unsigned char *r, unsigned char *g, unsigned char *b)
{
   int r1, g1, b1;

   // replaces floating point coefficients
   int c = y-16, d = u - 128, e = v - 128;

   // Conversion that avoids floating point
   r1 = (298 * c           + 409 * e + 128) >> 8;
   g1 = (298 * c - 100 * d - 208 * e + 128) >> 8;
   b1 = (298 * c + 516 * d           + 128) >> 8;

   // Computed values may need clipping.
   if (r1 > 255) r1 = 255;
   if (g1 > 255) g1 = 255;
   if (b1 > 255) b1 = 255;

   if (r1 < 0) r1 = 0;
   if (g1 < 0) g1 = 0;
   if (b1 < 0) b1 = 0;

   *r = r1 ;
   *g = g1 ;
   *b = b1 ;
}


// always ignore first 8 frames
int framecnt=-8;

unsigned char bigbuffer[(1280*960)];
unsigned char bigbuffer_processed[(1280*960)];

static void process_image(const void *p, int size)
{
    int i, j, newi, newsize=0;
    struct timespec frame_time;
    int y_temp, y2_temp, u_temp, v_temp;
    unsigned char *pptr = (unsigned char *)p;

    // record when process was called
    clock_gettime(CLOCK_REALTIME, &frame_time);

    framecnt++;
    //printf("frame %d: ", framecnt);

    if(framecnt == 0)
    {
        clock_gettime(CLOCK_MONOTONIC, &time_start);
        fstart = (double)time_start.tv_sec + (double)time_start.tv_nsec / 1000000000.0;
    }

    // This just dumps the frame to a file now, but you could replace with whatever image
    // processing you wish.
    //

    if(fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_GREY)
    {
        printf("Dump graymap as-is size %d\n", size);
        dump_pgm(p, size, framecnt, &frame_time);
    }

    else if(fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV)
    {
        // Pixels are YU and YV alternating, so YUYV which is 4 bytes
        // We want RGB, so RGBRGB which is 6 bytes
        // First convert to RGB which can then be used for sharpening
        struct timespec processing_start, processing_stop;
        double processing_start_time, processing_stop_time;
        clock_gettime(CLOCK_MONOTONIC, &processing_start);
        for(i=0, newi=0; i<size; i=i+4, newi=newi+6)
        {
            y_temp=(int)pptr[i]; u_temp=(int)pptr[i+1]; y2_temp=(int)pptr[i+2]; v_temp=(int)pptr[i+3];
            yuv2rgb(y_temp, u_temp, v_temp, &bigbuffer[newi], &bigbuffer[newi+1], &bigbuffer[newi+2]);
            yuv2rgb(y2_temp, u_temp, v_temp, &bigbuffer[newi+3], &bigbuffer[newi+4], &bigbuffer[newi+5]);
        }
        int l1, l2, l3, m1, m2, m3, r1, r2, r3;
        float temp;

        // Skip first and last row, no neighbors to convolve with
        for(i=1; i<((VRES)-1); i++)
        {
            // Skip first and last column, no neighbors to convolve with
            for(j=1; j<((HRES)-1); j++)
            {
                // Precompute left column idxs
                l1 = (((i-1)*HRES)+j-1) * 3;
                l2 = l1 + 3;
                l3 = l2 + 3;

                // Precompute middle column idxs
                m1 = (((i)*HRES)+j-1) * 3;
                m2 = m1 + 3; // Current pixel
                m3 = m2 + 3;

                // Precompute right column idxs
                r1 = (((i+1)*HRES)+j-1) * 3;
                r2 = r1 + 3;
                r3 = r2 + 3;

                // Process all of the reds
                temp=0;
                temp += (PSF[0] * (float)bigbuffer[l1]);
                temp += (PSF[1] * (float)bigbuffer[l2]);
                temp += (PSF[2] * (float)bigbuffer[l3]);
                temp += (PSF[3] * (float)bigbuffer[m1]);
                temp += (PSF[4] * (float)bigbuffer[m2]);
                temp += (PSF[5] * (float)bigbuffer[m3]);
                temp += (PSF[6] * (float)bigbuffer[r1]);
                temp += (PSF[7] * (float)bigbuffer[r2]);
                temp += (PSF[8] * (float)bigbuffer[r3]);
                if(temp<0.0) temp=0.0;
                if(temp>255.0) temp=255.0;
                bigbuffer_processed[m2]=(unsigned char)temp;

                // Process all of the greens
                temp=0;
                temp += (PSF[0] * (float)bigbuffer[l1 + 1]);
                temp += (PSF[1] * (float)bigbuffer[l2 + 1]);
                temp += (PSF[2] * (float)bigbuffer[l3 + 1]);
                temp += (PSF[3] * (float)bigbuffer[m1 + 1]);
                temp += (PSF[4] * (float)bigbuffer[m2 + 1]);
                temp += (PSF[5] * (float)bigbuffer[m3 + 1]);
                temp += (PSF[6] * (float)bigbuffer[r1 + 1]);
                temp += (PSF[7] * (float)bigbuffer[r2 + 1]);
                temp += (PSF[8] * (float)bigbuffer[r3 + 1]);
                if(temp<0.0) temp=0.0;
                if(temp>255.0) temp=255.0;
                bigbuffer_processed[m2 + 1]=(unsigned char)temp;

                // Process all of the blues
                temp=0;
                temp += (PSF[0] * (float)bigbuffer[l1 + 2]);
                temp += (PSF[1] * (float)bigbuffer[l2 + 2]);
                temp += (PSF[2] * (float)bigbuffer[l3 + 2]);
                temp += (PSF[3] * (float)bigbuffer[m1 + 2]);
                temp += (PSF[4] * (float)bigbuffer[m2 + 2]);
                temp += (PSF[5] * (float)bigbuffer[m3 + 2]);
                temp += (PSF[6] * (float)bigbuffer[r1 + 2]);
                temp += (PSF[7] * (float)bigbuffer[r2 + 2]);
                temp += (PSF[8] * (float)bigbuffer[r3 + 2]);
                if(temp<0.0) temp=0.0;
                if(temp>255.0) temp=255.0;
                bigbuffer_processed[m2 + 2]=(unsigned char)temp;
            }
        }

        clock_gettime(CLOCK_MONOTONIC, &processing_stop);
        processing_start_time = (double)processing_start.tv_sec + (double)processing_start.tv_nsec / 1000000000.0;
        processing_stop_time  = (double)processing_stop.tv_sec  + (double)processing_stop.tv_nsec / 1000000000.0;
        syslog(LOG_CRIT, "SIMPCAP: converted to sharpened image in %.6f seconds\n", (processing_stop_time-processing_start_time));

        if(framecnt > -1)
        {
            struct timespec writeback_start, writeback_stop;
            double writeback_start_time, writeback_stop_time;
            clock_gettime(CLOCK_MONOTONIC, &writeback_start);
            #ifdef DUMP_RGB
                dump_ppm(bigbuffer, ((size*6)/4), framecnt, &frame_time, 0);
            #endif /* DUMP_RGB */
            dump_ppm(bigbuffer_processed, ((size*6)/4), framecnt, &frame_time, 1);

            clock_gettime(CLOCK_MONOTONIC, &writeback_stop);
            writeback_start_time = (double)writeback_start.tv_sec + (double)writeback_start.tv_nsec / 1000000000.0;
            writeback_stop_time  = (double)writeback_stop.tv_sec  + (double)writeback_stop.tv_nsec / 1000000000.0;
            syslog(LOG_CRIT, "SIMPCAP: wrote image(s) in %.6f seconds\n", (writeback_stop_time-writeback_start_time));
        }
    }

    else if(fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB24)
    {
        printf("Dump RGB as-is size %d\n", size);
        dump_ppm(p, size, framecnt, &frame_time, 0);
    }
    else
    {
        printf("ERROR - unknown dump format\n");
    }

}


static int read_frame(void)
{
    struct v4l2_buffer buf;
    unsigned int i;

    switch (io)
    {

        case IO_METHOD_READ:
            if (-1 == read(fd, buffers[0].start, buffers[0].length))
            {
                switch (errno)
                {

                    case EAGAIN:
                        return 0;

                    case EIO:
                        /* Could ignore EIO, see spec. */

                        /* fall through */

                    default:
                        errno_exit("read");
                }
            }

            process_image(buffers[0].start, buffers[0].length);
            break;

        case IO_METHOD_MMAP:
            CLEAR(buf);

            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

            struct timespec acquisition_start, acquisition_stop;
            double acquisition_start_time, acquisition_stop_time;
            clock_gettime(CLOCK_MONOTONIC, &acquisition_start);

            if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf))
            {
                switch (errno)
                {
                    case EAGAIN:
                        return 0;

                    case EIO:
                        /* Could ignore EIO, but drivers should only set for serious errors, although some set for
                           non-fatal errors too.
                         */
                        return 0;


                    default:
                        printf("mmap failure\n");
                        errno_exit("VIDIOC_DQBUF");
                }
            }

            clock_gettime(CLOCK_MONOTONIC, &acquisition_stop);
            acquisition_start_time = (double)acquisition_start.tv_sec + (double)acquisition_start.tv_nsec / 1000000000.0;
            acquisition_stop_time  = (double)acquisition_stop.tv_sec  + (double)acquisition_stop.tv_nsec / 1000000000.0;
            syslog(LOG_CRIT, "SIMPCAP: acquired image in %.6f seconds\n", (acquisition_stop_time-acquisition_start_time));

            assert(buf.index < n_buffers);

            process_image(buffers[buf.index].start, buf.bytesused);

            if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                    errno_exit("VIDIOC_QBUF");
            break;

        case IO_METHOD_USERPTR:
            CLEAR(buf);

            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_USERPTR;

            if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf))
            {
                switch (errno)
                {
                    case EAGAIN:
                        return 0;

                    case EIO:
                        /* Could ignore EIO, see spec. */

                        /* fall through */

                    default:
                        errno_exit("VIDIOC_DQBUF");
                }
            }

            for (i = 0; i < n_buffers; ++i)
                    if (buf.m.userptr == (unsigned long)buffers[i].start
                        && buf.length == buffers[i].length)
                            break;

            assert(i < n_buffers);

            process_image((void *)buf.m.userptr, buf.bytesused);

            if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                    errno_exit("VIDIOC_QBUF");
            break;
    }

    //printf("R");
    return 1;
}


void* mainloop(void* threadp)
{
    unsigned int count;

    count = frame_count;

    while (!abortProcessing)
    {
        sem_wait(&semProc);
        for (;;)
        {
            fd_set fds;
            struct timeval tv;
            int r;

            FD_ZERO(&fds);
            FD_SET(fd, &fds);

            /* Timeout. */
            tv.tv_sec = 2;
            tv.tv_usec = 0;

            r = select(fd + 1, &fds, NULL, NULL, &tv);

            if (-1 == r)
            {
                if (EINTR == errno)
                    continue;
                errno_exit("select");
            }

            if (0 == r)
            {
                fprintf(stderr, "select timeout\n");
                exit(EXIT_FAILURE);
            }

            if (read_frame())
            {
                if(framecnt > 1)
                {
                    clock_gettime(CLOCK_MONOTONIC, &time_now);
                    fnow = (double)time_now.tv_sec + (double)time_now.tv_nsec / 1000000000.0;
                    //printf("REPLACE read at %lf, @ %lf FPS\n", (fnow-fstart), (double)(framecnt+1) / (fnow-fstart));
                    syslog(LOG_CRIT, "SIMPCAP: read at %lf, @ %lf FPS\n", (fnow-fstart), (double)(framecnt+1) / (fnow-fstart));
                }
                else
                {
                    printf("at %lf\n", fnow);
                }

                count--;
                break;
            }

            /* EAGAIN - continue select loop unless count done. */
            if(count <= 0) break;
        }

        if(count <= 0) break;
    }

    clock_gettime(CLOCK_MONOTONIC, &time_stop);
    fstop = (double)time_stop.tv_sec + (double)time_stop.tv_nsec / 1000000000.0;

}

static void stop_capturing(void)
{
        enum v4l2_buf_type type;

        switch (io) {
        case IO_METHOD_READ:
                /* Nothing to do. */
                break;

        case IO_METHOD_MMAP:
        case IO_METHOD_USERPTR:
                type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
                        errno_exit("VIDIOC_STREAMOFF");
                break;
        }
}

static void start_capturing(void)
{
        unsigned int i;
        enum v4l2_buf_type type;

        switch (io)
        {

        case IO_METHOD_READ:
                /* Nothing to do. */
                break;

        case IO_METHOD_MMAP:
                for (i = 0; i < n_buffers; ++i)
                {
                        printf("allocated buffer %d\n", i);
                        struct v4l2_buffer buf;

                        CLEAR(buf);
                        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                        buf.memory = V4L2_MEMORY_MMAP;
                        buf.index = i;

                        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                                errno_exit("VIDIOC_QBUF");
                }
                type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
                        errno_exit("VIDIOC_STREAMON");
                break;

        case IO_METHOD_USERPTR:
                for (i = 0; i < n_buffers; ++i) {
                        struct v4l2_buffer buf;

                        CLEAR(buf);
                        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                        buf.memory = V4L2_MEMORY_USERPTR;
                        buf.index = i;
                        buf.m.userptr = (unsigned long)buffers[i].start;
                        buf.length = buffers[i].length;

                        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                                errno_exit("VIDIOC_QBUF");
                }
                type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
                        errno_exit("VIDIOC_STREAMON");
                break;
        }
}

static void uninit_device(void)
{
        unsigned int i;

        switch (io) {
        case IO_METHOD_READ:
                free(buffers[0].start);
                break;

        case IO_METHOD_MMAP:
                for (i = 0; i < n_buffers; ++i)
                        if (-1 == munmap(buffers[i].start, buffers[i].length))
                                errno_exit("munmap");
                break;

        case IO_METHOD_USERPTR:
                for (i = 0; i < n_buffers; ++i)
                        free(buffers[i].start);
                break;
        }

        free(buffers);
}

static void init_read(unsigned int buffer_size)
{
        buffers = calloc(1, sizeof(*buffers));

        if (!buffers)
        {
                fprintf(stderr, "Out of memory\n");
                exit(EXIT_FAILURE);
        }

        buffers[0].length = buffer_size;
        buffers[0].start = malloc(buffer_size);

        if (!buffers[0].start)
        {
                fprintf(stderr, "Out of memory\n");
                exit(EXIT_FAILURE);
        }
}

static void init_mmap(void)
{
        struct v4l2_requestbuffers req;

        CLEAR(req);

        req.count = 6;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req))
        {
                if (EINVAL == errno)
                {
                        fprintf(stderr, "%s does not support "
                                 "memory mapping\n", dev_name);
                        exit(EXIT_FAILURE);
                } else
                {
                        errno_exit("VIDIOC_REQBUFS");
                }
        }

        if (req.count < 2)
        {
                fprintf(stderr, "Insufficient buffer memory on %s\n", dev_name);
                exit(EXIT_FAILURE);
        }

        buffers = calloc(req.count, sizeof(*buffers));

        if (!buffers)
        {
                fprintf(stderr, "Out of memory\n");
                exit(EXIT_FAILURE);
        }

        for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
                struct v4l2_buffer buf;

                CLEAR(buf);

                buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory      = V4L2_MEMORY_MMAP;
                buf.index       = n_buffers;

                if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
                        errno_exit("VIDIOC_QUERYBUF");

                buffers[n_buffers].length = buf.length;
                buffers[n_buffers].start =
                        mmap(NULL /* start anywhere */,
                              buf.length,
                              PROT_READ | PROT_WRITE /* required */,
                              MAP_SHARED /* recommended */,
                              fd, buf.m.offset);

                if (MAP_FAILED == buffers[n_buffers].start)
                        errno_exit("mmap");
        }
}

static void init_userp(unsigned int buffer_size)
{
        struct v4l2_requestbuffers req;

        CLEAR(req);

        req.count  = 4;
        req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_USERPTR;

        if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
                if (EINVAL == errno) {
                        fprintf(stderr, "%s does not support "
                                 "user pointer i/o\n", dev_name);
                        exit(EXIT_FAILURE);
                } else {
                        errno_exit("VIDIOC_REQBUFS");
                }
        }

        buffers = calloc(4, sizeof(*buffers));

        if (!buffers) {
                fprintf(stderr, "Out of memory\n");
                exit(EXIT_FAILURE);
        }

        for (n_buffers = 0; n_buffers < 4; ++n_buffers) {
                buffers[n_buffers].length = buffer_size;
                buffers[n_buffers].start = malloc(buffer_size);

                if (!buffers[n_buffers].start) {
                        fprintf(stderr, "Out of memory\n");
                        exit(EXIT_FAILURE);
                }
        }
}

static void init_device(void)
{
    struct v4l2_capability cap;
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    unsigned int min;

    if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap))
    {
        if (EINVAL == errno) {
            fprintf(stderr, "%s is no V4L2 device\n",
                     dev_name);
            exit(EXIT_FAILURE);
        }
        else
        {
                errno_exit("VIDIOC_QUERYCAP");
        }
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        fprintf(stderr, "%s is no video capture device\n",
                 dev_name);
        exit(EXIT_FAILURE);
    }

    switch (io)
    {
        case IO_METHOD_READ:
            if (!(cap.capabilities & V4L2_CAP_READWRITE))
            {
                fprintf(stderr, "%s does not support read i/o\n",
                         dev_name);
                exit(EXIT_FAILURE);
            }
            break;

        case IO_METHOD_MMAP:
        case IO_METHOD_USERPTR:
            if (!(cap.capabilities & V4L2_CAP_STREAMING))
            {
                fprintf(stderr, "%s does not support streaming i/o\n",
                         dev_name);
                exit(EXIT_FAILURE);
            }
            break;
    }


    /* Select video input, video standard and tune here. */


    CLEAR(cropcap);

    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap))
    {
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c = cropcap.defrect; /* reset to default */

        if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop))
        {
            switch (errno)
            {
                case EINVAL:
                    /* Cropping not supported. */
                    break;
                default:
                    /* Errors ignored. */
                        break;
            }
        }

    }
    else
    {
        /* Errors ignored. */
    }


    CLEAR(fmt);

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (force_format)
    {
        printf("FORCING FORMAT\n");
        fmt.fmt.pix.width       = HRES;
        fmt.fmt.pix.height      = VRES;

        // Specify the Pixel Coding Formate here

        // This one works for Logitech C200
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;

        //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
        //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_VYUY;

        // Would be nice if camera supported
        //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_GREY;
        //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;

        //fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
        fmt.fmt.pix.field       = V4L2_FIELD_NONE;

        if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
                errno_exit("VIDIOC_S_FMT");

        /* Note VIDIOC_S_FMT may change width and height. */
    }
    else
    {
        printf("ASSUMING FORMAT\n");
        /* Preserve original settings as set by v4l2-ctl for example */
        if (-1 == xioctl(fd, VIDIOC_G_FMT, &fmt))
                    errno_exit("VIDIOC_G_FMT");
    }

    /* Buggy driver paranoia. */
    min = fmt.fmt.pix.width * 2;
    if (fmt.fmt.pix.bytesperline < min)
            fmt.fmt.pix.bytesperline = min;
    min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
    if (fmt.fmt.pix.sizeimage < min)
            fmt.fmt.pix.sizeimage = min;

    switch (io)
    {
        case IO_METHOD_READ:
            init_read(fmt.fmt.pix.sizeimage);
            break;

        case IO_METHOD_MMAP:
            init_mmap();
            break;

        case IO_METHOD_USERPTR:
            init_userp(fmt.fmt.pix.sizeimage);
            break;
    }
}


static void close_device(void)
{
        if (-1 == close(fd))
                errno_exit("close");

        fd = -1;
}

static void open_device(void)
{
        struct stat st;

        if (-1 == stat(dev_name, &st)) {
                fprintf(stderr, "Cannot identify '%s': %d, %s\n",
                         dev_name, errno, strerror(errno));
                exit(EXIT_FAILURE);
        }

        if (!S_ISCHR(st.st_mode)) {
                fprintf(stderr, "%s is no device\n", dev_name);
                exit(EXIT_FAILURE);
        }

        fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

        if (-1 == fd) {
                fprintf(stderr, "Cannot open '%s': %d, %s\n",
                         dev_name, errno, strerror(errno));
                exit(EXIT_FAILURE);
        }
}

static void usage(FILE *fp, int argc, char **argv)
{
        fprintf(fp,
                 "Usage: %s [options]\n\n"
                 "Version 1.3\n"
                 "Options:\n"
                 "-d | --device name   Video device name [%s]\n"
                 "-h | --help          Print this message\n"
                 "-m | --mmap          Use memory mapped buffers [default]\n"
                 "-r | --read          Use read() calls\n"
                 "-u | --userp         Use application allocated buffers\n"
                 "-o | --output        Outputs stream to stdout\n"
                 "-f | --format        Force format to 640x480 GREY\n"
                 "-c | --count         Number of frames to grab [%i]\n"
                 "",
                 argv[0], dev_name, frame_count);
}

static const char short_options[] = "d:hmruofc:";

static const struct option
long_options[] = {
        { "device", required_argument, NULL, 'd' },
        { "help",   no_argument,       NULL, 'h' },
        { "mmap",   no_argument,       NULL, 'm' },
        { "read",   no_argument,       NULL, 'r' },
        { "userp",  no_argument,       NULL, 'u' },
        { "output", no_argument,       NULL, 'o' },
        { "format", no_argument,       NULL, 'f' },
        { "count",  required_argument, NULL, 'c' },
        { 0, 0, 0, 0 }
};

int main(int argc, char **argv)
{
    if(argc > 1)
        dev_name = argv[1];
    else
        dev_name = "/dev/video0";


    for (;;)
    {
        int idx;
        int c;

        c = getopt_long(argc, argv,
                    short_options, long_options, &idx);

        if (-1 == c)
            break;

        switch (c)
        {
            case 0: /* getopt_long() flag */
                break;

            case 'd':
                dev_name = optarg;
                break;

            case 'h':
                usage(stdout, argc, argv);
                exit(EXIT_SUCCESS);

            case 'm':
                io = IO_METHOD_MMAP;
                break;

            case 'r':
                io = IO_METHOD_READ;
                break;

            case 'u':
                io = IO_METHOD_USERPTR;
                break;

            case 'o':
                out_buf++;
                break;

            case 'f':
                force_format++;
                break;

            case 'c':
                errno = 0;
                frame_count = strtol(optarg, NULL, 0);
                if (errno)
                        errno_exit(optarg);
                break;

            default:
                usage(stderr, argc, argv);
                exit(EXIT_FAILURE);
        }
    }

    int i, rc, flags = 0;

    pthread_t threads[NUM_THREADS];
    thread_params_t threadParams[NUM_THREADS];
    pthread_attr_t rt_sched_attr[NUM_THREADS];
    struct sched_param rt_param[NUM_THREADS];
    int rt_max_prio;

    struct sched_param main_param;
    pthread_attr_t main_attr;

    if (sem_init (&semProc, 0, 0)) { printf ("Failed to initialize main processing semaphore\n"); exit (-1); }

    rt_max_prio = sched_get_priority_max(SCHED_FIFO);

    sched_getparam(getpid(), &main_param);
    main_param.sched_priority = rt_max_prio;
    sched_setscheduler(getpid(), SCHED_FIFO, &main_param);

    for (i = 0; i < NUM_THREADS; ++i)
    {
        pthread_attr_init(&rt_sched_attr[i]);
        pthread_attr_setinheritsched(&rt_sched_attr[i], PTHREAD_EXPLICIT_SCHED);
        pthread_attr_setschedpolicy(&rt_sched_attr[i], SCHED_FIFO);

        rt_param[i].sched_priority = rt_max_prio - 1;
        pthread_attr_setschedparam(&rt_sched_attr[i], &rt_param[i]);
    }

    rc = pthread_create(&threads[0],
                        &rt_sched_attr[0],
                        mainloop,
                        (void*)&(threadParams[0])
                    );

    if (rc < 0)
    {
        perror("Failed to start main pthread\n");
        exit(1);
    }

    // initialization of V4L2
    open_device();
    init_device();

    start_capturing();

    // Create and start timer
    timer_create(CLOCK_MONOTONIC, NULL, &sequencer_timer);

    signal(SIGALRM, (void(*)()) Sequencer);

    itime.it_interval.tv_sec = 0;
    itime.it_interval.tv_nsec = 50000000; // 0.05 second timer for 20Hz
    itime.it_value.tv_sec = 0;
    itime.it_value.tv_nsec = 50000000; // 0.05 second timer for 20Hz

    timer_settime(sequencer_timer, flags, &itime, &last_itime);

    for (i = 0; i < NUM_THREADS; ++i)
    {
        pthread_join(threads[i], NULL);
    }

    // shutdown of frame acquisition service
    stop_capturing();

    printf("Total capture time=%lf, for %d frames, %lf FPS\n", (fstop-fstart), CAPTURE_FRAMES+1, ((double)CAPTURE_FRAMES / (fstop-fstart)));

    uninit_device();
    close_device();
    fprintf(stderr, "\n");
    return 0;
}

