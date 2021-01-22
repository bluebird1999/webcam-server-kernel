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
#include<unistd.h>
#include<error.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include<fcntl.h>
//program header
#include "../../manager/manager_interface.h"
#include "../../tools/tools_interface.h"
#include "../../server/miio/miio_interface.h"
//#include "../../server/speaker/speaker_interface.h"
#include "../../server/audio/audio_interface.h"
#include "../../server/device/device_interface.h"
//server heade
#include "kernel.h"
#include "kernel_interface.h"
#include "../../tools/config/rwio.h"
#include "../../tools/log.h"
#include "kernel_ota.h"
#include "MD5.h"
#include "miio_sign_verify.h"

#define ROOT_CERT_PATH		"/mnt/data/MijiaRootCert.pem"

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
static int install_report(void);
static void ctrl_led_install(int type);
//static int ota_set_status(int model);
static int miio_upgrade_get_file(void );
static int miio_upgrade_check_sign(char *filemd5,int original_length, unsigned char *cert_buf, int certfilelen);
static int ota_report_manager(int arg);
/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * helper
 */

static int ota_report_manager(int arg)
{
	int ret = 0;
	static int send_flag=0;
    /********message body********/
	message_t message;
	msg_init(&message);
	if(arg){
	message.message = MSG_MANAGER_OTAUPDATE_SERVER_EXIT;
	message.sender = message.receiver = SERVER_KERNEL;
	manager_common_send_message(SERVER_MANAGER,    &message);
	send_flag=1;
	}
	else
	{
		if(send_flag==1){
		message.message = MSG_MANAGER_OTAUPDATE_SERVER_WAKEUP;
		message.sender = message.receiver = SERVER_KERNEL;
		manager_common_send_message(SERVER_MANAGER,    &message);
		send_flag=0;
		}
	}
	/***************************/
	return ret;

}

static int miio_upgrade_check_sign(char *filemd5,int original_length, unsigned char *cert_buf, int certfilelen)
{
	ota_ctx_t my_ctx = {0};
    //my_ctx.root_cert = (unsigned char *)miio_mijia_root_cert;
    //my_ctx.cert_len = sizeof(miio_mijia_root_cert);
	strncpy(my_ctx.file_md5, filemd5, strlen(filemd5));
    my_ctx.signed_file = 1;
    my_ctx.original_file_len = original_length;
	//1.1 通过url下载固件，打开DFU文件
    int index = 0, deep = 1024, count = 0;
    size_t data_len = 0;
    unsigned char sn[64] = {0};
    size_t sn_len = 64;
    int fd=0 ;

	if(cert_buf == NULL || certfilelen <= 0)
	{
		log_qcy(DEBUG_SERIOUS,"cert_buf param error!\n");
		return -1;
	}

    my_ctx.root_cert = (unsigned char *)cert_buf;
    my_ctx.cert_len = certfilelen;

    fd = open(OTA_DOWNLOAD_APPLICATION_NAME, O_RDWR);
    if (fd < 0) {
    	log_qcy(DEBUG_SERIOUS,"open %s file failed!\n", OTA_DOWNLOAD_APPLICATION_NAME);
        return -1;
    }
    //获取文件的属性
    struct stat sb;
    if ((fstat(fd, &sb)) == -1 ) {
        perror("fstat");
        close(fd);
        return -1;
    }
	count = sb.st_size / deep;
    lseek(fd, 0, SEEK_SET);
    //printf("dfu len: %ld\n", sb.st_size);
    unsigned char *buffer;
	if ((buffer = (unsigned char *)malloc(sb.st_size)) == NULL) {
		log_qcy(DEBUG_SERIOUS,"malloc sb.st_size  failed  \n");
        close(fd);
        return -1;
    } else {
        if (sb.st_size != read(fd, buffer, sb.st_size)) {
        	log_qcy(DEBUG_SERIOUS,"read  ota failed  failed  \n");
            close(fd);
            return -1;
        }
    }

	log_qcy(DEBUG_INFO,"fileLen: %d,original_length:%d,md5:%s,my_ctx.cert_len:%d\n", sb.st_size,original_length,filemd5,my_ctx.cert_len);

    //printf("%s\n", my_ctx.root_cert);
	//2.0初始化
    miio_sign_verify_init(&my_ctx);
	//2.1连续分段输入固件数据，每一段数据长度
//2.1连续分段输入固件数据，每一段数据长度
    for (index = 0; index < count; index ++) {
        miio_sign_verify_update(buffer + index * deep, deep);
    }
    if (index * deep < sb.st_size) {
        miio_sign_verify_update(buffer + index * deep, sb.st_size - index * deep);
    }
	//2.2完成验签，判断是否验签成功
    if (0 != miio_sign_verify_finish(&data_len, sn, &sn_len)) {
        free(cert_buf);
        log_qcy(DEBUG_SERIOUS,"miio_sign_verify_finish Failed! ret \n");
		return -1;
    }
	else {
        int i = 0;
        log_qcy(DEBUG_INFO,"---------miio_sign_verify_finish ok, data_len=%d\n", data_len);
        printf("sn:");
        for (i = 0; i < sn_len; i ++)
            printf("%x", sn[i]);
        printf("\n");
    }
    free(buffer);

    lseek(fd, 0, SEEK_SET);
    int size_f=data_len;
    int blok_t = 10*1024;
    int result=0;
    unsigned char *src=NULL;
	if ((src = (unsigned char *)malloc(blok_t)) == NULL) {
		log_qcy(DEBUG_SERIOUS,"malloc data_len ");
        close(fd);
        return -1;
    }
    int fd2 = open(OTA_UPDAT_NAME, O_SYNC|O_CREAT|O_RDWR);
    if (fd < 0) {
    	log_qcy(DEBUG_SERIOUS,"open %s file failed!\n", OTA_UPDAT_NAME);
        close(fd);
        close(fd2);
        free(src);
        return -1;
    }

    while(size_f)
    {
    	if(size_f < blok_t)  blok_t = size_f;

        result = read(fd,src,blok_t);
        if (blok_t != result)
        {
            if (result < 0)
            {
            	log_qcy(DEBUG_SERIOUS,"While reading data from %s\n",OTA_DOWNLOAD_APPLICATION_NAME);
                return -1;
            }
            log_qcy(DEBUG_SERIOUS,"Short read count returned while reading from %s\n",OTA_DOWNLOAD_APPLICATION_NAME);
            return -1;
        }


    	result=write(fd2,src,blok_t);
    	if (blok_t != result)
		{
			if (result < 0)
			{
				log_qcy(DEBUG_SERIOUS,"While writing data from %s\n",OTA_UPDAT_NAME);
				return -1;
			}
			log_qcy(DEBUG_SERIOUS,"Short write count returned while writing from %s\n",OTA_UPDAT_NAME);
			return -1;
		}
    	size_f= size_f - blok_t;
    }

    remove(OTA_DOWNLOAD_APPLICATION_NAME);
	free(cert_buf);
	free(src);
	close(fd);
	close(fd2);
    return data_len;
}


static int miio_upgrade_get_file(void)
{
    int ret = 0;
    int fileTotalLen = 0;
	FILE *fp;
	unsigned char *cert_buf = NULL;
    int certfilelen = 0;

	fp = fopen(ROOT_CERT_PATH, "rb");
	if(fp == NULL)
	{
		log_qcy(DEBUG_SERIOUS,"open %s failed!\n", ROOT_CERT_PATH);
		return -1;
	}

	fseek(fp, 0, SEEK_END);
	certfilelen = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if(certfilelen <= 0)
	{
		log_qcy(DEBUG_SERIOUS,"%s len error!\n", ROOT_CERT_PATH);
		fclose(fp);
		return -1;
	}

	cert_buf = malloc(certfilelen);
	if(cert_buf == NULL)
	{
		log_qcy(DEBUG_SERIOUS,"cert_buf malloc failed!\n");
		fclose(fp);
		return -1;
	}

	memset(cert_buf,0,certfilelen);
	ret = fread(cert_buf, 1, certfilelen, fp);
	if(ret != certfilelen)
	{
		log_qcy(DEBUG_SERIOUS,"fread %s error!\n",ROOT_CERT_PATH);
		fclose(fp);
		free(cert_buf);
		return -1;
	}
	fclose(fp);
	config.original_data_len  = miio_upgrade_check_sign(config.md5,config.original_length, cert_buf, certfilelen);
	//log_qcy(DEBUG_INFO,"int ---------config.original_data_len =%d---------- \n",config.original_data_len);
	if(config.original_data_len<1)  return -1;
    return 0;
}

static void ctrl_led_install( int type)
{
	// 0: installing  1: installed
	device_iot_config_t  temp_t;
	memset(&temp_t,0,sizeof(device_iot_config_t));
	log_qcy(DEBUG_INFO,"kernelctrl_led_install, type = %d\n",type);
	message_t message;
	msg_init(&message);
	message.sender = message.receiver = SERVER_KERNEL;
	message.message =MSG_DEVICE_CTRL_DIRECT;
	message.arg_in.cat = DEVICE_CTRL_LED;
	if(type)
	{
	temp_t.led1_onoff=0;
	temp_t.led2_onoff=2;
	}
	else {
		temp_t.led1_onoff=0;
		temp_t.led2_onoff=1;
	}
	message.arg=(void *)&temp_t;
	message.arg_size = sizeof(device_iot_config_t);
	manager_common_send_message(SERVER_DEVICE,    &message);
}
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
static int install_report(void)
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
		memset(fname,0,sizeof(fname));
		sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_KERNEL_OTA_PATH);
		ret = write_config_file(kernel_ota_config_profile_map, fname);
		if(!ret)
			open_config_flag=0;
		log_qcy(DEBUG_SERIOUS,"int ------------ota_config_save------------ \n");
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
	snprintf(fname,MAX_SYSTEM_STRING_SIZE*2,"%s%s",_config_.qcy_path, RESTORE_SH);
    snprintf(cmd,64, "%s  %s  &",fname,REBOOT);
    ret=system(cmd);
    if(ret == 0)  return 0;
    else  return -1;

}

static void *ota_install_thread(void *arg)
{
	//FILE *fp=NULL;
	int ret,j=0;
	char  filemd5[64]={0};
	log_qcy(DEBUG_SERIOUS,"ota_install_thread----------------\n");
	//把该线程设置为分离属性
	pthread_detach(pthread_self());
	while(1)
		{
			sleep (1);
			if(config.status == OTA_STATE_INSTALLED)
				goto exit;
			if(config.status == OTA_STATE_FAILED){
			    	goto exit;
					}
			if(config.status == OTA_STATE_DOWNLOADING){
				j++;
				if(j==60)
				{
					config.status = OTA_STATE_INSTALLED;
			    	goto exit;}
				continue;}
			if(config.status == OTA_STATE_INSTALLING)
			    	goto exit;
			if(config.status == OTA_STATE_WAIT_INSTALL)
				break;
		}
	ctrl_led_install(1);
	log_qcy(DEBUG_SERIOUS, "---------config.status=%d-----start----\n",config.status);
	config.status = OTA_STATE_INSTALLING;
	play_voice(SERVER_KERNEL, SPEAKER_CTL_INSTALLING);
	install_report();
	sleep(3);
	miio_upgrade_get_file();
//************
//	config.status=OTA_STATE_FAILED;
//	install_report();
//	goto exit;
	ret=ota_process_main(OTA_UPDAT_NAME);
	 if(ret)
	 {
			config.status=OTA_STATE_FAILED;
			log_qcy(DEBUG_INFO,"---ota_process_main   --install failed---\n");
			goto exit;
	 }
	 	remove(OTA_UPDAT_NAME);
		config.status=OTA_STATE_INSTALLED;
		config.error_msg = OTA_ERR_NONE;
		//install_report();
		play_voice(SERVER_KERNEL, SPEAKER_CTL_INSTALLEND);
		ctrl_led_install(0);
exit:
log_qcy(DEBUG_SERIOUS, "----thread exit: ota_install_thread----config.status=%d--config.error_msg = %d---\n",config.status,config.error_msg);
		pthread_exit(0);

}

int ota_install_fun(char *url,unsigned int ulr_len,char *ota_md5,unsigned int ota_md5_len,int original_length)
{
	pthread_t install_tid;
	int ret;
	memcpy(config.url,url,ulr_len);
	memcpy(config.md5,ota_md5,ota_md5_len);
	config.original_length=original_length;
	ret=pthread_create(&install_tid,NULL,ota_install_thread,NULL);
	if(ret != 0) {
		log_qcy(DEBUG_INFO,"installl thread create error! ret = %d",ret);
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
	int res=0;
	char buf[128]={0};
	log_info("into dowm_func thread\n");
	//把该线程设置为分离属性
	pthread_detach(pthread_self());
#if 1
	while(i<5){
	sprintf(cmd, "tail -n 2 %s", OTA_WGET_LOG);
	if ((fp = popen(cmd, "r") ) == NULL)
			{
		log_qcy(DEBUG_INFO," popen failed\n");
				res=OTA_ERR_DOWN_ERR;
				break;
			}
		else
		 {
			while(fgets(buf, sizeof(buf), fp))
			{
				log_qcy(DEBUG_INFO,"dowm_func buf=%s", buf);
				if(strstr(buf,"server returned error: HTTP/1.1 404 Not Found")!=0){
					//文件名字错误
					res = OTA_ERR_DOWN_ERR;
					log_qcy(DEBUG_SERIOUS, "-------server returned error: HTTP/1.1 404 Not Found------");
					break;
							  }
				if(strstr(buf,"HTTP/1.1 403")!=0){
					//url没加双引号
					res = OTA_ERR_DOWN_ERR;
					log_qcy(DEBUG_SERIOUS, "-------HTTP/1.1 403----");
					break;
							  }
				if(strstr(buf,"server returned error: HTTP/1.1 416 Requested Range Not Satisfiable")!=0){
					//文件已经下载
					config.status = OTA_STATE_DOWNLOADED;
					log_qcy(DEBUG_SERIOUS, "----server returned error: HTTP/1.1 416 Requested Range Not Satisfiable--");
					res = OTA_ERR_NONE;
					break;
							  }

				else if(strstr(buf,"bad address")!=0){
					  //目标地址错误
					  res =  OTA_ERR_DOWN_ERR;
						log_qcy(DEBUG_SERIOUS, "------bad address---");
						break;
				  }
				else if(strstr(buf,"can't connect to remote host")!=0){
					  //网络不可用
					  res = OTA_ERR_DOWN_ERR;
					  log_qcy(DEBUG_SERIOUS, "----can't connect to remote host--");
				  }
				else if(strstr(buf,"100%")!=0){
					  //无错误
					  res = OTA_ERR_NONE;
					  config.status = OTA_STATE_DOWNLOADED;
					  log_qcy(DEBUG_INFO,"--down-100%  ok-----------\n");
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
	log_qcy(DEBUG_INFO,"-i++   ---dowm_func----\n");
	sleep(10);
	}
#endif
			if(res != OTA_ERR_NONE)
			{
				config.status = OTA_STATE_FAILED;
			}
			else {
				config.status = OTA_STATE_WAIT_INSTALL;
				//ota_report_manager(1);
				sleep(2);
			}
			config.error_msg = res;
			log_qcy(DEBUG_SERIOUS, "-----------thread exit: dowm_func_thread--end---------");
			pthread_exit(0);
}

static void *get_progress_thread(void *arg)
{
	int ret;
	log_qcy(DEBUG_INFO,"into get_progress_thread\n");
	config.progress=0;
	//把该线程设置为分离属性
	pthread_detach(pthread_self());
	while(1)
	{
		if(config.status == OTA_STATE_FAILED)
		{
			install_report();
			ret=ota_config_save();
			play_voice(SERVER_KERNEL, SPEAKER_CTL_INSTALLFAILED);
			if(ret){
				log_info("--ota_config_save--- failed\n");
			}
			remove(OTA_DOWNLOAD_APPLICATION_NAME);
			remove(OTA_UPDAT_NAME);
			log_qcy(DEBUG_SERIOUS,"----get_progress_thread------config.status == OTA_STATE_FAILED---\n");
			//sleep(5);
			//ota_report_manager(0);
			break;
		}
		if(config.status == OTA_STATE_DOWNLOADING)
		{
			if(config.progress < 80)
			config.progress=config.progress+1;
		}
		if(config.status == OTA_STATE_DOWNLOADED)
				{
					if(config.progress < 95)
					config.progress=config.progress+1;
				}
		if(config.status == OTA_STATE_WAIT_INSTALL)
				{
					if(config.progress < 100)
					config.progress=config.progress+1;
				}
		if(config.status == OTA_STATE_INSTALLED)
				{
						if(config.progress < 100)
						config.progress=config.progress+1;
						if(config.progress==100 )
							{
								config.status = OTA_STATE_IDLE;
								//save config.status
								ret=ota_config_save();
								if(ret){
									log_info("--ota_config_save--- failed\n");
									goto exit;
								}
								log_qcy(DEBUG_SERIOUS,"------------ota_config_save--- ok----------\n");
								sleep(5);
								//send reboot cmoman
								ret=set_reboot();
								if(ret) {log_qcy(DEBUG_SERIOUS,"ota try reboot faile\n"); }
								break;
							}
				}
		usleep(40000);
	}

exit:
	log_qcy(DEBUG_SERIOUS, "-----------thread exit: get_progress_thread-----------");
	 pthread_exit(0);

}

int kernel_ota_get_status()
{
	int st;
	int ret;
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_qcy(DEBUG_INFO,"add lock fail, ret = %d", ret);
		return ret;
	}
	st = config.status;
	ret = pthread_rwlock_unlock(&lock);
	if (ret)
		log_qcy(DEBUG_INFO,"add unlock fail, ret = %d", ret);
	return st;
}

int kernel_ota_get_error_msg()
{
	int st;
	int ret;
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_qcy(DEBUG_INFO,"add lock fail, ret = %d", ret);
		return ret;
	}
	st = config.error_msg;
	ret = pthread_rwlock_unlock(&lock);
	if (ret)
		log_qcy(DEBUG_INFO,"add unlock fail, ret = %d", ret);
	return st;
}
int kernel_ota_get_progress()
{
	int st;
	int ret;
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_qcy(DEBUG_INFO,"add lock fail, ret = %d", ret);
		return ret;
	}
	st = config.progress;
	ret = pthread_rwlock_unlock(&lock);
	if (ret)
		log_qcy(DEBUG_INFO,"add unlock fail, ret = %d", ret);
	return st;
}

int ota_dowmload_date(char *url,unsigned int ulr_len)
{
	int ret,ret1=0;
	char cmd[512]={0};
	pthread_t dowm_id;

	if(config.status==OTA_STATE_DOWNLOADING || config.status==OTA_STATE_WAIT_INSTALL )
		return -1;

	config.status=OTA_STATE_DOWNLOADING;
	config.progress=0;
	config.error_msg=OTA_ERR_NONE;
	creat_get_progress_thread();
	install_report();
	sleep(4);
	sprintf(cmd, "wget  -c -t 3 -T 5  -O %s   \"%s\"  2>%s 1>&2   &",OTA_DOWNLOAD_APPLICATION_NAME,url,OTA_WGET_LOG);
	ret=my_system(cmd);
	if(ret !=0 ) {
		log_qcy(DEBUG_SERIOUS, "--ota_dowmload_date---my_system error  !---ret=%d\n",ret);
		return -1;
	}
	ret1|=ret;
	ret = pthread_create(&dowm_id, NULL, dowm_func, NULL);
	if(ret != 0) {
		log_err("download thread create error! ret = %d",ret);
		 return -1;
	 }
	log_qcy(DEBUG_INFO, "-----ota_dowmload_date end !---ret=%d\n",ret);
	ret1|=ret;
	if(ret1){
	 config.error_msg=OTA_ERR_DOWN_ERR; }
	return ret1;
}

 int read_ota_config_file(void)
 {
	 int ret;
	char fname[MAX_SYSTEM_STRING_SIZE*2];

	memset(&config, 0, sizeof(kernel_ota_config_t));
	pthread_rwlock_init(&lock, NULL);
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_qcy(DEBUG_INFO,"add lock fail, ret = %d", ret);
		return ret;
	}
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_KERNEL_OTA_PATH);
	 ret = read_config_file(kernel_ota_config_profile_map, fname);
	 		if(!ret) {
	 			open_config_flag=1;
		 		ret = pthread_rwlock_unlock(&lock);
		 		if (ret)
		 			log_qcy(DEBUG_INFO,"add unlock fail, ret = %d", ret);
		 		return ret;
	 		}
	 		else
	 			return -1;


 }

