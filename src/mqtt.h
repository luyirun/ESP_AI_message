/*
 * @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @Date: 2025-07-04 16:10:17
 * @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @LastEditTime: 2025-07-06 18:22:30
 * @FilePath: \.gitf:\VS code\github\ESP-AI\src\mqtt.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
/**
 * @FilePath     : /ESP_AI/src/mqtt.h
 * @Description  :
 * @Author       : 董文捷
 * @Version      : 0.0.1
 * @LastEditors  : 董文捷
 * @LastEditTime : 2025-07-03 21:12:03
 * @Copyright    : G AUTOMOBILE RESEARCH INSTITUTE CO.,LTD Copyright (c) 2025.
 **/

#pragma once
#define DEVICE_ROLE_A 0 // 1为A，0为B，编译时切换
#include <Arduino.h>



void mqtt_set_params(const String &clientid, const String &user, const String &pass, const String &inTopic, const String &outTopic);
void mqtt_init();
bool mqtt_connect();
void mqtt_poll();
void mqtt_send(const String &payload);