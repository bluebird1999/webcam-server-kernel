/*
 * kernel.c
 *
 *  Created on: 2020年10月19日
 *      Author: hjk
 */


/*
 * header
 */
//system header
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
//program header
#include "../../tools/tools_interface.h"
#include "../../manager/manager_interface.h"
#include "../../server/miio/miio_interface.h"
#include "../../server/miss/miss_interface.h"
#include "../../server/audio/audio_interface.h"
//#include "../../server/speaker/speaker_interface.h"
#include "../../server/player/player_interface.h"
#include "../../server/recorder/recorder_interface.h"
//server header
#include "kernel.h"
#include "kernel_interface.h"
#include "../../tools/config/rwio.h"
#include "../../tools/log.h"
#include "kernel_ota.h"
/*
 * static
 */
//variable
static server_info_t 		info;
static kernel_ota_status_info_t		ota_status;
static message_buffer_t		message;
static int hang_up_flag=0;
static pthread_rwlock_t		k_lock = PTHREAD_RWLOCK_INITIALIZER;
static pthread_mutex_t		k_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t		k_cond = PTHREAD_COND_INITIALIZER;
static int k_hang_up_flag=0;
//function
//common
static int server_message_proc(void);
static void task_default(void);
//static void task_error(void);
//static int server_release(void);
static int server_get_status(int type);
static int server_set_status(int type, int st);
static void server_kernel_thread_termination(int arg);
static int send_message(int receiver, message_t *msg);
static void *server_func(void *arg);
//specific
static int config_kernel_read(void);
static int send_iot_ack(message_t *org_msg, message_t *msg, int id, int receiver, int result, void *arg, int size);
static int set_timezone(char *arg);
static int set_reboot(void);
static int set_restore(void);
static int my_system(const char * cmd);

/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * helper
 */
//

int play_voice(int server_type, int type)
{
	int ret;
	log_err("kernel play_voice, type = %d\n", type);
	message_t message;
	msg_init(&message);
	message.sender = message.receiver = server_type;
	message.message = MSG_AUDIO_SPEAKER_CTL_PLAY;
	message.arg_in.cat = type;
	ret=manager_common_send_message(SERVER_AUDIO,  &message  );
	return ret;
}

static int send_iot_ack(message_t *org_msg, message_t *msg, int id, int receiver, int result, void *arg, int size)
{
	int ret = 0;
    /********message body********/
	msg_init(msg);
	memcpy(&(msg->arg_pass), &(org_msg->arg_pass),sizeof(message_arg_t));
	msg->message = id | 0x1000;
	msg->sender = msg->receiver = SERVER_KERNEL;
	msg->result = result;
	msg->arg = arg;
	msg->arg_size = size;
	ret = send_message(receiver, msg);
	/***************************/
	return ret;
}

static int send_ota_ack(message_t *org_msg, message_t *msg, int id, int receiver, int result, int status, int progress,int error_msg)
{
	int ret = 0;
    /********message body********/
	msg_init(msg);
	memcpy(&(msg->arg_pass), &(org_msg->arg_pass),sizeof(message_arg_t));
	msg->message = id | 0x1000;
	msg->sender = msg->receiver = SERVER_KERNEL;
	msg->result = result;
	msg->arg_in.cat = status;
	msg->arg_in.dog = progress;
	msg->arg_in.duck = error_msg;
	ret = send_message(receiver, msg);
	/***************************/
	return ret;
}

static int send_message(int receiver, message_t *msg)
{
	int st;
	switch(receiver) {
	case SERVER_DEVICE:
		break;
	case SERVER_KERNEL:
		break;
	case SERVER_REALTEK:
		break;
	case SERVER_MIIO:
		st = server_miio_message(msg);
		break;
	case SERVER_MISS:
		st = server_miss_message(msg);
		break;
	case SERVER_MICLOUD:
		break;
	case SERVER_AUDIO:
		st = server_audio_message(msg);
		break;
	case SERVER_RECORDER:
		break;
	case SERVER_PLAYER:
		break;
	case SERVER_MANAGER:
		st = manager_message(msg);
		break;
	}
	return st;
}

static int server_get_status(int type)
{
	int st;
	int ret;
	ret = pthread_rwlock_wrlock(&info.lock);
	if(ret)	{
		log_err("add lock fail, ret = %d", ret);
		return ret;
	}
	if(type == STATUS_TYPE_STATUS)
		st = info.status;
	else if(type== STATUS_TYPE_EXIT)
		st = info.exit;
	/*else if(type==STATUS_TYPE_CONFIG)
		st = kernel_wifi_config.status;*/
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret)
		log_err("add unlock fail, ret = %d", ret);
	return st;
}
static int heart_beat_proc(void)
{
	int ret = 0;
	message_t msg;
	long long int tick = 0;
	tick = time_get_now_stamp();
	if( (tick - info.tick) > 10 ) {
		info.tick = tick;
	    /********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_HEARTBEAT;
		msg.sender = msg.receiver = SERVER_KERNEL;
		msg.arg_in.cat = info.status;
		msg.arg_in.dog = info.thread_start;
		msg.arg_in.duck = info.thread_exit;
		ret = manager_message(&msg);
		/***************************/
	}
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
				log_info("%s\n", buf);
			}
			//fgets(buf, sizeof(buf), fp);
			//log_info("%s", buf);
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

static int set_timezone(char *arg)
{
	if(arg==NULL) return -1;
    int ret;
    char linedata[50]={0};
    char *tmp_str=NULL;
    char tmp_str2[8]={0};
    char tmp_buf[55]={0};
	char cmd[128]={0};
	int num_i=0;
    FILE *fp=fopen(TIMEZONE_INFO,"r");
    if(fp ==NULL)  {
    	log_qcy(DEBUG_INFO, "open   TIMEZONE_INFO failed \n"  );
    	return -1;
    }
    while (fgets(linedata,sizeof(linedata)/sizeof(char)-1,fp))
    {
        if (strstr(linedata,arg))
        {
        	log_qcy(DEBUG_INFO, "linedata   = %s\n",   linedata  );
        	tmp_str=strstr(linedata,"UTC");
        	if(tmp_str ==NULL)  return -1;
        	int len=strlen(tmp_str)-2; //  "/r-/n"
        //	log_err("--------------len =%d\n", len);
        	strncpy(tmp_buf,tmp_str,len);
        	memcpy(tmp_str2,tmp_buf+3,6);
        //log_err("--------------tmp_str2- = %s", tmp_str2);
        	if (  (tmp_str=strstr(tmp_str2,"+") ) )
        	{
        		if(len>7){
        			//*tmp_str='-';
					char *p=NULL;
					char buf1[5]={0};
					float num_float=0;

					//sscanf(tmp_str2,"%s:%s",buf1,num_tmp);
					p=strtok(tmp_str+1, ":");
				//	printf("----strtok1 p1 = %s\n",p);
					memcpy(buf1,p,strlen(p));
					p=strtok(NULL, ":");
					num_float =  atoi(p)/60.0;
					int num_tt=(int)(num_float*10);  //暂时不处理有两位小数的时区

				//	printf("----num_float = = %.2lf\n",num_float);
					memset(tmp_str2,0,sizeof(tmp_str2));
					snprintf(tmp_str2,sizeof(tmp_str2),"%s%d",buf1,num_tt);
					num_i=atoi(tmp_str2);
					num_i=-num_i;
				//	printf("----num_i  = %d\n",num_i);
        		}
        		else{
//					if( (*(tmp_str+1) !=0) )
//					*tmp_str='-';
        			num_i=atoi(tmp_str+1);
        			num_i=-(num_i*10);
        			printf("----num_i  = %d\n",num_i);
        		}
        	}
        	else if(  (tmp_str=strstr(tmp_str2,"-") ) )
				{
        				*tmp_str='+';
        				if(len>7){
        					char *p=NULL;
        					char buf1[5]={0};
        					float num_float=0;
        					//sscanf(tmp_str2,"%s:%s",buf1,num_tmp);
        					p=strtok(tmp_str+1, ":");
        					printf("----strtok1 p1 = %s\n",p);
        					memcpy(buf1,p,strlen(p));
        					p=strtok(NULL, ":");
        					num_float =  atoi(p)/60.0;
        					int num_tt=(int)(num_float*10);
        					printf("----num_float = = %.2lf\n",num_float);
        					memset(tmp_str2,0,sizeof(tmp_str2));
        					snprintf(tmp_str2,sizeof(tmp_str2),"%s%d",buf1,num_tt);
        					num_i=atoi(tmp_str2);
        					//printf("----num_i  = %d\n",num_i);
							}
        				else
        				{
                			num_i=atoi(tmp_str+1);
                			num_i=num_i*10;
                		//	printf("----num_i  = %d\n",num_i);
        				}


				}
        	//log_err("------change--------tmp_str2- = %s", tmp_str2);
        	log_err("-------num_i  = %d-------tmp_buf- = %s",num_i, tmp_buf);
        	break;
        }
    }
    fclose(fp);
    snprintf(cmd,64, "echo  %s  >  %s ",tmp_buf,YOUR_LINK_TIMEZONE_FILE);
    ret = my_system(cmd);

    if(ret==0){

		 log_err( "my_system(cmd) = %s  successful!787 \n", cmd);
		/********message body********/
		message_t message;
		msg_init(&message);
		message.message = MSG_KERNEL_TIMEZONE_CHANGE;
		message.sender = message.receiver = SERVER_KERNEL;
		message.arg_in.dog = num_i;
		//message.arg_size = sizeof(int);
		server_player_message(&message);
		server_recorder_message(&message);

		msg_init(&message);
		message.message = MSG_MANAGER_PROPERTY_SET;
		message.sender = message.receiver = SERVER_KERNEL;
		message.arg_in.cat = MANAGER_PROPERTY_TIMEZONE ;
		message.arg_in.dog = num_i;
	    manager_common_send_message(SERVER_MANAGER, &message);
		/***************************/
		return 0;
    }

//    fp = fopen(YOUR_LINK_TIMEZONE_FILE,"w+");
//     if( fputs(tmp_str,fp) !=EOF)
//     {
//    	 log_err("fputs(tmp_str,fp) !=EOF66");
//    	 fflush(fp);
//    	 fsync(fileno(fp));
//        /********message body********/
//    	message_t message;
//    	msg_init(&message);
//    	message.message = MSG_KERNEL_TIMEZONE_CHANGE;
//    	message.sender = message.receiver = SERVER_KERNEL;
//    	message.arg_in.cat = num;
//    	server_player_message(&message);
//    	server_recorder_message(&message);
//
//    	/***************************/
//        fclose(fp);
//    	return 0;
//    }
//    fclose(fp);
    return -1;

}


static int set_restore()
{
	int status;
    message_t msg;
	char cmdstring[64]={0};
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	memset(fname,0,sizeof(fname));
	snprintf(fname,MAX_SYSTEM_STRING_SIZE*2,"%s%s",_config_.qcy_path, RESTORE_SH);
    snprintf(cmdstring,64, "%s  %s  &",fname,RESTORE);
	play_voice(SERVER_KERNEL, SPEAKER_CTL_RESET);
	sleep(5);
    status = system(cmdstring);

        /********message body********/
    	msg_init(&msg);
    	msg.message = MSG_KERNEL_SIGINT;
    	msg.sender = msg.receiver = SERVER_KERNEL;
    	/****************************/
    	manager_message(&msg);
    	//info.exit =1;
    	log_qcy(DEBUG_INFO, "into func set_restore end \n");

    if(status == 0)  return 0;
    else  return -1;

}
static int set_reboot()
{
	int ret;
    message_t msg;
	char cmd[64]={0};
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	memset(fname,0,sizeof(fname));
	snprintf(fname,MAX_SYSTEM_STRING_SIZE*2,"%s%s",_config_.qcy_path, RESTORE_SH);
    snprintf(cmd,64, "%s  %s  &",fname,REBOOT);
	sleep(1);
    ret=system(cmd);

        /********message body********/
    	msg_init(&msg);
    	msg.message = MSG_KERNEL_SIGINT;
    	msg.sender = msg.receiver = SERVER_KERNEL;
    	/****************************/
    	manager_message(&msg);
    	//info.exit =1;
    	log_qcy(DEBUG_INFO, "into func set_reboot  end\n");

    if(ret == 0)  return 0;
    else  return -1;

}

static int server_set_status(int type, int st)
{
	int ret=-1;
	ret = pthread_rwlock_wrlock(&info.lock);
	if(ret)	{
		log_err("add lock fail, ret = %d", ret);
		return ret;
	}
	if(type == STATUS_TYPE_STATUS)
		info.status = st;
	else if(type==STATUS_TYPE_EXIT)
		info.exit = st;
	/*else if(type==STATUS_TYPE_CONFIG)
		kernel_wifi_config.status = st;*/
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret)
		log_err("add unlock fail, ret = %d", ret);
	return ret;
}

static int server_message_proc(void)
{
	int ret = 0;
	message_t msg;
	message_t send_msg;
	msg_init(&msg);
	msg_init(&send_msg);
	//condition
	pthread_mutex_lock(&k_mutex);
	if( message.head == message.tail ) {
		if( info.status==STATUS_RUN ) {
			pthread_cond_wait(&k_cond,&k_mutex);
		}
	}
	ret = msg_buffer_pop(&message, &msg);
	pthread_mutex_unlock(&k_mutex);
	if( ret == -1) {
		msg_free(&msg);
		return -1;
	}
	else if( ret == 1) {
		return 0;
	}
	switch(msg.message){
		case MSG_MANAGER_EXIT:
			log_qcy(DEBUG_INFO, " kernel MSG_MANAGER_EXIT");
			server_set_status(STATUS_TYPE_EXIT,1);
			break;
		case MSG_MANAGER_TIMER_ACK:
			((HANDLER)msg.arg_in.handler)();
			break;
		case MSG_KERNEL_CTRL_TIMEZONE:
			if( msg.arg_in.cat == KERNEL_SET_TZ ) {
				//ret = set_timezone(msg.arg);
				send_iot_ack(&msg, &send_msg, MSG_KERNEL_CTRL_TIMEZONE_ACK, msg.receiver, 0,0, 0);
			}
			break;
		case MSG_KERNEL_ACTION:
			if( msg.arg_in.cat == KERNEL_ACTION_REBOOT ) {
				send_iot_ack(&msg, &send_msg, MSG_KERNEL_ACTION_ACK, msg.receiver, 0,0, 0);
				ret = set_reboot();
			}
			else if( msg.arg_in.cat == KERNEL_ACTION_RESTORE ) {
				ret = set_restore();
				send_iot_ack(&msg, &send_msg, MSG_KERNEL_ACTION_ACK, msg.receiver, ret,0, 0);
			}
			break;
		case MSG_KERNEL_OTA_DOWNLOAD:
			if(msg.arg_in.wolf){
						if(msg.arg_in.dog == OTA_MODE_NORMAL)
						{
							if(msg.arg_in.chick == OTA_PROC_DNLD)
							{
								log_qcy(DEBUG_INFO, "send_iot_ack OTA_PROC_DNLD  ok \n");
								ret=ota_dowmload_date(msg.arg,msg.arg_size);
								send_iot_ack(&msg, &send_msg, MSG_KERNEL_OTA_DOWNLOAD_ACK, msg.receiver, ret,0, 0);

							}
							if(msg.arg_in.chick == OTA_PROC_INSTALL)
							{
								ret=ota_install_fun(msg.arg,msg.arg_size,msg.extra,msg.extra_size,msg.arg_in.tiger);

								send_iot_ack(&msg, &send_msg, MSG_KERNEL_OTA_DOWNLOAD_ACK, msg.receiver, ret,0, 0);
								log_qcy(DEBUG_INFO, "send_iot_ack OTA_PROC_DNLD  ok \n");
							}
							if(msg.arg_in.chick == OTA_PROC_DNLD_INSTALL)
							{
								log_qcy(DEBUG_INFO, "send_iot_ack OTA_PROC_DNLD_INSTALL  --- \n");
								send_iot_ack(&msg, &send_msg, MSG_KERNEL_OTA_DOWNLOAD_ACK, msg.receiver, 0,0, 0);
								ret=ota_dowmload_date(msg.arg,msg.arg_size);
								if(ret ==0){
								ret=ota_install_fun(msg.arg,msg.arg_size,msg.extra,msg.extra_size,msg.arg_in.tiger);
								//log_info("send_iot_ack MSG_KERNEL_OTA_DOWNLOAD  ok \n");
								}
							}
						}
						else if(msg.arg_in.dog == OTA_MODE_SILENT){
							//send_iot_ack(&msg, &send_msg, OTA_MODE_SILENT, msg.receiver, ret,0, 0);
						}
			}
			else   send_iot_ack(&msg, &send_msg, MSG_KERNEL_OTA_DOWNLOAD_ACK, msg.receiver, -1,0, 0);
			break;
		case MSG_KERNEL_OTA_REQUEST:
			if( msg.arg_in.cat == OTA_INFO_PROGRESS ) {
				ota_status.progress=kernel_ota_get_progress();
				ota_status.status=kernel_ota_get_status();
				ota_status.error_msg = kernel_ota_get_error_msg();
				send_ota_ack(&msg,&send_msg, MSG_KERNEL_OTA_REQUEST_ACK, msg.receiver, 0, ota_status.status, ota_status.progress,ota_status.error_msg);
				log_qcy(DEBUG_INFO, "------send_iot_ack  OTA_INFO_PROGRESS  ok \n");
			}
			else if( msg.arg_in.cat == OTA_INFO_STATUS ) {
					ota_status.status=kernel_ota_get_status();
					ota_status.progress=kernel_ota_get_progress();
					ota_status.error_msg = kernel_ota_get_error_msg();
					send_ota_ack(&msg,&send_msg, MSG_KERNEL_OTA_REQUEST_ACK, msg.receiver, 0, ota_status.status, ota_status.progress,ota_status.error_msg);
					log_qcy(DEBUG_INFO, "------send_iot_ack  OTA_INFO_STATUS  ok \n");
			}
			break;
		case MSG_KERNEL_OTA_REPORT:
			if( msg.arg_in.cat == OTA_REPORT ) {
				ota_status.progress=kernel_ota_get_progress();
				ota_status.status=kernel_ota_get_status();
				ota_status.error_msg = kernel_ota_get_error_msg();
				//log_info("ota_status.progress=%d --ota_status.status=%d,ota_status.error_msg=%d\n",ota_status.progress, ota_status.status,ota_status.error_msg);
				send_ota_ack(&msg,&send_msg, MSG_KERNEL_OTA_REPORT_ACK, msg.receiver, 0, ota_status.status,ota_status.progress,ota_status.error_msg);
				log_qcy(DEBUG_INFO, "------send_iot_ack  OTA_REPORT  ok \n");
			}
			break;
		case MSG_MIIO_PROPERTY_GET_ACK:
			log_qcy(DEBUG_INFO, "into  kernel  MSG_MIIO_PROPERTY_GET_ACK  from server miio\n");
			log_qcy(DEBUG_INFO, " msg.arg_in.cat =%d  msg.arg_in.dog =%d \n", msg.arg_in.cat,msg.arg_in.dog);
			if( msg.arg_in.cat == MIIO_PROPERTY_CLIENT_STATUS ) {
					if(msg.arg_in.dog == STATE_CLOUD_CONNECTED)
					{
						if(info.status == STATUS_NONE )
						{
							if(hang_up_flag != 1){
								ota_status.progress=kernel_ota_get_progress();
								ota_status.status=kernel_ota_get_status();
								ota_status.error_msg = kernel_ota_get_error_msg();
								//log_info("ota_status.progress=%d --ota_status.status=%d,ota_status.error_msg=%d\n",ota_status.progress, ota_status.status,ota_status.error_msg);
								send_ota_ack(&msg,&send_msg, MSG_KERNEL_OTA_REPORT_ACK, msg.receiver, 0, ota_status.status,ota_status.progress,ota_status.error_msg);
								//log_info("------send_iot_ack  MSG_MIIO_PROPERTY_NOTIFY  OTA_REPORT  ok \n");
							}
							server_set_status(STATUS_TYPE_STATUS, STATUS_WAIT);
						}

					}
			}
			break;
		default:
			log_qcy(DEBUG_INFO, "not processed message = %d", msg.message);
			break;
	}
	msg_free(&msg);
	return ret;
}

static int config_kernel_read(void)
{
	int ret,ret1=0;
	ret1 |= ret;
	if(ret==0)
	{
		ret=read_ota_config_file();
		ret1 |= ret;
	}
	ret1 |= ret;
	return ret1;
}

//
static void task_default(void)
{
	int ret = 0;
	message_t msg;
	switch( info.status ){
		case STATUS_NONE:
			  /********message body********/
			msg_init(&msg);
			msg.message = MSG_MIIO_PROPERTY_GET;
			msg.sender = msg.receiver = SERVER_KERNEL;
			msg.arg_in.cat = MIIO_PROPERTY_CLIENT_STATUS;
			server_miio_message(&msg);
			/****************************/
			sleep(2);
			log_qcy(DEBUG_INFO, " kernel task_default  STATUS_NONE");
			break;
		case STATUS_WAIT:
			log_qcy(DEBUG_INFO, " kernel task_default  STATUS_WAIT");
			//initialization  ota_status
			memset(&ota_status,0,sizeof(kernel_ota_status_info_t));
			ret=config_kernel_read();
			if(!ret)
			server_set_status(STATUS_TYPE_STATUS, STATUS_SETUP);
			break;
		case STATUS_SETUP:
			log_qcy(DEBUG_INFO, "create kernel server finished");
		    server_set_status(STATUS_TYPE_STATUS, STATUS_START);
			break;
		case STATUS_IDLE:
		//	if(hang_up_flag == 1)
				//server_set_status(STATUS_TYPE_STATUS, STATUS_WAIT);
			sleep(1);
			break;
		case STATUS_START:
			server_set_status(STATUS_TYPE_STATUS, STATUS_RUN);
			break;
		case STATUS_RUN:
			   //log_info("create kernel server finished1111");
			break;
		case STATUS_STOP:
			break;
		case STATUS_RESTART:
			server_set_status(STATUS_TYPE_STATUS, STATUS_WAIT);
			break;
		case STATUS_ERROR:
		//	info.task.func = task_error;
			break;
	}
	usleep(10000);

	return;
}

static void server_kernel_thread_termination(int arg)
{
	message_t msg;
    /********message body********/
	msg_init(&msg);
	msg.message = MSG_KERNEL_SIGINT;
	msg.sender = msg.receiver = SERVER_KERNEL;
	/****************************/
	manager_message(&msg);
	//info.exit =1;
	log_qcy(DEBUG_INFO, "----send------info.exit =1;----------");
}

static int server_release(void)
{
	return 0;
}
/*
 * server entry point
 */
static void *server_func(void *arg)
{
    signal(SIGINT, server_kernel_thread_termination);
    signal(SIGTERM, server_kernel_thread_termination);
	misc_set_thread_name("server_kernel");
	pthread_detach(pthread_self());
	if( !message.init ) {
		msg_buffer_init(&message, MSG_BUFFER_OVERFLOW_NO);
	}
	memset(&info, 0, sizeof(server_info_t));
	//default task
	if(k_hang_up_flag==1)    {  info.status=STATUS_WAIT;  }
	else {
	info.status=STATUS_NONE;}
	info.task.func = task_default;
	info.task.start = STATUS_NONE;
	info.task.end = STATUS_RUN;
	while( !info.exit ) {
		info.task.func();
		server_message_proc();
	}
	if( info.exit ) {
		k_hang_up_flag=1;
	    /********message body********/
		message_t msg;
		msg_init(&msg);
		msg.message = MSG_MANAGER_EXIT_ACK;
		msg.sender = SERVER_KERNEL;
		manager_message(&msg);
		/***************************/
	}
	msg_buffer_release(&message);
	server_release();
	log_qcy(DEBUG_INFO, "-----------thread exit: server_kernel-----------");
	pthread_exit(0);
}


//
///*
// * external interface
// */
int server_kernel_start(void)
{
	int ret=-1;
	pthread_rwlock_init(&info.lock, NULL);
	ret = pthread_create(&info.id, NULL, server_func, NULL);
	if(ret != 0) {
		log_qcy(DEBUG_SERIOUS, "kernel server pthread_create error! ret = %d",ret);
		 return ret;
	 }

	else {
		log_qcy(DEBUG_SERIOUS, "kernel server create successful!");
		return 0;
	}
}

int server_kernel_message(message_t *msg)
{
	int ret=0,ret1;
//	if( server_get_status(STATUS_TYPE_STATUS)!= STATUS_RUN ) {
//		log_err("kernel server is not ready!");
//		return -1;
//	}
	if( !message.init ) {
		log_qcy(DEBUG_SERIOUS, "kernel server is not ready for message processing!");
		return -1;
	}
	ret = pthread_rwlock_wrlock(&message.lock);
	if(ret)	{
		log_err("add message lock fail, ret = %d\n", ret);
		pthread_rwlock_unlock(&message.lock);
		return ret;
	}
	ret = msg_buffer_push(&message, msg);
	if( ret!=0 )
		log_err("message push in kernel error =%d", ret);
	else {
		pthread_cond_signal(&k_cond);
	}
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1)
		log_err("add message unlock fail, ret = %d\n", ret1);
	return ret;
}
