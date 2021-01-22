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

#define SIZE64  	64
#define SIZE512  	512

#define UNZIP_CMD 				"unzip %s \"%s\" -d /tmp/"
#define CONFIG_PATH				"/tmp/config.ini"
#define ROOTFS_PATH				"/tmp/rootfs.bin"
#define ROOTFS_DEV_PATH			"/dev/mtd4"
#define KERNEL_PATH				"/tmp/kernel.bin"
#define KERNEL_DEV_PATH			"/dev/mtd3"
#define USEDAT_PATH				"/tmp/userdata.bin"
#define VERSION_PATH			"/opt/qcy/os-release"
static int boot_mode_flag = 0;
static char buffer_flag[SIZE512] = {0};
static int erase_mtd(int dev_fd, struct erase_info_user *erase, char *device);
static int verify_block(int file_fb, int dev_fb, size_t size, char *file_path, char *dev_path);
static int write_block(int file_fb, int dev_fb, size_t size, char *file_path, char *dev_path);
static int get_update_dev_path(char *dev_path);
static int check_umount(char *dev_path);
static int write_flag();
static int get_mtd_num(char *path, int *num);



static int verify_block(int file_fb, int dev_fb, size_t size, char *file_path, char *dev_path)
{
    size_t size_tmp = size;
    int i = BUFSIZE;
    ssize_t result;

    unsigned char src[BUFSIZE],dest[BUFSIZE];

    if (lseek(file_fb,0,SEEK_SET) < 0)
    {
		log_qcy(DEBUG_SERIOUS,"seeking failed");
        return -1;
    }

    if (lseek(dev_fb,0,SEEK_SET) < 0)
    {
		log_qcy(DEBUG_SERIOUS,"seeking failed");
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
        		log_qcy(DEBUG_SERIOUS,"While reading data from %s\n",file_path);
                return -1;
            }
    		log_qcy(DEBUG_SERIOUS,"Short read count returned while reading from %s\n",file_path);
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
    		log_qcy(DEBUG_SERIOUS,"Short read count returned while reading from %s\n",dev_path);
            return -1;
        }

        if (memcmp(src,dest,i))
        {
    		log_qcy(DEBUG_SERIOUS,"File does not seem to match flash data.\n");
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
        		log_qcy(DEBUG_SERIOUS,"While reading data from %s\n",file_path);
                return -1;
            }
    		log_qcy(DEBUG_SERIOUS,"Short read count returned while reading from %s\n",file_path);
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
    		log_qcy(DEBUG_SERIOUS,"Short write count returned while writing from %s\n",dev_path);
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
		log_qcy(DEBUG_SERIOUS,"While erasing blocks from 0x%.8x-0x%.8x on %s\n",
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
    if(device_path == NULL || filename == NULL)
        	return -1;
    memset(&filestat, 0, sizeof(filestat));
    memset(&mtd, 0, sizeof(mtd));
    memset(&erase, 0, sizeof(erase));

    dev_fd = open (device_path,O_SYNC | O_RDWR);
    if (dev_fd < 0)
    {
		log_qcy(DEBUG_SERIOUS,"open device error\n");
       ret=-1;
       goto error;
    }

    if (ioctl(dev_fd,MEMGETINFO,&mtd) < 0)
    {
		log_qcy(DEBUG_SERIOUS,"ioctl MEMGETINFO\n");
		log_qcy(DEBUG_SERIOUS,"This doesn't seem to be a valid MTD flash device!\n");
        ret=-1;
        goto error;
    }

    file_fd = open (filename,O_SYNC | O_RDONLY);
    if (file_fd < 0)
    {
		log_qcy(DEBUG_SERIOUS,"open device error\n");
       ret=-1;
       goto error;
    }

    if (fstat(file_fd,&filestat) < 0)
    {
		log_qcy(DEBUG_SERIOUS,"While trying to get the file status of %s: %m\n",filename);
        ret=-1;
        goto error;
    }

    if (filestat.st_size > mtd.size)
    {
		log_qcy(DEBUG_SERIOUS,"%s won't fit into %s!\n",filename,device_path);
        ret=-1;
        goto error;
    }

    erase.start = 0;
    erase.length = (filestat.st_size + mtd.erasesize - 1) / mtd.erasesize;
    erase.length *= mtd.erasesize;

    ret = erase_mtd(dev_fd, &erase, device_path);
    if(ret)
    {
		log_qcy(DEBUG_SERIOUS,"erase failed\n");
        goto error;
    }

    ret = write_block(file_fd, dev_fd, filestat.st_size, filename, device_path);
    if(ret)
    {
		log_qcy(DEBUG_SERIOUS,"block write failed\n");
        goto error;
    }

    ret = verify_block(file_fd, dev_fd, filestat.st_size, filename, device_path);
    if(ret)
    {
		log_qcy(DEBUG_SERIOUS,"verify block failed\n");
        goto error;
    }

error:
    if (dev_fd > 0) close (dev_fd);
    if (file_fd > 0) close (file_fd);
    return ret;
}

static int get_mtd_num(char *path, int *num)
{
	FILE *fd = NULL;
	char buffer[1024] = {0};

	fd = fopen("/proc/mtd", "r");
	if(!fd)
	{
		log_qcy(DEBUG_SERIOUS,"fopen /proc/mtd error\n");
		return -1;
	}

	while(fgets(buffer, sizeof(buffer) ,fd))
	{
		if(strstr(buffer, path))
		{
			*num = (int)(buffer[3] - 48);
			log_qcy(DEBUG_SERIOUS,"num=%d\n",*num);
			break;
		}
		memset(buffer, 0 , sizeof(buffer));
	}

	fclose(fd);
	return 0;
}

static int get_update_dev_path(char *dev_path)
{
    int ret;
    int cmdf_fd;
    int path_num = 0;
    memset(buffer_flag,0,sizeof(buffer_flag));

    cmdf_fd = open(CMDLINE_PATH, O_RDONLY);
    if (cmdf_fd < 0)
    {
    	log_qcy(DEBUG_SERIOUS,"open device error path = %s\n", CMDLINE_PATH);
       return -1;
    }

    ret = read(cmdf_fd,buffer_flag,SIZE512);
    if (ret < 0)
    {
    	log_qcy(DEBUG_SERIOUS,"While reading data from %s\n",CMDLINE_PATH);
        close(cmdf_fd);
        return -1;
    }

    close(cmdf_fd);

    if(strstr(buffer_flag, "boot_mode=1"))
    {

    	if(get_mtd_num("userdata_a", &path_num))
    	{
    		log_qcy(DEBUG_SERIOUS,"get part num error\n");
    		return -1;
    	}

    	snprintf(dev_path, 12, "/dev/mtd%d", path_num);
        boot_mode_flag = 1;
    } else {

    	if(get_mtd_num("userdata_b", &path_num))
    	{
    		log_qcy(DEBUG_SERIOUS,"get part num error\n");
    		return -1;
    	}

    	snprintf(dev_path, 12, "/dev/mtd%d", path_num);
        boot_mode_flag = 0;
    }

	log_qcy(DEBUG_INFO,"update dev_path : %s\n",dev_path);
    return 0;
}

static int write_flag()
{
    char cmd[SIZE512] = {0};
    char buffer_tmp[SIZE512] = {0};
    char *p1 = NULL;
    char *p2 = buffer_flag;
    char *p3 = buffer_tmp;


    if(buffer_flag != NULL)
    {
        p1 = strstr(buffer_flag, "boot_mode=");
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


static int get_version_flag(char *config_path, int *version, int *kernel_flag, int *rootfs_flag, int *user_flag)
{
	if(config_path == NULL ||
			version == NULL ||
			kernel_flag == NULL ||
			rootfs_flag == NULL ||
			user_flag == NULL)
		return -1;

	FILE *fd = NULL;
	char buffer[100] = {0};
	char verson_tmp[6] = {0};
	fd = fopen(config_path, "r");
	if(!fd)
	{
		log_qcy(DEBUG_SERIOUS,"fopen /proc/mtd error\n");
		return -1;
	}

	while(fgets(buffer, sizeof(buffer) ,fd))
	{
		if(strstr(buffer, "version"))
		{
			strncpy(verson_tmp, buffer+14, 4);
			*version = atoi(verson_tmp);
		}
		else if(strstr(buffer, "kernel"))
		{
			*kernel_flag = (int)(buffer[7] - 48);
		}
		else if(strstr(buffer, "rootfs"))
		{
		*rootfs_flag = (int)(buffer[7] - 48);
		}
		else if(strstr(buffer, "usedat"))
		{
			*user_flag = (int)(buffer[7] - 48);
		}
		memset(buffer, 0 , sizeof(buffer));
	}

	fclose(fd);
	return 0;
}
static int check_version(int version)
{
	FILE *fd = NULL;
	char buffer[100] = {0};
	int version_tmp = -1;
	char tmp[8] = {0};

	if(version <= 0)
	{
		log_qcy(DEBUG_SERIOUS,"package version error\n");
		return -1;
	}

	fd = fopen(VERSION_PATH, "r");
	if(!fd)
	{
		log_qcy(DEBUG_SERIOUS,"fopen os-release error\n");
		return -1;
	}

	fgets(buffer, sizeof(buffer) ,fd);
	fclose(fd);

	strncpy(tmp, buffer+18, 4);
	version_tmp = atoi(tmp);

	if(version > version_tmp)
		return 0;
	else
	{
		log_qcy(DEBUG_INFO,"version check filed, device version %d,package version %d|\n",version_tmp, version);
		return -1;
	}
}

int ota_process_main(char *arg)
{
    int ret = 0;

    char dev_path[SIZE64]={0};
    char unzip_cmd[SIZE64]={0};
    char *file_path = arg;
    int version = -1;
    int kernel_ota = -1;
    int rootfs_ota = -1;
    int userdata_ota = -1;
	log_qcy(DEBUG_SERIOUS,"into  ota_process_main----------------\n");
		//------------------------get config.ini
		memset(unzip_cmd, 0 ,SIZE64);
		snprintf(unzip_cmd, SIZE64, UNZIP_CMD, file_path, "config.ini");
		system(unzip_cmd);
		if(!access(CONFIG_PATH, R_OK))
		{
			ret = get_version_flag(CONFIG_PATH, &version, &kernel_ota, &rootfs_ota, &userdata_ota);
			if(ret)
			{
				log_qcy(DEBUG_SERIOUS,"get config failed, please check ota package\n");
				remove(CONFIG_PATH);
				return -1;
			}else
				log_qcy(DEBUG_INFO, "get config.ini success\n");
		}
		else
		{
			log_qcy(DEBUG_SERIOUS,"can not get config, please check ota package\n");
			return -1;
		}
		remove(CONFIG_PATH);
		ret = check_version(version);
		if(ret < 0)
			return -1;

			//------------------------update kernel
			if(kernel_ota == 1)
			{
				memset(unzip_cmd, 0 ,SIZE64);
				snprintf(unzip_cmd, SIZE64, UNZIP_CMD, file_path, "kernel.bin");
				system(unzip_cmd);
				if(!access(KERNEL_PATH, R_OK))
				{
				    ret = ota_update_begin(KERNEL_DEV_PATH, KERNEL_PATH);
				    if(ret)
				    {
				    	log_qcy(DEBUG_SERIOUS, "ota update failed\n");
				    	remove(KERNEL_PATH);
				        return -1;
				    }else
				    	log_qcy(DEBUG_INFO, "ota update kernel success\n");
				}
			else
				{
					log_qcy(DEBUG_SERIOUS,"can not get kernel.ini, please check ota package\n");
					return -1;
				}
				remove(KERNEL_PATH);
				log_qcy(DEBUG_SERIOUS, "kernel_ota  ok1\n");
			}

			//------------------------update rootfs
			if(rootfs_ota == 1)
			{
				memset(unzip_cmd, 0 ,SIZE64);
				snprintf(unzip_cmd, SIZE64, UNZIP_CMD, file_path, "rootfs.bin");
				system(unzip_cmd);
				if(!access(ROOTFS_PATH, R_OK))
				{
				    ret = ota_update_begin(ROOTFS_DEV_PATH, ROOTFS_PATH);
				    if(ret)
				    {
				    	log_qcy(DEBUG_SERIOUS, "ota update failed\n");
				    	remove(ROOTFS_PATH);
				        return -1;
				    }else
			    	log_qcy(DEBUG_INFO, "ota update rootfs success\n");
				}
				else
				{
					log_qcy(DEBUG_SERIOUS,"can not get kernel.ini, please check ota package\n");
					return -1;
				}
				remove(ROOTFS_PATH);
				log_qcy(DEBUG_SERIOUS, "rootfs_ota  ok1\n");
			}

			if(userdata_ota == 1)
			{
				//find which block need update
				memset(unzip_cmd, 0 ,SIZE64);
				snprintf(unzip_cmd, SIZE64, UNZIP_CMD, file_path, "userdata.bin");
				system(unzip_cmd);

				if(!access(USEDAT_PATH, R_OK))
				{
					ret = get_update_dev_path(dev_path);
					if(ret)
					{
						log_qcy(DEBUG_SERIOUS, "get_update_dev_path failed\n");
						return -1;
					}

					log_qcy(DEBUG_INFO, "dev_path = %s, file_path = %s\n", dev_path, USEDAT_PATH);

				ret = ota_update_begin(dev_path, USEDAT_PATH);
					if(ret)
					{
						log_qcy(DEBUG_SERIOUS, "ota update failed\n");
						return -1;
					}

					ret = write_flag();
				}
				else
				{
					log_qcy(DEBUG_SERIOUS,"can not get kernel.ini, please check ota package\n");
					return -1;
				}
				remove(USEDAT_PATH);
				log_qcy(DEBUG_SERIOUS, "userdata_ota  ok!\n");
			}
			log_qcy(DEBUG_INFO, "ota_process_main  end ret = %d\n",ret);
    return ret;
}
