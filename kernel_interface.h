/*
 * kernel_interface.h
 *
 *  Created on: 2020年10月19日
 *      Author: hjk
 */


#ifndef SERVER_KERNEL_KERNEL_INTERFACE_H_
#define SERVER_KERNEL_KERNEL_INTERFACE_H_

/*
 * header
 */
#include "../../manager/manager_interface.h"
#include "../../manager/global_interface.h"
/*
 * define
 */

#define		SERVER_KERNEL_VERSION_STRING			"alpha-6.1"

#define		MSG_KERNEL_BASE						   (SERVER_KERNEL<<16)
#define		MSG_KERNEL_SIGINT						MSG_KERNEL_BASE | 0x0000
#define		MSG_KERNEL_SIGINT_ACK					MSG_KERNEL_BASE | 0x1000
/*  set  timezone  */
#define		MSG_KERNEL_CTRL_TIMEZONE				MSG_KERNEL_BASE | 0x0010
#define		MSG_KERNEL_CTRL_TIMEZONE_ACK			MSG_KERNEL_BASE | 0x1010
/* miIO.reboot   miIO.restore*/
#define		MSG_KERNEL_ACTION						MSG_KERNEL_BASE | 0x0011
#define		MSG_KERNEL_ACTION_ACK					MSG_KERNEL_BASE | 0x1011
/*  download and install  */
#define		MSG_KERNEL_OTA_DOWNLOAD					MSG_KERNEL_BASE | 0x0012
#define		MSG_KERNEL_OTA_DOWNLOAD_ACK				MSG_KERNEL_BASE | 0x1012
/*  request  install_status\ install_error_msg\install_progress   */
#define		MSG_KERNEL_OTA_REQUEST					MSG_KERNEL_BASE | 0x0013
#define		MSG_KERNEL_OTA_REQUEST_ACK				MSG_KERNEL_BASE | 0x1013
#define		MSG_KERNEL_OTA_REPORT					MSG_KERNEL_BASE | 0x0014
#define		MSG_KERNEL_OTA_REPORT_ACK				MSG_KERNEL_BASE | 0x1014
#define		MSG_KERNEL_TIMEZONE_CHANGE				MSG_KERNEL_BASE | 0x0015
#define		MSG_KERNEL_TIMEZONE_CHANGE_ACK			MSG_KERNEL_BASE | 0x1015



/*TZ contrl*/
#define		KERNEL_SET_TZ							0x01
/*ACTION contrl*/
#define		KERNEL_ACTION_REBOOT					0x02
#define		KERNEL_ACTION_RESTORE					0x03
/*UPDATE TYPE contrl*/
#define		OTA_INFO_STATUS 						0x04
#define		OTA_INFO_PROGRESS 						0x05
#define		OTA_REPORT		 						0x06

/*
 * structure
 * 其他服务器查询ota的升级状态、升级进度就会返回该结构体:
 */
typedef struct kernel_ota_status_info_t {
	int  	status;
	int     error_msg;
	int     progress;
} kernel_ota_status_info_t;

/*
 * function
 */

int server_kernel_start(void);
int server_kernel_message(message_t *msg);

#endif /* SERVER_KERNEL_KERNEL_INTERFACE_H_ */
