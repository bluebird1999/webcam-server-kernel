/*
 * kernel_ota.c
 *
 *  Created on: 2020年11月1日
 *      Author: kang
 */
/*
 * header
 */
//system header
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <stdbool.h>
#include <limits.h>
#include <time.h>
#include <json-c/json.h>
#include <string.h>
#include <unistd.h>
//program header
#include "../../manager/manager_interface.h"
#include "../../tools/tools_interface.h"
#include "../../server/miio/miio_interface.h"
//server heade
#include "kernel.h"
#include "../../tools/config/rwio.h"
#include "../../tools/log.h"
#include "kernel_ota.h"
#include "MD5.h"


/*
 * static
 */
//variable
static kernel_ota_config_t	config;
static pthread_rwlock_t			lock;
static int open_config_flag=0;
static config_map_t  kernel_ota_config_profile_map[] = {
    {"ota_status",     &(config.status), cfg_s32, 6,0, 0,7,	},
    {NULL,},
};
//function
static int my_system(const char * cmd);
static int ota_config_save(void);
static int config_ota_set(void *arg);
static void *get_progress_thread(void *arg);
static void *dowm_func(void *arg);
static void *ota_install_thread(void *arg);

//static int ota_set_status(int model);
/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * helper
 */
/*
static int ota_set_status( int model)
{
	int ret=-1;
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_err("add lock fail, ret = %d", ret);
		return ret;
	}
	config.status=model;
	ret = pthread_rwlock_unlock(&lock);
	if (ret)
		log_err("add unlock fail, ret = %d", ret);
	return ret;
}
*/
static int ota_config_save(void)
{
	int ret = 0;
	//message_t msg;
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_err("add lock fail, ret = %d\n", ret);
		return ret;
	}
	if( open_config_flag == 1 ) {
		log_err("int ------------ota_config_save------------ \n");
		ret = write_config_file(kernel_ota_config_profile_map, CONFIG_KERNEL_OTA_PATH);
		if(!ret)
			open_config_flag=0;
	}

	ret = pthread_rwlock_unlock(&lock);
	if (ret)
		log_err("add unlock fail, ret = %d\n", ret);

	return ret;
}

static int config_ota_set(void *arg)
{
	int ret = 0;
//	ret = pthread_rwlock_wrlock(&lock);
//	if(ret)	{
//		log_err("add lock fail, ret = %d\n", ret);
//		return ret;
//	}
//		message_t msg;
//	    /********message body********/
//		msg_init(&msg);
//		msg.message = MSG_MANAGER_TIMER_ADD;
//		msg.sender = SERVER_KERNEL;
//		msg.arg_in.cat = FILE_FLUSH_TIME;	//1min
//		msg.arg_in.dog = 0;
//		msg.arg_in.duck = 0;
//		msg.arg_in.handler = &ota_config_save;
//		/****************************/
//		manager_message(&msg);
	memcpy( (kernel_ota_config_t*)(&config.status), arg, sizeof(kernel_ota_config_t));
	ota_config_save();

//	ret = pthread_rwlock_unlock(&lock);
//	if (ret)
//		log_err("add unlock fail, ret = %d\n", ret);

	return ret;
}

static int my_system(const char * cmd)
{
			FILE *fp=NULL;
			int res; char buf[1024]={0};
			if (cmd == NULL)
			{
			log_info("my_system cmd is NULL!\n");
			 return -1;
			 }
			if ((fp = popen(cmd, "r") ) == NULL)
			{
				perror("popen");
				return -1;
			}
			else
			 {
				while(fgets(buf, sizeof(buf), fp))
				{
					log_info("%s", buf);
				}
				if ( (res = pclose(fp)) == -1)
				{
				log_info("close popen file pointer fp error!\n");
				return res;
				}
					else if (res == 0)
				{
				return res;
				}
				else
				{
					log_info("popen res is :%d\n", res);
					return res;
				}
			 }
 }
static int mv_ota_datafile(char *dest_path,char * src_path);
/*
 * interface
 */
static int creat_get_progress_thread(void)
{
	int ret;
	pthread_t progress_id;
	ret = pthread_create(&progress_id,NULL,get_progress_thread,NULL);
	if(ret != 0) {
			log_err("creat_get_progress_thread thread create error! ret = %d",ret);
			 return -1;
		 }
		return 0;
}
static int set_reboot()
{
	int ret;
	char *cmd="reboot";
	log_info("into func set_reboot \n");
    ret=my_system(cmd);
    if(ret == 0)  return 0;
    else  return -1;

}
static int mv_ota_datafile(char *dest_path,char * src_path)
{
	FILE *dest_fp=NULL;
	FILE *src_fp=NULL;
	int filesize,ret;
	char *data=NULL;
	char dest_filemd5[64]={0};

	src_fp = fopen(src_path,"r");
	if(src_fp == NULL)
	{
		return -1;
	}
	if( fseek(src_fp,0,SEEK_END) != 0)
	{
		fclose(src_fp);
		return -1;
	}
	filesize=ftell(src_fp);
	if(filesize>0)
	{
		data=malloc(filesize);
		if(!data) {
					fclose(src_fp);
					return -1;
				}
			memset(data, 0, filesize);
			if(0 != fseek(src_fp, 0, SEEK_SET)) {
				free(data);
				fclose(src_fp);
				return -1;
			}
			if (fread(data, 1, filesize, src_fp) != (filesize)) {
				free(data);
				fclose(src_fp);
				return -1;

			}
	}
		fclose(src_fp);

		dest_fp = fopen(dest_path,"w+");
		if(dest_fp == NULL)
		{
			return -1;
		}
		if(0 != fseek(dest_fp, 0, SEEK_SET)) {
			fclose(dest_fp);
			return -1;
		}
		if (fwrite(data, 1, filesize, dest_fp) != (filesize)) {
			fclose(dest_fp);
			return -1;
		}

		fclose(dest_fp);
		//again check md5 dest file
		ret=Compute_file_md5(dest_path, dest_filemd5);
				if(ret) {
					log_info("----------get dest_path  md5 faile---------------------\n");
					return -1;
				}
				log_info("---------dest--filemd5=%s---------------\n",dest_filemd5);

				ret=strcmp(config.md5,dest_filemd5);
					log_info("------------ret=%d------------------\n",ret);
					if(!ret) {
						log_info("------md5 check ok--------\n");
					}
					else if(ret !=0) {
						log_info("----------md5 check faile------\n");
						return -1;
						}

	return 0;

}

static void *ota_install_thread(void *arg)
{
	int ret,i=0,j=0,ota_type;
	char  filemd5[64]={0};
	char cmd[64]={0};
	ota_type = *((int *)arg);
	log_info("ota_install_thread-----------------ota_type=%d\n",ota_type);
	while(1)
		{
			usleep (500000);
			if(config.status == OTA_STATE_INSTALLED)
				return 0;
			if(config.status == OTA_STATE_FAILED){
				j++;
				if(j==60)
					pthread_exit(0);
					}
			if(config.status == OTA_STATE_DOWNLOADING){
				j++;
				if(j==60)
					pthread_exit(0);
				continue;}
			if(config.status == OTA_STATE_INSTALLING){
				if(j==60)
					pthread_exit(0);
				continue;}
			if(config.status == OTA_STATE_WAIT_INSTALL)
				break;
		}
		//check ota_file md5
		if(ota_type == OTA_TYPE_APPLICATION)
		{
			ret=Compute_file_md5(OTA_DOWNLOAD_APPLICATION_NAME, filemd5);
			if(ret) {
				log_info("----------get OTA_DOWNLOAD_APPLICATION_NAME  md5 faile---------------------\n");
				config.error_msg = OTA_ERR_INSTALL_ERR;
				pthread_exit(0);
			}
		}
		// Other pending interfaces
		else if(ota_type == OTA_TYPE_MIIO_CLIENT){
		}


		ret=strcmp(config.md5,filemd5);
		if(!ret) {
			log_info("------md5 check ok--------\n");
			config.status=OTA_STATE_INSTALLING;
		}
		else if(ret !=0) {
			log_info("----------md5 check faile------\n");
			config.status=OTA_STATE_FAILED;
			config.error_msg = OTA_ERR_INSTALL_ERR;
			pthread_exit(0);
			}

		ret = mv_ota_datafile(OTA_DEST_APPLICATION_NAME,OTA_DOWNLOAD_APPLICATION_NAME);

		if(ret!=0)
		{
			while(i<2)
			{
				sleep(1);
				ret = mv_ota_datafile(OTA_DEST_APPLICATION_NAME,OTA_DOWNLOAD_APPLICATION_NAME);
				if (ret==0) break;
				i++;
			}
			if(ret!=0){
				config.status=OTA_STATE_FAILED;
				config.error_msg = OTA_ERR_INSTALL_ERR;
				ota_config_save();
				pthread_exit(0);
			}
		}
		config.status=OTA_STATE_INSTALLED;
		config.error_msg = OTA_ERR_NONE;
		//jia geizhege wenjiande quanxian
		sprintf(cmd, "chmod 777  %s", OTA_DEST_APPLICATION_NAME);
		ret=my_system(cmd);
		if(ret)
		{
			log_info("-chmod webcam failed\n");
			config.status=OTA_STATE_FAILED;
			config.error_msg = OTA_ERR_INSTALL_ERR;
			 pthread_exit(0);
		}
		usleep(500000);
		config.status=OTA_STATE_IDLE;

		//save config.status
		ret=ota_config_save();
		if(ret){
			log_info("--ota_config_save--- failed\n");
			config.status=OTA_STATE_FAILED;
			config.error_msg = OTA_ERR_INSTALL_ERR;
			 pthread_exit(0);
		}
		sleep(2);
		//send reboot cmoman
		//ret=set_reboot();
		//if(ret) {log_info("ota try reboot faile\n"); }
		log_info("------ota_install_fun-----end---\n");
		pthread_exit(0);

}

int ota_install_fun(char *url,unsigned int ulr_len,char *ota_md5,unsigned int ota_md5_len,int ota_type)
{
	pthread_t install_tid;
	int ret;
	log_info("into ota_download_state url=%s\n",url);
	log_info("into ota_download_state md5=%s\n",ota_md5);
	memcpy(config.url,url,ulr_len);
	memcpy(config.md5,ota_md5,ota_md5_len);
	log_info("into ota_download_state config.url=%s\n",config.url);
	log_info("into ota_download_state config.md5=%s\n",config.md5);
	creat_get_progress_thread();
	//if(config.status == OTA_STATE_DOWNLOADING)
	//	return 0;

	ret=pthread_create(&install_tid,NULL,ota_install_thread,&ota_type);
	if(ret != 0) {
			log_err("installl thread create error! ret = %d",ret);
			 config.error_msg=OTA_ERR_INSTALL_ERR;
			 return -1;
		 }

	return 0;

}

static void *dowm_func(void *arg)
{
	FILE *fp=NULL;
	char cmd[64]={0};
	int i=0;
	int res=0; char buf[128]={0};
	log_info("into dowm_func thread\n");
#if 1
	while(i<3){
	sleep(4);
	sprintf(cmd, "tail -n 2 %s", OTA_WGET_LOG);
	if ((fp = popen(cmd, "r") ) == NULL)
			{
				perror("popen");
				res=OTA_ERR_DOWN_ERR;
				break;
			}
		else
		 {
			while(fgets(buf, sizeof(buf), fp))
			{
				log_info("dowm_func buf=%s", buf);
				if(strstr(buf,"server returned error: HTTP/1.1 404 Not Found")!=0){
					//文件名字错误
					res = OTA_ERR_DOWN_ERR;
							  }
				if(strstr(buf,"server returned error: HTTP/1.1 416 Requested Range Not Satisfiable")!=0){
					//文件已经下载
					res = OTA_ERR_NONE;
					break;
							  }

				else if(strstr(buf,"bad address")!=0){
					  //目标地址错误
					  res =  OTA_ERR_DOWN_ERR;
				  }
				else if(strstr(buf,"can't connect to remote host")!=0){
					  //网络不可用
					  res = OTA_ERR_DOWN_ERR;
				  }
				else if(strstr(buf,"100%")!=0){
					  //无错误
					  res = OTA_ERR_NONE;
					  config.status = OTA_STATE_DOWNLOADED;
					  log_info("---100% ok-----------\n");
					  break;
				 }
				 else {
						  //下载错误
						  res = OTA_ERR_DOWN_ERR;
				 }


			}
			pclose(fp);
			memset(cmd,0,sizeof(cmd));
			i++;
			if( config.status == OTA_STATE_DOWNLOADED)
				break;
		 }

	}
#endif
			if(res != OTA_ERR_NONE)
			{
				config.status = OTA_STATE_FAILED;
			}
			else {
				config.status = OTA_STATE_WAIT_INSTALL;
			}
			config.error_msg = res;
			pthread_exit(0);
}

static void *get_progress_thread(void *arg)
{
	log_info("into get_progress_thread\n");
	while(1)
	{
		if(config.status == OTA_STATE_FAILED)
		{
			config.progress=0;
		}
		if(config.status == OTA_STATE_DOWNLOADING)
		{
			if(config.progress < 20)
			config.progress=config.progress+1;
		}
		if(config.status == OTA_STATE_DOWNLOADED)
				{
					if(config.progress < 40)
					config.progress=config.progress+1;
				}
		if(config.status == OTA_STATE_WAIT_INSTALL)
				{
					if(config.progress < 60)
					config.progress=config.progress+1;
				}
		if(config.status == OTA_STATE_INSTALLING)
				{
					if(config.progress < 80)
					config.progress=config.progress+1;
				}
		if(config.status == OTA_STATE_INSTALLED)
				{
					if(config.progress < 90)
					config.progress=config.progress+1;
				}
		if(config.status == OTA_STATE_IDLE)
				{
					if(config.progress < 100)
					config.progress=config.progress+1;
				}
		if(config.progress==100)
			break;
		usleep(70000);
	}


	 pthread_exit(0);

}

int kernel_ota_get_status()
{
	int st;
	int ret;
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_err("add lock fail, ret = %d", ret);
		return ret;
	}
	st = config.status;
	ret = pthread_rwlock_unlock(&lock);
	if (ret)
		log_err("add unlock fail, ret = %d", ret);
	return st;
}

int kernel_ota_get_error_msg()
{
	int st;
	int ret;
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_err("add lock fail, ret = %d", ret);
		return ret;
	}
	st = config.error_msg;
	ret = pthread_rwlock_unlock(&lock);
	if (ret)
		log_err("add unlock fail, ret = %d", ret);
	return st;
}
int kernel_ota_get_progress()
{
	int st;
	int ret;
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_err("add lock fail, ret = %d", ret);
		return ret;
	}
	st = config.progress;
	ret = pthread_rwlock_unlock(&lock);
	if (ret)
		log_err("add unlock fail, ret = %d", ret);
	return st;
}

int ota_dowmload_date(char *url,unsigned int ulr_len,int ota_type)
{
	int ret;
	char cmd[128]={0};
	pthread_t dowm_id;
	config.status=OTA_STATE_DOWNLOADING;
	log_info("---------down--------url=%s\n",url);
	log_info("---------down--------ulr_len=%d\n",ulr_len);
	if(ota_type == OTA_TYPE_APPLICATION)
	{
		sprintf(cmd, "wget  -c -t 3 -T 5  -O %s   %s  2>%s 1>&2   &",OTA_DOWNLOAD_APPLICATION_NAME,url,OTA_WGET_LOG);
		log_info("---------cmd--------cmd=%s\n",cmd);
	}
	//Other pending interfaces
	else if(ota_type == OTA_TYPE_MIIO_CLIENT){

	}

	ret=my_system(cmd);
	if(ret !=0 ) {
		config.error_msg=OTA_ERR_DOWN_ERR;
		return -1;
	}
	ret = pthread_create(&dowm_id, NULL, dowm_func, NULL);
	if(ret != 0) {
		log_err("download thread create error! ret = %d",ret);
		 config.error_msg=OTA_ERR_DOWN_ERR;
		 return -1;
	 }
	return 0;
}

 int read_ota_config_file(void)
 {
	 int ret;

	memset(&config, 0, sizeof(kernel_ota_config_t));
	pthread_rwlock_init(&lock, NULL);
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_err("add lock fail, ret = %d", ret);
		return ret;
	}
	 ret = read_config_file(kernel_ota_config_profile_map, CONFIG_KERNEL_OTA_PATH);
	 		if(!ret) {
	 			open_config_flag=1;
	 			log_err("--------read_ota_config_file ----------  config.status=%d \n",config.status);
		 		ret = pthread_rwlock_unlock(&lock);
		 		if (ret)
		 			log_err("add unlock fail, ret = %d", ret);
		 		return ret;
	 		}
	 		else
	 			return -1;


 }




