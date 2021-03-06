/*
 * kernel_ota.h
 *
 *  Created on: 2020年11月1日
 *      Author: kang
 */

#ifndef SERVER_KERNEL_KERNEL_OTA_H_
#define SERVER_KERNEL_KERNEL_OTA_H_
/*
 * header
 */
#include "../../manager/global_interface.h"
#define 	OTA_DOWNLOAD_APPLICATION_NAME			"/tmp/qcy_camera_s1pro.zip"
#define 	OTA_UPDAT_NAME							"/tmp/oat_update.zip"
#define 	OTA_WGET_LOG							"/tmp/wget.log"
#define 	CONFIG_KERNEL_OTA_PATH					"config/kernel_ota_update.config"

/*
 * define
 */
/*
 * structure
 */
typedef struct kerbel_ota_config_t {
	int		status;
	int		progress;
    char 	url[MAX_SYSTEM_STRING_SIZE*8];
    char 	md5[MAX_SYSTEM_STRING_SIZE*4];
    int 	mode;
    int 	proc;
    int     error_msg;
    int    original_length;
    int    original_data_len;
} kernel_ota_config_t;

/*
 * function
 */

int ota_dowmload_date(char *url,unsigned int ulr_len);
int ota_install_fun(char *url,unsigned int ulr_len,char *ota_md5,unsigned int ota_md5_len,int original_length);
int read_ota_config_file(void);
int kernel_ota_get_status(void);
int kernel_ota_get_error_msg(void);
int kernel_ota_get_progress(void);
int ota_process_main(char *arg);
#endif /* SERVER_KERNEL_KERNEL_OTA_H_ */
