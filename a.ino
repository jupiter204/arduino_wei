// ESP32-CAM
// SSD1306*******************************
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define SCREEN_WIDTH 128    // OLED display width, in pixels
#define SCREEN_HEIGHT 64    // OLED display height, in pixels
#define OLED_RESET -1       // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
#define I2C_SDA 14
#define I2C_CLK 15
Adafruit_SSD1306 display; //(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
                          // WiFi*************************************
#include "esp_camera.h"
#include <WiFi.h> // 引用 Wi-Fi 函式庫
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h"          //用於電源不穩不重開機
#include "soc/rtc_cntl_reg.h" //用於電源不穩不重開機
//官方函式庫
#include "esp_http_server.h"
//#include <WebServer.h> // 引用 WebServer 函式庫
// WebServer server(80); // 建立伺服器物件，設定監聽80
const char *ssid = "";     // 基地台 SSID
const char *password = ""; // 基地台連線密碼
//輸入AP端連線帳號密碼  http://192.168.4.1
const char *apssid = "esp32-cam_01";
const char *appassword = "12345678"; // AP密碼至少要8個字元以上
//網頁檔頭
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;
// ESP32-CAM模組腳位設定
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

// DRV8833*****************************************
const byte MotorA1 = 16; // 19;  輸出至 8833 模組 A-IA
const byte MotorA2 = 2;  // 18;  輸出至 8833 模組
const byte MotorA3 = 13; // 5;  輸出至  8833 模組 A-IA
const byte MotorA4 = 12; // 17;  輸出至 8833 模組
// int DRV_E=1; //16;ru, 3.3V
//首頁
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<html>
  <head>
    <title>ESP32-CAM Robot</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body { font-family: Arial; text-align: center; margin:0px auto; padding-top: 30px;}
      table { margin-left: auto; margin-right: auto; }
      td { padding: 8 px; }
      .button {
        background-color: #2f4468;
        border: none;
        color: white;
        padding: 10px 20px;
        text-align: center;
        text-decoration: none;
        display: inline-block;
        font-size: 18px;
        margin: 6px 3px;
        cursor: pointer;
        -webkit-touch-callout: none;
        -webkit-user-select: none;
        -khtml-user-select: none;
        -moz-user-select: none;
        -ms-user-select: none;
        user-select: none;
        -webkit-tap-highlight-color: rgba(0,0,0,0);
      }
      img {  width: auto ;
        max-width: 100% ;
        height: auto ; 
      }
    </style>
  </head>
  <body>
    <h1>ESP32-CAM Robot</h1>
    <img src="" id="photo" >
    <table>
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('forward');" ontouchstart="toggleCheckbox('forward');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');"><a href=/forward> 前進 </a></button></td></tr>
      <tr><td align="center"><button class="button" onmousedown="toggleCheckbox('left');" ontouchstart="toggleCheckbox('left');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');"><a href=/left>左轉</a></button></td><td align="center"><button class="button" onmousedown="toggleCheckbox('stop');" ontouchstart="toggleCheckbox('stop');"><a href=/stop>停止</a></button></td><td align="center"><button class="button" onmousedown="toggleCheckbox('right');" ontouchstart="toggleCheckbox('right');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');"> <a href=/right> 右轉 </a> </button></td></tr>
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('backward');" ontouchstart="toggleCheckbox('backward');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');"> <a href=/back> 後退 </a> </button></td></tr>                   
    </table>
   <script>
   function toggleCheckbox(x) {
     var xhr = new XMLHttpRequest();
     xhr.open("GET", "/action?go=" + x, true);
     xhr.send();
   }
   window.onload = document.getElementById("photo").src = window.location.href.slice(0, -1) + ":81/stream";
  </script>
  </body>
</html>
)rawliteral";
//首頁程序
static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}
//影像截圖
static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;

    fb = esp_camera_fb_get();
    if (!fb)
    {
        Serial.println("Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    size_t fb_len = 0;
    fb_len = fb->len;
    res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    Serial.write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return res;
}

//影像串流程序
static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[64];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
    {
        return res;
    }

    // httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    while (true)
    {
        fb = esp_camera_fb_get();
        if (!fb)
        {
            Serial.println("Camera capture failed");
            res = ESP_FAIL;
        }
        else
        {
            if (fb->width > 400)
            {
                if (fb->format != PIXFORMAT_JPEG)
                {
                    bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                    esp_camera_fb_return(fb);
                    fb = NULL;
                    if (!jpeg_converted)
                    {
                        Serial.println("JPEG compression failed");
                    }
                }
                else
                {
                    _jpg_buf_len = fb->len;
                    _jpg_buf = fb->buf;
                }
            }
        }
        if (res == ESP_OK)
        {
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if (fb)
        {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        }
        else if (_jpg_buf)
        {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        if (res != ESP_OK)
        {
            break;
        }
        // Serial.printf("MJPG: %uB\n",(uint32_t)(_jpg_buf_len));
    }
    return res;
}
//命令解碼程序
static esp_err_t cmd_handler(httpd_req_t *req)
{
    char *buf;
    size_t buf_len;
    char variable[32] = {
        0,
    };

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1)
    {
        buf = (char *)malloc(buf_len);
        if (!buf)
        {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            if (httpd_query_key_value(buf, "go", variable, sizeof(variable)) == ESP_OK)
            {
            }
            else
            {
                free(buf);
                httpd_resp_send_404(req);
                return ESP_FAIL;
            }
        }
        else
        {
            free(buf);
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
        free(buf);
    }
    else
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    sensor_t *s = esp_camera_sensor_get();
    int res = 0;

    int speed_value = 0;
    int speed_i;
    int len = strlen(buf);
    Serial.println(buf);
    for (speed_i = 0; speed_i < len; speed_i++)
    {
        if (buf[speed_i] == '&')
            break;
    }
    if (speed_i == len)
    {
        speed_value = 200;
    }
    else
    {
        for (int speed_num = speed_i + 1; speed_num < len; speed_num++)
        {
            speed_value = speed_value * 10 + buf[speed_num] - 48;
        }
    }
    if (!strcmp(variable, "forward"))
    {
        Serial.println("Forward");
        Serial.println(speed_value);
        ledcWrite(0, speed_value); // 通道 0
        ledcWrite(1, 0);           // 通道 1
        ledcWrite(2, speed_value); // 通道 2
        ledcWrite(3, 0);           // 通道 3
    }
    else if (!strcmp(variable, "left"))
    {
        Serial.println("Left");
        Serial.println(speed_value);
        ledcWrite(0, speed_value); // 通道 0
        ledcWrite(1, 0);           // 通道 1
        ledcWrite(2, 0);           // 通道 2
        ledcWrite(3, speed_value); // 通道 3
    }
    else if (!strcmp(variable, "right"))
    {
        Serial.println("Right");
        Serial.println(speed_value);
        ledcWrite(0, 0);           // 通道 0
        ledcWrite(1, speed_value); // 通道 1
        ledcWrite(2, speed_value); // 通道 2
        ledcWrite(3, 0);           // 通道 3
    }
    else if (!strcmp(variable, "backward"))
    {
        Serial.println("Backward");
        Serial.println(speed_value);
        ledcWrite(0, 0);           // 通道 0
        ledcWrite(1, speed_value); // 通道 1
        ledcWrite(2, 0);           // 通道 2
        ledcWrite(3, speed_value); // 通道 3
    }
    else if (!strcmp(variable, "stop"))
    {
        Serial.println("Stop");
        Serial.println(speed_value);
        ledcWrite(0, 0); // 通道 0
        ledcWrite(1, 0); // 通道 1
        ledcWrite(2, 0); // 通道 2
        ledcWrite(3, 0); // 通道 3
    }
    else
    {
        res = -1;
    }

    if (res)
    {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}
//網頁分析結束
void startCameraServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL};

    httpd_uri_t cmd_uri = {
        .uri = "/action",
        .method = HTTP_GET,
        .handler = cmd_handler,
        .user_ctx = NULL};
    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL};

    httpd_uri_t capture_uri = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = capture_handler,
        .user_ctx = NULL};

    if (httpd_start(&camera_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
    }
    config.server_port += 1;
    config.ctrl_port += 1;
    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}

void setup()
{
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable brownout detector
    //馬達
    pinMode(MotorA1, OUTPUT); // 設定接腳為輸出屬性
    pinMode(MotorA2, OUTPUT);
    pinMode(MotorA3, OUTPUT); // 設定接腳為輸出屬性
    pinMode(MotorA4, OUTPUT);
    // pinMode(DRV_E, OUTPUT);
    ledcSetup(0, 5000, 8);     // 設定 LED PWM 功能，通道 0，頻率 5000Hz， 解析度8位元
    ledcAttachPin(MotorA1, 0); // 將 PWM 通道 0 連接至 GPIO 接腳 32
    ledcSetup(1, 5000, 8);     // 設定 LED PWM 功能，通道 1，頻率 5000Hz， 解析度 8 位元
    ledcAttachPin(MotorA2, 1); // 將 PWM 通道 1 連接至 GPIO 接腳 33
    ledcSetup(2, 5000, 8);     // 設定 LED PWM 功能，通道 2，頻率 5000Hz， 解析度8位元
    ledcAttachPin(MotorA3, 2); // 將 PWM 通道 2 連接至 GPIO 接腳 25
    ledcSetup(3, 5000, 8);     // 設定 LED PWM 功能，通道 3，頻率 5000Hz， 解析度 8 位元
    ledcAttachPin(MotorA4, 3); // 將 PWM 通道 3 連接至 GPIO 接腳 26
    // digitalWrite(DRV_E,1);
    //
    Serial.begin(115200);
    Serial.setDebugOutput(false);
    Serial.println("123");
    //設定鏡頭ESP32CAM
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    // UXGA(1600x1200), 9->SXGA(1280x1024), 8->XGA(1024x768) ,7->SVGA(800x600),
    // 6->VGA(640x480), 5 selected=selected->CIF(400x296), 4->QVGA(320x240),
    // 3->HQVGA(240x176), 0->QQVGA(160x120), 11->QXGA(2048x1564 for OV3660)
    if (psramFound())
    {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 10;
        config.fb_count = 2;
    }
    else
    {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }
    /*if(psramFound()){
      config.frame_size = FRAMESIZE_QQVGA;
      config.jpeg_quality = 10;
      config.fb_count = 1;
      Serial.println("PSRAM");
    } else {
      config.frame_size = FRAMESIZE_QQVGA;
      config.jpeg_quality = 12;
      config.fb_count = 1;
    }*/

    // Camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("Camera init failed with error 0x%x", err);
        return;
    }
    // ssd1306
    pinMode(I2C_SDA, INPUT_PULLUP);
    pinMode(I2C_CLK, INPUT_PULLUP);
    Wire.begin(I2C_SDA, I2C_CLK);
    display = Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
    {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;)
            ; // Don't proceed, loop forever
    }
    Serial.println(F("SSD1306 allocation OK"));
    display.clearDisplay();  // 清除緩衝區資料
    display.setTextSize(1);  // 設定字型大小為 1
    display.setCursor(0, 0); // 設定文字起始位置為(0,0)
    display.setTextColor(WHITE, BLACK);
    // WiFi
    /*WiFi.mode(WIFI_AP_STA);  //其他模式 WiFi.mode(WIFI_AP); WiFi.mode(WIFI_STA);

    //指定Client端靜態IP
    //WiFi.config(IPAddress(192, 168, 201, 100), IPAddress(192, 168, 201, 2), IPAddress(255, 255, 255, 0));

    for (int i=0;i<2;i++) {
      WiFi.begin(ssid, password);    //執行網路連線
      delay(1000);
      Serial.println("");
      Serial.print("Connecting to ");
      Serial.println(ssid);
      long int StartTime=millis();
      while (WiFi.status() != WL_CONNECTED) {
          delay(500);
          if ((StartTime+5000) < millis()) break;    //等待10秒連線
      }

      if (WiFi.status() == WL_CONNECTED) {    //若連線成功
        WiFi.softAP((WiFi.localIP().toString()+"_"+(String)apssid).c_str(), appassword);   //設定SSID顯示客戶端IP
        Serial.println("");
        Serial.println("STAIP address: ");
        Serial.println(WiFi.localIP());
        Serial.println("");
        display.println(WiFi.localIP());
        display.display();
        for (int i=0;i<5;i++) {   //若連上WIFI設定閃光燈快速閃爍
          ledcWrite(4,10);
          delay(200);
          ledcWrite(4,0);
          delay(200);
        }
        break;
      }
    }
    if (WiFi.status() != WL_CONNECTED) {    //若連線失敗
      WiFi.softAP((WiFi.softAPIP().toString()+"_"+(String)apssid).c_str(), appassword);
      for (int i=0;i<2;i++) {    //若連不上WIFI設定閃光燈慢速閃爍
        ledcWrite(4,10);
        delay(1000);
        ledcWrite(4,0);
        delay(1000);
      }
    } */
    //指定AP端IP
    // WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP(apssid, appassword);
    Serial.println("");
    Serial.println("APIP address: ");
    Serial.println(WiFi.softAPIP());
    Serial.println("");
    display.println(WiFi.softAPIP());
    display.display();
    display.println("Start  ");
    display.display();
    startCameraServer();

    //設定閃光燈為低電位
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);
}
void loop()
{
}