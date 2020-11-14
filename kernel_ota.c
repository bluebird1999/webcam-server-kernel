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
#include "kernel_interface.h"
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
    {"ota_status",      &(config.status),     cfg_s32, 6,0, 0,7,	},
    {"ota_progress",     &(config.progress), cfg_s32, 0,0, 0,100,	},
    {"error_msg",      &(config.error_msg), cfg_s32, 0,0, 0,8,	},
    {NULL,},
};

//function
static int my_system(const char * cmd);
static int ota_config_save(void);
static int config_ota_set(void *arg);
static void *get_progress_thread(void *arg);
static void *dowm_func(void *arg);
static void *ota_install_thread(void *arg);
static int install_failed_report(void);
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
static int install_failed_report(void)
{
	int ret = 0;
    /********message body********/
	message_t message;
	msg_init(&message);
	message.message = MSG_KERNEL_OTA_REPORT_ACK;
	message.sender = message.receiver = SERVER_MICLOUD;
	message.arg_in.cat = config.status;
	message.arg_in.dog = config.progress;
	message.arg_in.duck = config.error_msg;
	server_miio_message(&message);
	/***************************/
	//log_info("-------------------when install failed send  report-----------------");
	return ret;

}

static int ota_config_save(void)
{
	int ret ,ret1=0;
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	//message_t msg;
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_err("add lock fail, ret = %d\n", ret);
		return ret;
	}
	if( open_config_flag == 1 ) {
		log_err("int ------------ota_config_save------------ \n");
		memset(fname,0,sizeof(fname));
		sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_KERNEL_OTA_PATH);
		ret = write_config_file(kernel_ota_config_profile_map, fname);
		if(!ret)
			open_config_flag=0;
		ret1|=ret;
	}

	ret = pthread_rwlock_unlock(&lock);
	if (ret){
		log_err("add unlock fail, ret = %d\n", ret);
		return ret;
	}

	return ret1;
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
	char cmd[32]={0};
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	log_info("into func set_reboot \n");
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.qcy_path, RESTORE_SH);
    sprintf(cmd, "%s  3  &",fname);
    ret=system(cmd);
    if(ret == 0)  return 0;
    else  return -1;

}

static void *ota_install_thread(void *arg)
{
	FILE *fp=NULL;
	int ret,j=0;
	char  filemd5[64]={0};
	char cmd[64]={0};
	char buf[64]={0};
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	log_info("ota_install_thread----------------\n");
	//把该线程设置为分离属性
	pthread_detach(pthread_self());
	while(1)
		{
			usleep (500000);
			if(config.status == OTA_STATE_INSTALLED)
				return 0;
			if(config.status == OTA_STATE_FAILED){
				j++;
				if(j==60)
			    	goto exit;
					}
			if(config.status == OTA_STATE_DOWNLOADING){
				j++;
				if(j==60)
			    	goto exit;
				continue;}
			if(config.status == OTA_STATE_INSTALLING){
				if(j==60)
			    	goto exit;
				continue;}
			if(config.status == OTA_STATE_WAIT_INSTALL)
				break;
		}
		//check ota_file md5
			ret=Compute_file_md5(OTA_DOWNLOAD_APPLICATION_NAME, filemd5);
			if(ret) {
				log_info("----------get OTA_DOWNLOAD_APPLICATION_NAME  md5 faile---------------------\n");
				config.status=OTA_STATE_FAILED;
				config.error_msg = OTA_ERR_INSTALL_ERR;
		    	goto exit;
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
	    	goto exit;
			}
		memset(fname,0,sizeof(fname));
		sprintf(fname,"%s%s",_config_.qcy_path, OTA_UPDARE_SH_PATH);
		sprintf(cmd, "%s &", fname);
			if ((fp = popen(cmd, "r") ) == NULL)
					{
						perror("popen");
						config.status=OTA_STATE_FAILED;
						config.error_msg = OTA_ERR_INSTALL_ERR;
				    	goto exit;
					}
				else
				 {
					while(fgets(buf, sizeof(buf), fp))
					{
						log_info("install_func buf=%s", buf);
						if(strstr(buf,"install failed")!=0){

							config.status=OTA_STATE_FAILED;
							config.error_msg = OTA_ERR_INSTALL_ERR;
									  }
						else if(strstr(buf,"install success")!=0){

							config.status=OTA_STATE_INSTALLED;
							config.error_msg = OTA_ERR_NONE;
							  log_info("---install  ok-----------\n");
							  break;
						 }

					}
					pclose(fp);
				 }
		if(config.status==OTA_STATE_FAILED)
		{
	    	goto exit;
		}
		usleep(500000);
		config.status=OTA_STATE_IDLE;
		log_info("------ota_install_fun-----end---\n");

exit:
		log_qcy(DEBUG_SERIOUS, "-----------thread exit: ota_install_thread-----------");
		pthread_exit(0);

}

int ota_install_fun(char *url,unsigned int ulr_len,char *ota_md5,unsigned int ota_md5_len)
{
	pthread_t install_tid;
	int ret;
	memcpy(config.url,url,ulr_len);
	memcpy(config.md5,ota_md5,ota_md5_len);
	creat_get_progress_thread();
	//if(config.status == OTA_STATE_DOWNLOADING)
	//	return 0;

	ret=pthread_create(&install_tid,NULL,ota_install_thread,NULL);
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
	//把该线程设置为分离属性
	pthread_detach(pthread_self());
#if 1
	while(i<2){
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
					break;
							  }
				if(strstr(buf,"HTTP/1.1 403")!=0){
					//url没加双引号
					res = OTA_ERR_DOWN_ERR;
					break;
							  }
				if(strstr(buf,"server returned error: HTTP/1.1 416 Requested Range Not Satisfiable")!=0){
					//文件已经下载
					res = OTA_ERR_NONE;
					break;
							  }

				else if(strstr(buf,"bad address")!=0){
					  //目标地址错误
					  res =  OTA_ERR_DOWN_ERR;
						break;
				  }
				else if(strstr(buf,"can't connect to remote host")!=0){
					  //网络不可用
					  res = OTA_ERR_DOWN_ERR;
				  }
				else if(strstr(buf,"100%")!=0){
					  //无错误
					  res = OTA_ERR_NONE;
					  config.status = OTA_STATE_DOWNLOADED;
					  log_info("---100%  ok-----------\n");
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
	int ret;
	log_info("into get_progress_thread\n");
	config.progress=0;
	//把该线程设置为分离属性
	pthread_detach(pthread_self());
	while(1)
	{
		if(config.status == OTA_STATE_FAILED)
		{
			install_failed_report();
			ret=ota_config_save();
			if(ret){
				log_info("--ota_config_save--- failed\n");
			}
			break;
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

					if(config.progress==100)
					{
						//save config.status
						ret=ota_config_save();
						if(ret){
							log_info("--ota_config_save--- failed\n");
							config.status=OTA_STATE_FAILED;
							config.error_msg = OTA_ERR_INSTALL_ERR;
							goto exit;
						}
						log_info("------------ota_config_save--- ok----------\n");
						sleep(2);
						//send reboot cmoman
						ret=set_reboot();
						if(ret) {log_info("ota try reboot faile\n"); }
						break;
					}


				}

		usleep(80000);
	}

exit:
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

int ota_dowmload_date(char *url,unsigned int ulr_len)
{
	int ret;
	char cmd[512]={0};
	pthread_t dowm_id;
	config.status=OTA_STATE_DOWNLOADING;
	config.progress=0;
	config.error_msg=OTA_ERR_NONE;
	sprintf(cmd, "wget  -c -t 3 -T 5  -O %s   \"%s\"  2>%s 1>&2   &",OTA_DOWNLOAD_APPLICATION_NAME,url,OTA_WGET_LOG);
	ret=my_system(cmd);
	if(ret !=0 ) {
		config.error_msg=OTA_ERR_DOWN_ERR;
		log_info("--ota_dowmload_date---my_system error  !---ret=%d\n",ret);
		return -1;
	}

	ret = pthread_create(&dowm_id, NULL, dowm_func, NULL);
	if(ret != 0) {
		log_err("download thread create error! ret = %d",ret);
		 config.error_msg=OTA_ERR_DOWN_ERR;
		 return -1;
	 }
	log_info("-----ota_dowmload_date end !---ret=%d\n",ret);
	return 0;
}

 int read_ota_config_file(void)
 {
	 int ret;
	char fname[MAX_SYSTEM_STRING_SIZE*2];

	memset(&config, 0, sizeof(kernel_ota_config_t));
	pthread_rwlock_init(&lock, NULL);
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_err("add lock fail, ret = %d", ret);
		return ret;
	}
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_KERNEL_OTA_PATH);
	 ret = read_config_file(kernel_ota_config_profile_map, fname);
	 		if(!ret) {
	 			open_config_flag=1;
		 		ret = pthread_rwlock_unlock(&lock);
		 		if (ret)
		 			log_err("add unlock fail, ret = %d", ret);
		 		return ret;
	 		}
	 		else
	 			return -1;


 }


