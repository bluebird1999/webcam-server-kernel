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
//program header
#include "../../tools/tools_interface.h"
#include "../../manager/manager_interface.h"
#include "../../server/miio/miio_interface.h"
#include "../../server/miss/miss_interface.h"
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

//function
//common
static void *server_func(void);
static int server_message_proc(void);
static void task_default(void);
//static void task_error(void);
//static int server_release(void);
static int server_get_status(int type);
static int server_set_status(int type, int st);
static void server_thread_termination(void);
static int send_message(int receiver, message_t *msg);
static void *server_func(void);
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

static int send_iot_ack(message_t *org_msg, message_t *msg, int id, int receiver, int result, void *arg, int size)
{
	int ret = 0;
    /********message body********/
	msg_init(msg);
	memcpy(&(msg->arg_pass), &(org_msg->arg_pass),sizeof(message_arg_t));
	msg->message = id | 0x1000;
	msg->sender = msg->receiver = SERVER_MICLOUD;
	msg->result = result;
	msg->arg = arg;
	msg->arg_size = size;
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

static int set_timezone(char *arg)
{

	int ret;
	char cmd[64]={0};
	log_info("into func set_timezone arg=%s\n",arg);
    sprintf(cmd, "ln -fs  %s%s  %s", UCLIBC_TIMEZONE_DIR,arg,YOUR_LINK_TIMEZONE_FILE);
	log_err("set_timezone cmd = %s", cmd);
    ret=my_system(cmd);
    if(ret == 0)  return 0;
    else  return -1;

}

static int set_restore()
{
	int ret;
	char cmd[32]={0};
	log_info("into func set_restore \n");
    sprintf(cmd, "%s  &",RESTORE_SH);
    ret=my_system(cmd);
    if(ret == 0)  return 0;
    else  return -1;

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
	int ret = 0, ret1 = 0;
	message_t msg;
	message_t send_msg;
	msg_init(&msg);
	msg_init(&send_msg);
	int st;
	ret = pthread_rwlock_wrlock(&message.lock);
	if(ret)	{
		log_err("add message lock fail, ret = %d\n", ret);
		return ret;
	}
	ret = msg_buffer_pop(&message, &msg);
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1) {
		log_err("add message unlock fail, ret = %d\n", ret1);
	}
	if( ret == -1) {
		msg_free(&msg);
		return -1;
	}
	else if( ret == 1) {
		return 0;
	}
	switch(msg.message){
		case MSG_MANAGER_EXIT:
			server_set_status(STATUS_TYPE_EXIT,1);
			break;
		case MSG_MANAGER_TIMER_ACK:
			((HANDLER)msg.arg_in.handler)();
			break;
		case MSG_KERNEL_CTRL_TIMEZONE:
			if( msg.arg_in.cat == KERNEL_SET_TZ ) {
				ret = set_timezone(msg.arg);
				send_iot_ack(&msg, &send_msg, MSG_KERNEL_CTRL_TIMEZONE_ACK, msg.receiver, ret,0, 0);
			}
			break;
		case MSG_KERNEL_ACTION:
			if( msg.arg_in.cat == KERNEL_ACTION_REBOOT ) {
				ret = set_reboot();
				send_iot_ack(&msg, &send_msg, MSG_KERNEL_ACTION_ACK, msg.receiver, ret,0, 0);
			}
			else if( msg.arg_in.cat == KERNEL_ACTION_RESTORE ) {
				ret = set_restore();
				send_iot_ack(&msg, &send_msg, MSG_KERNEL_ACTION_ACK, msg.receiver, ret,0, 0);
			}
			break;
		case MSG_KERNEL_OTA_DOWNLOAD:
			if( msg.arg_in.cat == OTA_TYPE_APPLICATION ) {
				if(msg.arg_in.dog == OTA_MODE_NORMAL)
				{
					if(msg.arg_in.chick == OTA_PROC_DNLD)
					{
						ret=ota_dowmload_date(msg.arg,msg.arg_size,OTA_TYPE_APPLICATION);
						send_iot_ack(&msg, &send_msg, MSG_KERNEL_OTA_DOWNLOAD_ACK, msg.receiver, ret,0, 0);
					}
					if(msg.arg_in.chick == OTA_PROC_INSTALL)
					{
						ret=ota_install_fun(msg.arg,msg.arg_size,msg.extra,msg.extra_size,OTA_TYPE_APPLICATION);

						send_iot_ack(&msg, &send_msg, MSG_KERNEL_OTA_DOWNLOAD_ACK, msg.receiver, ret,0, 0);
					}
					if(msg.arg_in.chick == OTA_PROC_DNLD_INSTALL)
					{

						ret=ota_dowmload_date(msg.arg,msg.arg_size,OTA_TYPE_APPLICATION);
						if(ret!=0)
						{
							log_info("download command execute failed");
							send_iot_ack(&msg, &send_msg, MSG_KERNEL_OTA_DOWNLOAD_ACK, msg.receiver, ret,0, 0);
						}
						else {
						ret=ota_install_fun(msg.arg,msg.arg_size,msg.extra,msg.extra_size,OTA_TYPE_APPLICATION);
						send_iot_ack(&msg, &send_msg, MSG_KERNEL_OTA_DOWNLOAD_ACK, msg.receiver, ret,0, 0);
						}

					}
				}
				else if(msg.arg_in.dog == OTA_MODE_SILENT){
					send_iot_ack(&msg, &send_msg, OTA_MODE_SILENT, msg.receiver, ret,0, 0);
				}


			}
			else if( msg.arg_in.cat == OTA_TYPE_MIIO_CLIENT ) {
				//send_iot_ack(&msg, &send_msg, OTA_TYPE_MIIO_CLIENT, msg.receiver, ret,0, 0);
			}
			else if( msg.arg_in.cat == OTA_TYPE_CONFIG ) {
				//send_iot_ack(&msg, &send_msg, OTA_TYPE_CONFIG, msg.receiver, ret,0, 0);
			}
			break;
		case MSG_KERNEL_OTA_REQUEST:
			if( msg.arg_in.cat == OTA_INFO_PROGRESS ) {
				ota_status.progress=kernel_ota_get_progress();
				send_iot_ack(&msg, &send_msg, MSG_KERNEL_OTA_REQUEST_ACK, msg.receiver, 0,&ota_status, sizeof(kernel_ota_status_info_t));
						}
			else if( msg.arg_in.cat == OTA_INFO_STATUS ) {
					ota_status.status=kernel_ota_get_status();
					ota_status.error_msg = kernel_ota_get_error_msg();
				send_iot_ack(&msg, &send_msg, MSG_KERNEL_OTA_REQUEST_ACK, msg.receiver, 0,&ota_status, sizeof(kernel_ota_status_info_t));
						}
			break;
		default:
			log_err("not processed message = %d", msg.message);
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
	switch( info.status ){
		case STATUS_NONE:
			//initialization  ota_status
			memset(&ota_status,0,sizeof(kernel_ota_status_info_t));
			ret=config_kernel_read();
			if(!ret)
			server_set_status(STATUS_TYPE_STATUS, STATUS_WAIT);
			break;
		case STATUS_WAIT:
			server_set_status(STATUS_TYPE_STATUS, STATUS_SETUP);
			break;
		case STATUS_SETUP:

		   log_info("create kernel server finished");
		    server_set_status(STATUS_TYPE_STATUS, STATUS_IDLE);
			break;
		case STATUS_IDLE:
			server_set_status(STATUS_TYPE_STATUS, STATUS_START);
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
			break;
		case STATUS_ERROR:
		//	info.task.func = task_error;
			break;
	}
	usleep(10000);

	return;
}

static void server_thread_termination(void)
{
	message_t msg;
    /********message body********/
	msg_init(&msg);
	msg.message = MSG_KERNEL_SIGINT;
	msg.sender = msg.receiver = SERVER_KERNEL;
	/****************************/
	manager_message(&msg);
}

/*
 * server entry point
 */
static void *server_func(void)
{
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
	misc_set_thread_name("server_kernel");
	pthread_detach(pthread_self());
	memset(&info, 0, sizeof(server_info_t));
	//default task
	info.task.func = task_default;
	info.task.start = STATUS_NONE;
	info.task.end = STATUS_RUN;
	while( !info.exit ) {
		info.task.func();
		server_message_proc();
		heart_beat_proc();
	}
	if( info.exit ) {
		while( info.thread_start ) {
		}
	    /********message body********/
		message_t msg;
		msg_init(&msg);
		msg.message = MSG_MANAGER_EXIT_ACK;
		msg.sender = SERVER_KERNEL;
		manager_message(&msg);
		/***************************/
	}
	//server_release();
	log_info("-----------thread exit: server_miss-----------");
	pthread_exit(0);
}


//
///*
// * external interface
// */
int server_kernel_start(void)
{
	int ret=-1;
	msg_buffer_init(&message, MSG_BUFFER_OVERFLOW_NO);
	pthread_rwlock_init(&info.lock, NULL);
	ret = pthread_create(&info.id, NULL, server_func, NULL);
	if(ret != 0) {
		log_err("kernel server create error! ret = %d",ret);
		 return ret;
	 }
	else {
		log_err("kernel server create successful!");
		return 0;
	}
}

int server_kernel_message(message_t *msg)
{
	int ret=0,ret1;
	if( server_get_status(STATUS_TYPE_STATUS)!= STATUS_RUN ) {
		log_err("kernel server is not ready!");
		return -1;
	}
	ret = pthread_rwlock_wrlock(&message.lock);
	if(ret)	{
		log_err("add message lock fail, ret = %d\n", ret);
		return ret;
	}
	ret = msg_buffer_push(&message, msg);
	if( ret!=0 )
		log_err("message push in kernel error =%d", ret);
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1)
		log_err("add message unlock fail, ret = %d\n", ret1);
	return ret;
}
