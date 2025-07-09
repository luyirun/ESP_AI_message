#include "xf_stt.h"
#include <WebSocketsClient.h>
#include <base64.h>
#include <mbedtls/md.h>
#include <driver/i2s.h>
#include <Arduino.h>
#include <time.h>
#include <esp-ai.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#undef typeof // 解决ArduinoJson与ESP32的typeof宏冲突
#include <ArduinoJson.h>
#include "mqtt.h"
extern bool isPaired;
extern const String person_username; // 引入设备名称

extern WiFiUDP ntpUDP;  // 引用 main.cpp 中的 ntpUDP
extern NTPClient timeClient;  // 引用 main.cpp 中的 timeClient
// 声明 addMessage 函数
void addMessage(const String &person, const String &message);

const char *STTAPPID = "82d46d68";
const char *STTAPISecret = "ZmY1NmUxZjAyYmEwNGU5OTE5Zjk2NTJh";
const char *STTAPIKey = "0b6d67b54713587f145e3d43ce410ac8";
WebSocketsClient xf_client;
int16_t *xf_pcm_data = nullptr;
uint xf_recordingSize = 0;

// // NTP时间配置
// WiFiUDP ntpUDP;
// NTPClient timeClient(ntpUDP, "pool.ntp.org");

// 录音区间采集相关变量
static int16_t *xf_record_buffer = nullptr;
static size_t xf_recordingSizeBtn = 0;
static bool xf_isRecording = false;
static const int xf_maxSeconds = 10;
static const int xf_sample_rate = 16000;
static const int xf_maxSamples = xf_sample_rate * xf_maxSeconds;

String xf_stt_result = "";

// 初始化NTP客户端
void setup_ntp_client()
{
    timeClient.begin();
    timeClient.setTimeOffset(0); // GMT时间偏移
}

String unixTimeToGMTString(time_t unixTime)
{
    char buffer[80];
    struct tm timeinfo;
    gmtime_r(&unixTime, &timeinfo);
    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &timeinfo);
    return String(buffer);
}

String getDateTime()
{
    timeClient.update();
    unsigned long epochTime = timeClient.getEpochTime();
    return unixTimeToGMTString(epochTime);
}

String formatDateForURL(String dateString)
{
    dateString.replace(" ", "+");
    dateString.replace(",", "%2C");
    dateString.replace(":", "%3A");
    return dateString;
}

String XF_wsUrl(const char *Secret, const char *Key, String request, String host)
{
    String timeString = getDateTime();
    String signature_origin = "host: " + host + "\n";
    signature_origin += "date: " + timeString + "\n";
    signature_origin += "GET " + request + " HTTP/1.1";
    unsigned char hmacResult[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)Secret, strlen(Secret));
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)signature_origin.c_str(), signature_origin.length());
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    mbedtls_md_free(&ctx);
    String base64Result = base64::encode(hmacResult, 32);
    String authorization_origin = "api_key=\"" + String(Key) + "\", algorithm=\"hmac-sha256\", headers=\"host date request-line\", signature=\"" + base64Result + "\"";
    String authorization = base64::encode(authorization_origin);
    String url = "ws://" + host + request;
    url += "?authorization=" + authorization;
    url += "&date=" + formatDateForURL(timeString);
    url += "&host=" + host;
    return url;
}

// 讯飞STT WebSocket回调
// 讯飞STT WebSocket回调
void xf_ws_on_event(WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case WStype_DISCONNECTED:
        Serial.println("[STT] WebSocket连接断开");
        break;
    case WStype_CONNECTED:
        Serial.println("[STT] WebSocket连接成功");
        break;
    case WStype_ERROR:
        Serial.println("[STT] WebSocket连接错误");
        break;
    case WStype_TEXT:
    {
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, String((char *)payload));
        if (error)
            break;
        if (doc["data"].containsKey("result"))
        {
            JsonArray ws = doc["data"]["result"]["ws"];
            for (JsonObject word : ws)
            {
                xf_stt_result += word["cw"][0]["w"].as<const char *>();
            }
            if (doc["data"].containsKey("status") && doc["data"]["status"] == 2)
            {
                Serial.print("[STT] 识别结果: ");
                Serial.println(xf_stt_result);
                #if DEVICE_ROLE_A
                // 新增：调用 addMessage 函数发送数据到 MySQL 数据库
                addMessage("ESP32_Voice_A", xf_stt_result);
                #else
                addMessage("ESP32_Voice_B", xf_stt_result);
                #endif

                if (isPaired && xf_stt_result.length() > 0)
                {
                    String jsonPayload = "{\"tts\":\"" + xf_stt_result + "\"}";
                    mqtt_send(jsonPayload);
                }
                xf_stt_result = "";
            }
        }
        break;
    }
    default:
        break;
    }
}

void XF_STTsend()
{
    uint8_t status = 0;
    int dataSize = 1280 * 8;
    int audioDataSize = xf_recordingSize * 2;
    uint lan = audioDataSize / dataSize;
    uint lan_end = audioDataSize % dataSize;
    if (lan_end > 0)
        lan++;

    String host_url = XF_wsUrl(STTAPISecret, STTAPIKey, "/v2/iat", "ws-api.xfyun.cn");
    Serial.println("[STT] 连接讯飞STT服务器...");
    String host = "ws-api.xfyun.cn";
    uint16_t port = 80;
    String url = host_url.substring(host_url.indexOf('/', 5));
    xf_client.begin(host.c_str(), port, url.c_str());
    xf_client.onEvent(xf_ws_on_event);
    Serial.println("[STT] 等待WebSocket连接...");
    unsigned long connect_start = millis();
    while (millis() - connect_start < 3000)
    {
        xf_client.loop();
        if (xf_client.isConnected())
        {
            break;
        }
        delay(10);
    }
    if (!xf_client.isConnected())
    {
        Serial.println("[STT] WebSocket连接失败");
        return;
    }
    for (int i = 0; i < lan; i++)
    {
        if (i == (lan - 1))
            status = 2;
        String input = "{";
        if (status == 0)
        {
            input += "\"common\":{ \"app_id\":\"" + String(STTAPPID) + "\" },";
            input += "\"business\":{\"domain\": \"iat\", \"language\": \"zh_cn\", \"accent\": \"mandarin\", \"vinfo\":1,\"vad_eos\":10000},";
            input += "\"data\":{\"status\": 0, \"format\": \"audio/L16;rate=16000\",\"encoding\": \"raw\",\"audio\":\"";
        }
        else
        {
            input += "\"data\":{\"status\": " + String(status) + ", \"format\": \"audio/L16;rate=16000\",\"encoding\": \"raw\",\"audio\":\"";
        }
        int sendSize = (status == 2 && lan_end > 0) ? lan_end : dataSize;
        String base64audioString = base64::encode((uint8_t *)xf_pcm_data + i * dataSize, sendSize);
        input += base64audioString + "\"}}";
        xf_client.sendTXT(input);
        if (status == 0)
            status = 1;
        delay(30);
    }
    // 等待服务器返回，最多8秒
    unsigned long t_start = millis();
    while (millis() - t_start < 8000)
    {
        xf_client.loop();
        delay(10);
    }
}

// 录音区间采集：按下开始，松开结束并上传
void startXFSTT_record_begin()
{
    if (xf_isRecording)
        return;
    xf_record_buffer = (int16_t *)ps_malloc(xf_maxSamples * sizeof(int16_t));
    if (!xf_record_buffer)
    {
        Serial.println("[STT] 录音内存分配失败");
        return;
    }
    xf_recordingSizeBtn = 0;
    xf_isRecording = true;
    Serial.println("[STT] 开始录音...");
}
void startXFSTT_record_end()
{
    if (!xf_isRecording)
        return;
    xf_isRecording = false;
    Serial.printf("[STT] 录音结束，采样点数: %d\n", xf_recordingSizeBtn);
    if (xf_recordingSizeBtn > 0)
    {
        xf_pcm_data = xf_record_buffer;
        xf_recordingSize = xf_recordingSizeBtn;
        XF_STTsend();
    }
    free(xf_record_buffer);
    xf_record_buffer = nullptr;
    xf_recordingSizeBtn = 0;
}
// 需在main.cpp loop中定期调用
void startXFSTT_record_loop()
{
    if (xf_isRecording && xf_recordingSizeBtn < xf_maxSamples)
    {
        size_t bytesRead = esp_ai_i2s_input.readBytes(
            (uint8_t *)(xf_record_buffer + xf_recordingSizeBtn),
            256 * sizeof(int16_t));
        xf_recordingSizeBtn += bytesRead / 2;
    }
    xf_client.loop();
}

// 保留原有一键采集上传逻辑
void startXFSTT()
{
    const int sample_rate = 16000;
    const int seconds = 5;
    const int sample_count = sample_rate * seconds;
    int16_t *buffer = (int16_t *)ps_malloc(sample_count * sizeof(int16_t));
    if (!buffer)
    {
        Serial.println("讯飞STT内存分配失败");
        return;
    }
    size_t bytes_read = 0;
    esp_ai_i2s_input.readBytes((uint8_t *)buffer, sample_count * sizeof(int16_t));
    xf_pcm_data = buffer;
    xf_recordingSize = sample_count;
    XF_STTsend();
    free(buffer);
    xf_client.loop();
}