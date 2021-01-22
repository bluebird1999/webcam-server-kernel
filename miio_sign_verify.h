/****************************************************************************/
/*
 * Copyright (c) 2020 Xiaomi. All Rights Reserved.
 */
/**
 * @file miio_sign_verify.h
 *this file defined base on 固件打包格式-V2, used for DFU verify
 */

#ifndef MIIO_SIGN_VERIFY_H
#define MIIO_SIGN_VERIFY_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>


#define FILE_MD5_MAX_LEN  64
/***************defined data type by miIO.ota命令****************************/
typedef enum {
    MIIO_PROC_NONE = 0,
    MIIO_PROC_DNLD,
    MIIO_PROC_INSTALL,
    MIIO_PROC_DNLD_INSTALL,
    MIIO_PROC_MAX
} miio_proc_id_t;

typedef enum {
    MIIO_MODE_NONE = 0,
    MIIO_MODE_NORMAL,
    MIIO_MODE_MAX
} miio_mode_id_t;

typedef enum {
    MIIO_INSTALL_NONE = 0,
    MIIO_INSTALL_TRUE,
    MIIO_INSTALL_MAX
} miio_install_id_t;

typedef enum {
    MIIO_SIGN_NONE = 0,
    MIIO_SIGN_HMAC_SHA256,
    MIIO_SIGN_MAX
} miio_sign_suite_id_t;

typedef struct {
    unsigned char *root_cert; /*root cert data adress*/
    size_t cert_len; /*root cert data length*/
    char file_md5[FILE_MD5_MAX_LEN]; /*the whole DFU md5 from miIO.ota*/
    int signed_file; /*the sign flag from miIO.ota*/
    size_t original_file_len;/*original firmware length from miIO.ota*/
    miio_proc_id_t proc; /*ota process flag from miIO.ota*/
    miio_mode_id_t mode; /*ota mode flag from miIO.ota*/
    miio_install_id_t install; /*ota install flag from miIO.ota*/
} ota_ctx_t;


/*******************************************************************************
  *@Function: miio_sign_verify_init
  *
  *@ Description: 验签库初始化
  *
  *@ Input:
  *     ota_ctx             设备根证书以及通过miIO.ota命令解析的参数
  *
  *@ Output: N/A
  *
  *@ Return:
  *         0               成功
  *         非0              错误
  *@ Note:
*******************************************************************************/
int miio_sign_verify_init(ota_ctx_t *ota_ctx);


/*******************************************************************************
  *@Function: miio_sign_verify_update
  *
  *@ Description: 对下载的固件数据验签计算，可根据固件存储策略多次调用
  *                 确保传入数据是下载的固件连续顺序的数据
  *                 确保完整的固件都应该传入，以免验签失败
  *
  *@ Input:
  *     dfu_ptr     固件数据
  *     dfu_len     数据长度
  *
  *@ Output: N/A
  *
  *@ Return:
  *         0               成功
  *         非0              错误
  *@ Note:
*******************************************************************************/
int miio_sign_verify_update(unsigned char *dfu_ptr, size_t dfu_len);


/*******************************************************************************
  *@Function: mi_ota_init
  *
  *@ Description: 执行验签，返回验签结果
  *
  *@ Input: N/A
  *
  *@ Output:
  *         data_len        返回实际要写入flash的固件长度
  *         sn              返回厂商序列号
  *         sn_len          返回序列号长度
  *
  *@ Return:
  *         0               成功
  *         非0              错误
  *@ Note:
*******************************************************************************/
int miio_sign_verify_finish(size_t *data_len, unsigned char *sn, size_t *sn_len);




#endif //MIIO_SIGN_VERIFY_H

