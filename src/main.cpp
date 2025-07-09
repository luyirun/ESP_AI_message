/**
 * @FilePath     : /ESP_AI/src/main.cpp
 * @Description  : 语音交互设备主程序（含消息状态更新功能）
 * @Author       : 董文捷
 * @Version      : 0.0.2
 * @LastEditors  : 董文捷
 * @LastEditTime : 2025-07-07 10:30:00
 * @Copyright    : G AUTOMOBILE RESEARCH INSTITUTE CO.,LTD Copyright (c) 2025.
 **/
#include <esp-ai.h>
#include "mqtt.h"
#include <OneButton.h>
#include <base64.h>
#include <mbedtls/md.h>
#include <WebSocketsClient.h>
#include <driver/i2s.h>
#include "xf_stt.h"
#undef typeof
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

ESP_AI esp_ai;

// 引脚定义
#define BUTTON_PIN 7      // 配对/获取消息按钮引脚
#define REC_BUTTON_PIN 21 // 录音专用按钮引脚
OneButton button(BUTTON_PIN, true, true);

// 全局状态变量
bool isPaired = false;
bool waitingPairRequest = false; // B设备：是否收到A的配对请求
String lastAsrText = "";         // 最近一次语音识别文本
bool recBtnLast = false;
bool getNewMessage = false;

// 留言功能相关
bool newMessageAvailable = false; // 新消息标志
const char *getLatestMessageUrl = "http://8.130.119.173:8090/api/getLatestMessage/";
const char *addMessageUrl = "http://8.130.119.173:8090/api/addMessage/";
const char *updateIsReadUrl = "http://8.130.119.173:8090/api/updateLatestIsRead/"; // 新增：更新isRead的接口

// 设备信息
#if DEVICE_ROLE_A 
const String person_username = "ESP32_Voice_A";
#else
const String person_username = "ESP32_Voice_B";
#endif

extern String xf_stt_result;
extern void setup_ntp_client();

// NTP时间同步
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp.aliyun.com", 8 * 3600, 60000); // 北京时间（UTC+8）

// 函数声明
void addMessage(const String &person_username, const String &message);
void updateLatestMessageIsRead();


// 语音识别结果回调
void onAiEvent(String command_id, String data)
{
    if (command_id == "on_iat_cb")
    {
        lastAsrText = data;
        Serial.print("[情感交互机器人-语音识别结果：] ");
        Serial.println(lastAsrText);
        
        // 识别到有效文本时存入数据库
        if (lastAsrText.length() > 0)
        {
            xf_stt_result = lastAsrText;
            newMessageAvailable = true;
            addMessage(person_username, lastAsrText);
        }
    }
}

// 新增消息到服务器
void addMessage(const String &person_username, const String &message)
{
    // 打印 person_username 的值
    Serial.println("当前 person_username: " + person_username); 

    HTTPClient http;
    http.begin(addMessageUrl);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String postData = "person=" + person_username + "&message=" + message;
    int httpCode = http.POST(postData);

    if (httpCode > 0)
    {
        Serial.println("添加消息响应：" + http.getString());
    }
    else
    {
        Serial.print("添加消息失败：");
        Serial.println(http.errorToString(httpCode).c_str());
    }
    http.end();
}

// 核心功能：更新最新消息的isRead状态为1（已读）
void updateLatestMessageIsRead()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi未连接，无法更新消息状态");
        return;
    }

    HTTPClient http;
    http.begin(updateIsReadUrl);
    // 设置PUT请求头（表单格式）
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // 构造参数：isRead=1（已读状态）
    String putData = "isRead=1";
    int httpCode = http.PUT(putData);

    if (httpCode > 0)
    {
        String response = http.getString();
        Serial.print("更新isRead状态响应：");
        Serial.println(response);
        if (response.indexOf("更新成功") != -1)
        {
            Serial.println("最新消息已标记为已读");
        }
        else
        {
            Serial.println("更新isRead状态失败：" + response);
        }
    }
    else
    {
        Serial.print("更新isRead请求失败：");
        Serial.println(http.errorToString(httpCode).c_str());
    }
    http.end();
}

// 获取最新消息（基础版）
void getLatestMessage()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    HTTPClient http;
    http.begin(getLatestMessageUrl);

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK)
    {
        String payload = http.getString();
        Serial.println("最新消息：" + payload);

        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, payload);
        if (error)
        {
            Serial.println("JSON解析失败：" + String(error.c_str()));
            http.end();
            return;
        }

        const char *msgPerson = doc["person"] | "";
        int isRead = doc["isRead"] | -1;
        if (strcmp(msgPerson, "ESP32_Voice_A") == 0 && isRead == 0)
        {
            String message = doc["message"] | "";
            Serial.println("播报消息：" + message);
            esp_ai.tts("最新消息：" + message);
            
            // 关键：播报后立即更新状态为已读
            updateLatestMessageIsRead();
        }
    }
    else
    {
        Serial.print("获取消息失败：");
        Serial.println(http.errorToString(httpCode).c_str());
    }
    http.end();
}

// 处理最新消息（增强版）
void processLatestMessage()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    HTTPClient http;
    http.begin(getLatestMessageUrl);

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK)
    {
        String payload = http.getString();
        Serial.println("消息详情：" + payload);

        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, payload);
        if (error)
        {
            Serial.println("解析失败：" + String(error.c_str()));
            http.end();
            return;
        }

        String msgPerson = doc["person"].as<String>();
        String message = doc["message"].as<String>();
        int isRead = doc["isRead"].as<int>();

        Serial.println("发送者：" + msgPerson + "，状态：" + (isRead ? "已读" : "未读"));
        if (isRead == 0 && msgPerson == "ESP32_Voice_A")
        {
            Serial.println("处理未读消息：" + message);
            esp_ai.tts("收到来自" + msgPerson + "的消息：" + message);
            
            // 播报后更新状态
            updateLatestMessageIsRead();
        }
    }
    else
    {
        Serial.print("处理消息失败：");
        Serial.println(httpCode);
    }
    http.end();
}

// 长按按钮回调（配对逻辑）
void onLongPressStart()
{
#if DEVICE_ROLE_A
    if (!isPaired)
    {
        if (waitingPairRequest)
        {
            esp_ai.tts("配对成功");
            mqtt_send("{\"pair_accept\":true}");
            isPaired = true;
            waitingPairRequest = false;
        }
        else
        {
            esp_ai.tts("等待配对");
            mqtt_send("{\"pair\":true}");
        }
    }
    else
    {
        esp_ai.tts("关闭异地模式");
        mqtt_send("{\"remote_off\":true}");
        isPaired = false;
    }
#else
    if (!isPaired)
    {
        if (!waitingPairRequest)
        {
            esp_ai.tts("等待配对");
            mqtt_send("{\"pair\":true}");
        }
        else
        {
            esp_ai.tts("配对成功");
            mqtt_send("{\"pair_accept\":true}");
            isPaired = true;
            waitingPairRequest = false;
        }
    }
    else
    {
        esp_ai.tts("关闭异地模式");
        mqtt_send("{\"remote_off\":true}");
        isPaired = false;
    }
#endif
}

void setup()
{
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLUP);     // 按钮引脚上拉
    pinMode(REC_BUTTON_PIN, INPUT_PULLUP); // 新增录音按钮上拉

    // [必  填] 是否调试模式， 会输出更多信息
    bool debug = true;

    // wifi 配置： { wifi 账号， wifi 密码 }  注意：要用双引号！
    ESP_AI_wifi_config wifi_config = {"", "", "ESP-AI"};

    // 服务地址，用开发者平台，只需要配置为空，需要在配网页面配置。
    ESP_AI_server_config server_config = {};

    // 离线唤醒方案：{ 方案, 识别阈值 }, "edge_impulse" | "diy"，为 "diy" 时可调用 esp_ai.wakeUp() 方法进行唤醒
    ESP_AI_wake_up_config wake_up_config = {};
    strcpy(wake_up_config.wake_up_scheme, "asrpro"); // 唤醒方案
    strcpy(wake_up_config.str, "start");             // 串口和天问asrpro 唤醒时需要配置的字符串，也就是从另一个开发版发送来的字符串

    // 开始运行 ESP-AI
    esp_ai.begin({debug, wifi_config, server_config, wake_up_config});
    esp_ai.setVolume(10);      // 设置音量，范围 0-10
    esp_ai.onEvent(onAiEvent); // 注册语音识别回调

    // 设备A/B参数配置
#if DEVICE_ROLE_A
    mqtt_set_params("deviceA_id", "username", "password", "A_in", "B_in");
#else
    mqtt_set_params("deviceB_id", "username", "password", "B_in", "A_in");
#endif

    // 等待WiFi连接成功
    while (!esp_ai.wifiIsConnected())
    {
        delay(100);
    }
    Serial.println("WiFi已连接,初始化MQTT...");
    mqtt_init();
    while (!mqtt_connect())
    {
        Serial.println("MQTT连接失败,重试中...");
        delay(2000);
    }
    Serial.println("MQTT已连接");

    // 初始化NTP时间同步
    setup_ntp_client();
    Serial.println("NTP时间同步初始化完成");
    
    // 按钮初始化
    button.attachLongPressStart(onLongPressStart);
}

void loop()
{
    static unsigned long lastProcessTime = 0;
    const unsigned long processInterval = 30000; // 30秒自动检查一次消息

    esp_ai.loop();
    mqtt_poll();
    button.tick();
    timeClient.update();

    // 配对状态下的串口-MQTT转发
    if (isPaired && Serial.available() > 0)
    {
        String msg = Serial.readStringUntil('\n');
        if (msg != "start")
        {
            mqtt_send("{\"tts\":\"" + msg + "\"}");
        }
        else
        {
            esp_ai.wakeUp();
        }
    }

    // 录音按钮控制
    bool recBtn = digitalRead(REC_BUTTON_PIN) == LOW;
    if (recBtn && !recBtnLast)
    {
        Serial.println("开始录音...");
        startXFSTT_record_begin();
        newMessageAvailable = false;
    }
    if (!recBtn && recBtnLast)
    {
        Serial.println("停止录音，识别中...");
        startXFSTT_record_end();
    }
    recBtnLast = recBtn;

    // 获取消息按钮控制
    bool getMsgBtn = digitalRead(BUTTON_PIN) == LOW;
    if (getMsgBtn)
    {
        Serial.println("获取最新消息...");
        getLatestMessage();
        processLatestMessage();
        delay(500); // 防抖动
    }

    // 定时自动检查消息
    if (millis() - lastProcessTime > processInterval)
    {
        Serial.println("定时检查最新消息...");
        processLatestMessage();
        lastProcessTime = millis();
    }

    startXFSTT_record_loop();
}
