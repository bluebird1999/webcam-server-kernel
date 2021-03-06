/*
 * kernel.h
 *
 *  Created on: 2020年10月19日
 *      Author: hjk
 */

#ifndef SERVER_KERNEL_KERNEL_H_
#define SERVER_KERNEL_KERNEL_H_

/*
 * header
 */

/*
 * define
 */
#define		CONFIG_KERNEL_WIFI_PROFILE			0



#define 	UCLIBC_TIMEZONE_DIR			"/usr/share/zoneinfo/uclibc/"
#define 	YOUR_LINK_TIMEZONE_FILE		"/vconf/TZ"
#define 	RESTORE_SH					"bin/wifi_reset_factory.sh"
#define 	RESTORE					    "wifi_reset"
#define 	REBOOT				   		"reboot"
#define 	TIMEZONE_INFO				"/vconf/timezone.info"
#define     CHECKING_WEBCAM_SH          "bin/checking_webcam.sh"
/*
 * structure
 */

/*
 * function
 */

int play_voice(int server_type, int type);


#endif /* SERVER_KERNEL_KERNEL_H_ */
