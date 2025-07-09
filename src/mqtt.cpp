/**
 * @FilePath     : /ESP_AI/src/mqtt.cpp
 * @Description  :
 * @Author       : 董文捷
 * @Version      : 0.0.1
 * @LastEditors  : 董文捷
 * @LastEditTime : 2025-07-04 14:03:29
 * @Copyright    : G AUTOMOBILE RESEARCH INSTITUTE CO.,LTD Copyright (c) 2025.
 **/

#include "mqtt.h"
#include <ArduinoMqttClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <esp-ai.h>
#include <base64.h>
#include <mbedtls/md.h>
#include <WebSocketsClient.h>
#include <driver/i2s.h>
#include "xf_stt.h"

// MQTT 服务器信息（请根据实际情况修改）
static const char broker[] = "b00001b0.ala.cn-hangzhou.emqxsl.cn";
static const int port = 8883; // MQTTs默认端口

// 改为全局可配置变量
String g_mqtt_clientid = "mqttx_da5aa4aa";
String g_mqtt_user = "username";
String g_mqtt_pass = "password";
String g_mqtt_inTopic = "Android_Get";
String g_mqtt_outTopic = "Android_Set";

// CA证书（如需验证服务器）
static const char root_ca[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD
QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB
CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97
nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt
43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P
T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4
gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO
BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR
TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw
DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr
hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg
06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF
PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls
YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk
CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=
-----END CERTIFICATE-----
)EOF";

static WiFiClientSecure wifiClient;
static MqttClient mqttClient(wifiClient);

static String inputString = "";
extern ESP_AI esp_ai; // 声明外部esp_ai对象
extern bool isPaired;
extern bool waitingPairRequest;

extern I2SStream esp_ai_i2s_input;

static void onMqttMessage(int messageSize)
{
    Serial.print("Received a message with topic '");
    Serial.print(mqttClient.messageTopic());
    Serial.print("', length ");
    Serial.print(messageSize);
    Serial.println(" bytes:");
    while (mqttClient.available())
    {
        inputString += (char)mqttClient.read();
        if (inputString.length() == messageSize)
        {
            Serial.print("Message content: ");
            Serial.println(inputString);

            // 解析JSON消息
            DynamicJsonDocument json_msg(1024);
            deserializeJson(json_msg, inputString);

            // 新增：收到消息先停止当前会话
            esp_ai.stopSession();

            // 检查是否有tts字段，有则TTS朗读
            if (json_msg.containsKey("tts"))
            {
                String ttsText = json_msg["tts"].as<String>();
                Serial.print("收到TTS指令，播放文本: ");
                Serial.println(ttsText);
                esp_ai.tts(ttsText);
            }
            // 收到配对请求，A和B都TTS提示"对方请求配对"，并设置waitingPairRequest=true
            if (json_msg.containsKey("pair") && json_msg["pair"] == true)
            {
                esp_ai.tts("对方请求配对");
                waitingPairRequest = true;
            }
            // 新增：收到pair_accept字段，A和B都TTS配对成功
            if (json_msg.containsKey("pair_accept") && json_msg["pair_accept"] == true)
            {
                esp_ai.tts("配对成功");
                isPaired = true;
                waitingPairRequest = false;
            }
            // 新增：收到remote_off字段，TTS提示对方已关闭异地模式
            if (json_msg.containsKey("remote_off") && json_msg["remote_off"] == true)
            {
                esp_ai.tts("对方已关闭异地模式");
                isPaired = false;
                waitingPairRequest = false;
            }

            inputString = "";
        }
    }
    Serial.println();
}

void mqtt_set_params(const String &clientid, const String &user, const String &pass, const String &inTopic, const String &outTopic)
{
    g_mqtt_clientid = clientid;
    g_mqtt_user = user;
    g_mqtt_pass = pass;
    g_mqtt_inTopic = inTopic;
    g_mqtt_outTopic = outTopic;
}

void mqtt_init()
{
    wifiClient.setCACert(root_ca);
    mqttClient.setId(g_mqtt_clientid.c_str());
    mqttClient.setUsernamePassword(g_mqtt_user.c_str(), g_mqtt_pass.c_str());
    mqttClient.onMessage(onMqttMessage);
}

bool mqtt_connect()
{
    if (!mqttClient.connect(broker, port))
    {
        Serial.print("MQTT connection failed! Error code = ");
        Serial.println(mqttClient.connectError());
        return false;
    }
    Serial.println("You're connected to the MQTT broker!");
    mqttClient.subscribe(g_mqtt_inTopic.c_str(), 1);
    return true;
}

void mqtt_poll()
{
    mqttClient.poll();
}

void mqtt_send(const String &payload)
{
    mqttClient.beginMessage(g_mqtt_outTopic.c_str(), payload.length(), false, 1, false);
    mqttClient.print(payload);
    mqttClient.endMessage();
}