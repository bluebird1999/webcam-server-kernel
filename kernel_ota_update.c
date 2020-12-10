#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <mtd/mtd-user.h>
#include "../../tools/tools_interface.h"
#include "../../manager/manager_interface.h"
#define BUFSIZE (10 * 1024)
#define CMDLINE_PATH "/proc/cmdline"

#define SIZE64  64
#define SIZE512  512

static int boot_mode_flag = 0;
static char buffer[SIZE512] = {0};
static int erase_mtd(int dev_fd, struct erase_info_user *erase, char *device);
static int verify_block(int file_fb, int dev_fb, size_t size, char *file_path, char *dev_path);
static int write_block(int file_fb, int dev_fb, size_t size, char *file_path, char *dev_path);
static int get_update_dev_path(char *dev_path);
static int check_umount(char *dev_path);
static int write_flag();



static int verify_block(int file_fb, int dev_fb, size_t size, char *file_path, char *dev_path)
{
    size_t size_tmp = size;
    int i = BUFSIZE;
    ssize_t result;

    unsigned char src[BUFSIZE],dest[BUFSIZE];

    if (lseek(file_fb,0,SEEK_SET) < 0)
    {
        printf("seeking failed");
        return -1;
    }

    if (lseek(dev_fb,0,SEEK_SET) < 0)
    {
        printf("seeking failed");
        return -1;
    }

    while(size_tmp)
    {
        if (size_tmp < BUFSIZE) i = size_tmp;

        result = read(file_fb,src,i);
        if (i != result)
        {
            if (result < 0)
            {
                printf("While reading data from %s\n",file_path);
                return -1;
            }
            printf("Short read count returned while reading from %s\n",file_path);
            return -1;
        }

        result = read(dev_fb,dest,i);
        if (i != result)
        {
            if (result < 0)
            {
                printf("While reading data from %s\n",dev_path);
                return -1;
            }
            printf("Short read count returned while reading from %s\n",dev_path);
            return -1;
        }

        if (memcmp(src,dest,i))
        {
            printf("File does not seem to match flash data.\n");
            return -1;
        }

        size_tmp -= i;
    }
    return 0;
}

static int write_block(int file_fb, int dev_fb, size_t size, char *file_path, char *dev_path)
{
    size_t size_tmp = size;
    int i = BUFSIZE;
    ssize_t result;
    unsigned char src[BUFSIZE];

    while(size_tmp)
    {
        if (size_tmp < BUFSIZE) i = size_tmp;

        result = read(file_fb,src,i);
        if (i != result)
        {
            if (result < 0)
            {
                printf("While reading data from %s\n",file_path);
                return -1;
            }
            printf("Short read count returned while reading from %s\n",file_path);
            return -1;
        }

        result = write(dev_fb,src,i);
        if (i != result)
        {
            if (result < 0)
            {
                printf("While writing data from %s\n",dev_path);
                return -1;
            }
            printf("Short write count returned while writing from %s\n",dev_path);
            return -1;
        }

        size_tmp -= i;
    }
    return 0;
}

static int erase_mtd(int dev_fd, struct erase_info_user *erase, char *device)
{

    if (ioctl (dev_fd,MEMERASE,erase) < 0)
    {
        printf("While erasing blocks from 0x%.8x-0x%.8x on %s\n",
                (unsigned int) erase->start,(unsigned int) (erase->start + erase->length),device);
        return -1;
    }
    return 0;
}

static int ota_update_begin(char *device_path, char *filename)
{
    int ret = 0;
    int dev_fd = 0;
    int file_fd = 0;

    struct stat filestat;
    struct mtd_info_user mtd;
    struct erase_info_user erase;

    memset(&filestat, 0, sizeof(filestat));
    memset(&mtd, 0, sizeof(mtd));
    memset(&erase, 0, sizeof(erase));

    dev_fd = open (device_path,O_SYNC | O_RDWR);
    if (dev_fd < 0)
    {
       printf("open device error\n");
       goto error;
    }

    if (ioctl(dev_fd,MEMGETINFO,&mtd) < 0)
    {
        printf("ioctl MEMGETINFO\n");
        printf("This doesn't seem to be a valid MTD flash device!\n");
        goto error;
    }

    file_fd = open (filename,O_SYNC | O_RDONLY);
    if (file_fd < 0)
    {
       printf("open device error\n");
       goto error;
    }

    if (fstat(file_fd,&filestat) < 0)
    {
        printf("While trying to get the file status of %s: %m\n",filename);
        goto error;
    }

    if (filestat.st_size > mtd.size)
    {
        printf("%s won't fit into %s!\n",filename,device_path);
        goto error;
    }

    erase.start = 0;
    erase.length = (filestat.st_size + mtd.erasesize - 1) / mtd.erasesize;
    erase.length *= mtd.erasesize;

    ret = erase_mtd(dev_fd, &erase, device_path);
    if(ret)
    {
        printf("erase failed\n");
        goto error;
    }

    ret = write_block(file_fd, dev_fd, filestat.st_size, filename, device_path);
    if(ret)
    {
        printf("block write failed\n");
        goto error;
    }

    ret = verify_block(file_fd, dev_fd, filestat.st_size, filename, device_path);
    if(ret)
    {
        printf("verify block failed\n");
        goto error;
    }

error:
    if (dev_fd > 0) close (dev_fd);
    if (file_fd > 0) close (file_fd);
    return ret;
}

static int get_update_dev_path(char *dev_path)
{
    int ret;
    int cmdf_fd;

    cmdf_fd = open(CMDLINE_PATH, O_RDONLY);
    if (cmdf_fd < 0)
    {
       printf("open device error path = %s\n", CMDLINE_PATH);
       return -1;
    }

    ret = read(cmdf_fd,buffer,SIZE512);
    if (ret < 0)
    {
        printf("While reading data from %s\n",CMDLINE_PATH);
        close(cmdf_fd);
        return -1;
    }

    close(cmdf_fd);

    if(strstr(buffer, "boot_mode=1"))
    {
        strncpy(dev_path, "/dev/mtd7", 9);
        boot_mode_flag = 1;
    } else {
        strncpy(dev_path, "/dev/mtd8", 9);
        boot_mode_flag = 0;
    }

    return 0;
}

static int write_flag()
{
    char cmd[SIZE512] = {0};
    char buffer_tmp[SIZE512] = {0};
    char *p1 = NULL;
    char *p2 = buffer;
    char *p3 = buffer_tmp;


    if(buffer != NULL)
    {
        p1 = strstr(buffer, "boot_mode=");
    }

    while(p2 != p1)
    {
        *p3 = *p2;
        p2++;
        p3++;
    }

    //printf("write_flag buffer_tmp = %s\n", buffer_tmp);

    if(boot_mode_flag == 1)
    {
        snprintf(cmd, SIZE512, "fw_setenv bootargs \"%sboot_mode=%d\"", buffer_tmp, 0);
    } else {
        snprintf(cmd, SIZE512, "fw_setenv bootargs \"%sboot_mode=%d\"", buffer_tmp, 1);
    }


	log_qcy(DEBUG_INFO, "write_flag  cmd = %s\n", cmd);

    system(cmd);

    return 0;
}

int ota_process_main(char *arg)
{
    int ret = 0;

    char dev_path[SIZE64];
    char *file_path = arg;
	log_qcy(DEBUG_SERIOUS,"into  ota_process_main----------------\n");
    //find which block need update
    ret = get_update_dev_path(dev_path);
    if(ret)
    {
        printf("get_update_dev_path failed\n");
        return -1;
    }

    printf("dev_path = %s, file_path = %s\n", dev_path, file_path);

    ret = ota_update_begin(dev_path, file_path);
    if(ret)
    {
        printf("ota update failed\n");
        return -1;
    }

    ret = write_flag();
	log_qcy(DEBUG_INFO, "ota_process_main  ret = %d\n",ret);
    return ret;
}
