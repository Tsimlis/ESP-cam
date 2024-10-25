#include "esp_camera.h"
#include "time.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include <ArduinoJson.h>
#include "fb_gfx.h"
#include "soc/soc.h"           // disable brownout problems
#include "soc/rtc_cntl_reg.h"  // disable brownout problems
#include "esp_http_server.h"
#include <ESP32Servo.h>

#include <StringArray.h>
#include <SPIFFS.h>
#include <FS.h>
#include "SD_MMC.h"

// Replace with your network credentials
const char *ssid = "Tsim";
const char *password = "88888888";

char *apssid = "ESP32-CAM";
char *appassword = "12345678";

// IPAddress ip(192, 168, 1, 200);
// IPAddress gateway(192, 168, 1, 1);
// IPAddress subnet(255, 255, 255, 0);

#include <TridentTD_LineNotify.h>
#define LINE_TOKEN "yPQPy8Rdd0V6lskeGJMqD49mKfVhk3Pvkmv24cFKBuW"
#define PART_BOUNDARY "123456789000000000000987654321"

bool Line = false;
bool Save = false;
bool Sleep = false;

/////////////////////////////บันทึกวีดีโอ

bool loop_s = true;
String hour_s, date_s; // วัน เวลาในการสร้างชื่อไฟล์
int hour, date;
String dateDelete = "";

int Last_frame = 0, frameRate = 10; // เฟรมต่อวินาที
int delayTime = 1000 / frameRate; // คำนวณหน่วงเวลา (ms) เพื่อให้ได้ 10 FPS

// Current date and time tracking
String currentFolder = "";
String currentFileName = "";
String record_msg = "";
unsigned long lastRecordTime = 0;
const unsigned long recordingInterval = 3600000;  // 1 hour in milliseconds

// Function to start recording video
File videoFile;

///////////////////////////////
#define FLASH_GPIO_NUM 4 // พินที่เชื่อมต่อกับไฟแฟลช

// OV2640 camera module pins (CAMERA_MODEL_AI_THINKER)
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

#define SERVO_1 15
#define SERVO_2 14
// #define SERVO_STEP 5
// Servo servoN1;
// Servo servoN2;
Servo servo1;
Servo servo2;

int servo1Pos = 0;
int servo2Pos = 0;

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 25200;
const int daylightOffset_sec = 3600;

struct tm timeinfo;
String currentTime = "";

void printLocalTime() {
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  date_s = String(timeinfo.tm_year + 1900) + "_" + String(timeinfo.tm_mon + 1) + "_" + String(timeinfo.tm_mday);
  hour_s = String(timeinfo.tm_hour -1) + "_" + String(timeinfo.tm_min) + "_" + String(timeinfo.tm_sec);

  currentTime = String(timeinfo.tm_year + 1900) + "/" + String(timeinfo.tm_mon + 1) + "/" + String(timeinfo.tm_mday) + " ," + String(timeinfo.tm_hour -1) + ":" + String(timeinfo.tm_min) + ":" + String(timeinfo.tm_sec);
}

static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<html>
  <head>
      <title>ESP32-CAM Robot</title>
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <style>
        body { font-family: Arial; text-align: center; margin:0px auto; padding-top: 30px; background-color: lightslategrey;}
        table { margin-left: auto; margin-right: auto; }
        td { padding: 8px; }
        .button {
          background-color: #2f4468;
          border: none;
          color: white;
          padding: 10px 20px;
          text-align: center;
          text-decoration: none;
          display: inline-block;
          font-size: 18px;
          margin: 10px 5px;
          cursor: pointer;
          -webkit-touch-callout: none;
          -webkit-user-select: none;
          -khtml-user-select: none;
          -moz-user-select: none;
          -ms-user-select: none;
          user-select: none;
          -webkit-tap-highlight-color: rgba(0,0,0,0);
        }
        img {  width: auto; max-width: 100%; height: auto; }
        .btn {
          margin: 5px 10px;
          padding: 5px;
        }
      </style>
  </head>
  <body>
      <h1>ESP32-CAM</h1>
      <img src="" id="photo" style='rotate:180deg;'>
      <p id="data"></p>

      <h3>
        <button class="btn" onclick="FileList('title')">List Files</button>
        <button class="btn" onClick="toggleCheckbox('Line');" ontouchstart="toggleCheckbox('Line');">Line Notify</button>
        <button class="btn" onClick="toggleCheckbox('Restart');" ontouchstart="toggleCheckbox('Restart');"> RESTART </button>
        <button class="btn" onClick="toggleCheckbox('Sleep');" ontouchstart="toggleCheckbox('Sleep');"> Sleep </button>
        <button class="btn" onmousedown="toggleCheckbox('ON')" onmouseup="toggleCheckbox('OFF')"
                ontouchstart="toggleCheckbox('ON');" ontouchend="toggleCheckbox('OFF');"> Light </button>
      </h3>
      <table>
        <tr></tr>
        <tr><td colspan="3" align="center"><button class="button" onClick="toggleCheckbox('up');" ontouchstart="toggleCheckbox('up');">Up</button></td></tr>
        <tr>
          <td align="center"><button class="button" onClick="toggleCheckbox('left');" ontouchstart="toggleCheckbox('left');">Left</button></td>
          <td align="center"><button class="button" onClick="toggleCheckbox('center');" ontouchstart="toggleCheckbox('center');">Center</button></td>
          <td align="center"><button class="button" onClick="toggleCheckbox('right');" ontouchstart="toggleCheckbox('right');">Right</button></td>
        </tr>
        <tr><td colspan="3" align="center"><button class="button" onClick="toggleCheckbox('down');" ontouchstart="toggleCheckbox('down');">Down</button></td></tr>
      </table>
      <h5>
        <label id="total" Style="margin: 10px;"></label>
        <label id="use" Style="margin: 10px;"></label>
        <label id="free" Style="margin: 10px;"></label>
      </h5>
      <p id="msg"></p>

      <div id="title" style="display: none;">
        <div>
          <p id="nameVideo"></p>
          <video id="videoPlayer" width="360" height="240" controls >
            <source id="videoSource" src="" type="video/mp4">
            Your browser does not support the video tag.
          </video>
        </div>
        
        <h3 >SD Card File Viewer</h3>
        <div id="fileList"></div>
      </div>

  </body>

  <script>

    function toggleCheckbox(x) {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/action?go=" + x, true);
      xhr.send();
    }

    window.onload = function() {
      document.getElementById("photo").src = window.location.href.slice(0, -1) + ":81/stream";

      setInterval(() => {
        // Fetch SD card info
        fetch('/get-data').then(response => response.json())
          .then(result => {
              document.getElementById('data').innerText = 'record status: ' + result.data.record_msg;
              document.getElementById('total').innerText = 'Total: ' + result.data.total_SD + ' MB';
              document.getElementById('use').innerText = 'Use: ' + result.data.used_SD + ' MB , ' + result.data.pers_SD + '% ';
              document.getElementById('free').innerText = 'Free: ' + result.data.free_SD + ' MB';
              document.getElementById('msg').innerText = result.data.msg_SD ;
          }).catch(error => console.error('Error:', error));
      }, 5000);
    };

    function FileList(id) {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/list-files", true);
      xhr.onreadystatechange = function() {
        if (xhr.readyState == 4 && xhr.status == 200) {
          document.getElementById("fileList").innerHTML = xhr.responseText;
        }
      };
      xhr.send();

      setTimeout(() => { openfile(id) }, 1000);
    }

    function openfile(folder) {
      var A = document.getElementById(folder);
      if (A.style.display === "none") A.style.display = "block";
      else A.style.display = "none";
    }

    function playVideo(filePath) {
      document.getElementById('nameVideo').innerText = 'File name: ' + filePath ;
      var videoPlayer = document.getElementById("videoPlayer");
      var videoSource = document.getElementById("videoSource");
      videoSource.src = "/video?file=" + filePath;
      videoPlayer.load();
      videoPlayer.play();
    }

    function deleteFile(file) {
      if (confirm(`Are you delete this file: ${file}`)) {
        fetch('/delete?file=' + file, { method: 'DELETE' })
          .then(response => {
            if (response.ok) {
              FileList(`${file.split('/')[0]}`); // Refresh the file list
            } else {
              alert('Failed to delete file');
            }
          }).catch(error => { console.error('Error:', error); });
      }
    }

    function deleteFolder(folder) {
      if (confirm(`Are you delete this folder: ${folder}`)) {
        fetch('/delete-fol?file=' + folder, { method: 'DELETE' })
          .then(response => {
            if (response.ok) {
              FileList(''); // Refresh the folder list
            } else {
              alert('Failed to delete folder');
            }
          }).catch(error => { console.error('Error:', error); });
      }
    }

  </script>

</html>
)rawliteral";

// ส่งหน้า html
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

// ส่งภาพ stream ต่อกันเป็นวีดีโอที่หน้าเว็บ
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char *part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      if (fb->width > 400) {
        if (fb->format != PIXFORMAT_JPEG) {
          bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
          esp_camera_fb_return(fb);
          fb = NULL;
          if (!jpeg_converted) {
            Serial.println("JPEG compression failed");
            res = ESP_FAIL;
          }
        } else {
          _jpg_buf_len = fb->len;
          _jpg_buf = fb->buf;
        }
      }
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) {
      break;
    }
    // Serial.printf("MJPG: %uB\n",(uint32_t)(_jpg_buf_len)); /////////////////
  }
  return res;
}


// รับคำสั่งมาจากหน้าเว็บ
static esp_err_t cmd_handler(httpd_req_t *req) {
  char *buf;
  size_t buf_len;
  char variable[32] = {
    0,
  };

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char *)malloc(buf_len);
    if (!buf) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      if (httpd_query_key_value(buf, "go", variable, sizeof(variable)) == ESP_OK) {
      } else {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
      }
    } else {
      free(buf);
      httpd_resp_send_404(req);
      return ESP_FAIL;
    }
    free(buf);
  } else {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  int res = 0;

  if (!strcmp(variable, "center")) {
    servo2Pos = 90;
    servo1Pos = 90;
    servo1.write(servo1Pos);
    servo2.write(servo2Pos);
    Serial.println(servo1Pos);
    Serial.println("Center");
  } else if (!strcmp(variable, "up")) {
    if (servo1Pos <= 170) {
      servo1Pos += 10;
      servo1.write(servo1Pos);
    }
    Serial.println(servo1Pos);
    Serial.println("Up");
  } else if (!strcmp(variable, "down")) {
    if (servo1Pos >= 10) {
      servo1Pos -= 10;
      servo1.write(servo1Pos);
    }
    Serial.println(servo1Pos);
    Serial.println("Down");
  } else if (!strcmp(variable, "left")) {
    if (servo2Pos <= 170) {
      servo2Pos += 10;
      servo2.write(servo2Pos);
    }
    Serial.print(servo2Pos);
    Serial.println(" : Left");
  } else if (!strcmp(variable, "right")) {
    if (servo2Pos >= 10) {
      servo2Pos -= 10;
      servo2.write(servo2Pos);
    }
    Serial.print(servo2Pos);
    Serial.println(" : Right");
  } else if (!strcmp(variable, "Line")) {
    Serial.println("send Line");
    Line = true;
  } else if (!strcmp(variable, "ON")) {
    Serial.println("Light ON");
    digitalWrite(FLASH_GPIO_NUM, HIGH);
  } else if (!strcmp(variable, "OFF")) {
    Serial.println("Light OFF");
    digitalWrite(FLASH_GPIO_NUM, LOW);
  } else if (!strcmp(variable, "Sleep")) {
    Serial.println("Bound Sleep");
    Sleep = true;
  } else if (!strcmp(variable, "Restart")) {
    Serial.println("Restart System");
    ESP.restart();
  } else {
    res = -1;
  }

  if (res) {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

// ฟังก์ชันที่จัดการ GET request ของการส่งค่าไปหน้าเว็บ
esp_err_t get_handler(httpd_req_t *req) {
    // ส่งข้อมูลกลับไปยัง client (หน้าเว็บ)
    String msg_SD = "";
    uint64_t totalBytes = SD_MMC.totalBytes();
    uint64_t usedBytes = SD_MMC.usedBytes();
    uint64_t freeBytes = totalBytes - usedBytes;
    uint8_t pers = (100.0 / totalBytes) * usedBytes;

    if (pers > 90) msg_SD = "Please delete file management SD Card.";

    StaticJsonDocument<200> doc;

    JsonObject data = doc.createNestedObject("data");
    data["record_msg"] = String(record_msg);
    data["total_SD"] = String(totalBytes / (1024 * 1024));
    data["used_SD"] = String(usedBytes / (1024 * 1024));
    data["free_SD"] = String(freeBytes / (1024 * 1024));
    data["pers_SD"] = String(pers);
    data["msg_SD"] = msg_SD;

    String response;
    serializeJson(doc, response);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response.c_str(), response.length());
    return ESP_OK;
}


////////////////////////////////////
// Function to list files in the SD card
static esp_err_t list_folders_handler(httpd_req_t *req) {
  String response = "<ol>";

  // Open root directory
  File root = SD_MMC.open("/");
  if (!root) {
    Serial.println("Failed to open directory");
    return ESP_FAIL;
  }

  // Iterate through all directories (dates)
  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      if (String(file.name()) != "System Volume Information") {
        response += "<li style='color: blue; cursor: pointer; margin: 20px; text-align: left;'>";
        response += "<a onclick=\"openfile('"+ String(file.name()) +"')\" style='text-decoration: none;'>" + String(file.name()) + "</a>";
        response += "<button onclick=\"deleteFolder('"+ String(file.name()) +"')\" style='color: green; margin: 5px 40px; '>Delete</button>";
        response += "<ul id='" + String(file.name()) + "' style='display: none;'>";
        File subfile = file.openNextFile();
        while (subfile) {
          String filePath = String(file.name()) + "/" + String(subfile.name());
          response += "<li><a href='#' onclick=\"playVideo('" + filePath + "')\">" + String(subfile.name()) + "</a>";
          response += "<button onclick=\"deleteFile('"+ filePath +"')\" style='color: red; margin: 5px 40px;'>Delete</button></li>";
          subfile = file.openNextFile();
        }
        response += "</ul></li>";
      }
    }
    file = root.openNextFile();
  }
  response += "</ol>";

  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, response.c_str(), response.length());

  return ESP_OK;
}

// play video on Webserver
static esp_err_t playVideo_Handler(httpd_req_t *req) {
  String filepath = "/";
  if (req->uri && strlen(req->uri) > 12) {
      filepath += String(req->uri + 12);
  }
  Serial.println(filepath);

  File file = SD_MMC.open(filepath.c_str());
  if (!file) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "video/mp4");
  char buffer[1024];
  size_t chunk_size;

  while ((chunk_size = file.read((uint8_t *)buffer, sizeof(buffer))) > 0) {
    httpd_resp_send_chunk(req, buffer, chunk_size);
    // Serial.println(buffer);  // ส่งไปครั้งละกี่ตัว
    // Serial.println(chunk_size);  //ข้อมูลวีดีโอที่ส่งไปให้หน้าเว็บ
  }

  file.close();
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}


// ลบไฟล์วีดีโอเฉพาะที่กำหนด
esp_err_t deleteFileHandler(httpd_req_t *req) {
  String filePath = "/";
  if (req->uri && strlen(req->uri) > 13) {
    filePath += String(req->uri + 13); // Skip "/delete" part
  }

  if (SD_MMC.remove(filePath.c_str())) {
    httpd_resp_send(req, "File deleted successfully", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  } else {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
}
// ลบทั้งโฟลเดอร์
esp_err_t deleteFolderHandler(httpd_req_t *req) {
  String folderPath = "/";
  if (req->uri && strlen(req->uri) > 17) {
    folderPath += String(req->uri + 17); // Skip "/delete" part
  }

  if (SD_MMC.open(folderPath.c_str())) {
    removeDirectory(folderPath); // ฟังกชันลบข้อมุลทั้งหมดในโฟลเดอร์
    httpd_resp_send(req, "Folder deleted successfully", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  } else {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
}

//////////////////////////////////////

// คำสั่งหลัก
void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
  };
  httpd_uri_t cmd_uri = {
    .uri = "/action",
    .method = HTTP_GET,
    .handler = cmd_handler,
    .user_ctx = NULL
  };
  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
  };
  // ส่งค่าไปหน้าเว็บ
  httpd_uri_t get_uri = {
    .uri = "/get-data",
    .method = HTTP_GET,
    .handler = get_handler,
    .user_ctx = NULL
  };
  httpd_uri_t list_folders_uri = {
    .uri = "/list-files",
    .method = HTTP_GET,
    .handler = list_folders_handler,
    .user_ctx = NULL
  };
  httpd_uri_t play_Video_uri = {
    .uri = "/video",
    .method = HTTP_GET,
    .handler = playVideo_Handler,
    .user_ctx = NULL
  };
  httpd_uri_t delete_file_uri = {
    .uri = "/delete",
    .method = HTTP_DELETE,
    .handler = deleteFileHandler,
    .user_ctx = NULL
  };
  httpd_uri_t delete_folder_uri = {
    .uri = "/delete-fol",
    .method = HTTP_DELETE,
    .handler = deleteFolderHandler,
    .user_ctx = NULL
  };

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
    httpd_register_uri_handler(camera_httpd, &get_uri);
    httpd_register_uri_handler(camera_httpd, &play_Video_uri);
    httpd_register_uri_handler(camera_httpd, &list_folders_uri);
    httpd_register_uri_handler(camera_httpd, &delete_file_uri);
    httpd_register_uri_handler(camera_httpd, &delete_folder_uri);
  }
  config.server_port += 1;
  config.ctrl_port += 1;
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

// การเชื่อมต่อไวไฟ
void initWiFi() {
  WiFi.mode(WIFI_AP_STA);
  // WiFi.config(ip, gateway, subnet);
  WiFi.begin(ssid, password);
  
  ledcAttachPin(4, 4); // pin, chanel
  ledcSetup(4, 5000, 8); // chanel, Freq, resolution

  uint8_t i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 10) {  //wait 10 seconds
    Serial.print("_");
    ledcWrite(4, 10);  // ค่าเปิดได้สูงสุดที่ 0-255 เมื่อความละเอียดตังเป็น resolution 8 bit
    delay(500);
    ledcWrite(4, 0);
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connecting to ");
    Serial.println(ssid);
    Serial.print("IP address: http://");
    Serial.println(WiFi.localIP());
    LINE.notify(WiFi.localIP()[0]);

    ledcWrite(4, 10);
    delay(2000);
    ledcWrite(4, 0);
  } else {
    // WiFi.begin(apssid, appassword);
    WiFi.softAP((WiFi.softAPIP().toString() + "_" + (String)apssid).c_str(), appassword);
    Serial.print("Connecting to ");
    Serial.println(apssid);
    Serial.print("Go To APIP address: http://");
    Serial.println(WiFi.softAPIP());
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);  // ปิดการส่งข้อมูลดีบักไปที่ Serial Monitor

  pinMode(FLASH_GPIO_NUM, OUTPUT);

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  //disable brownout detector ปิดระบบตรวจสอบแรงดันไฟฟ้าเมื่อต่ำกว่ากำหนดจะรีสตาร์ทเครื่อง
  servo1.setPeriodHertz(50);                  // standard 50 hz servo
  servo2.setPeriodHertz(50);                  // standard 50 hz servo
  // servoN1.attach(2, 1000, 2000);
  // servoN2.attach(13, 1000, 2000);
  // servo1.attach(SERVO_1, 1000, 200000);
  // servo2.attach(SERVO_2, 1000, 200000);
  servo1.attach(SERVO_1);
  servo2.attach(SERVO_2);

  // Camera config
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

  if (psramFound()) { // มีหน่วยความจำรองรับการสำรองพื้นที่หรือไม่ Ai thinker ไม่มี
    config.frame_size = FRAMESIZE_VGA; // ใช้ความละเอียด VGA (640x480)
    config.jpeg_quality = 15;           // คุณภาพของภาพ
    config.fb_count = 2;                // จำนวนภาพที่สร้างเช่น 1 ภาพไว้แสดง, อีก 1 ภาพใช้ส่งไปบันทึก
  } else {
    config.frame_size = FRAMESIZE_SVGA; // ใช้ความละเอียด SVGA (800x600)
    config.jpeg_quality = 8;           // คุณภาพการบีบอัด ยิ่งมากการบีบอัดไม่มาก น้อยการบีบอัดทำให้คุณภาพน้อย
    config.fb_count = 1;
  }
  // FRAMESIZE_96X96,    // 96x96
  // FRAMESIZE_QQVGA,    // 160x120
  // FRAMESIZE_QCIF,     // 176x144
  // FRAMESIZE_HQVGA,    // 240x176
  // FRAMESIZE_240X240,  // 240x240
  // FRAMESIZE_QVGA,     // 320x240
  // FRAMESIZE_CIF,      // 400x296
  // FRAMESIZE_HVGA,     // 480x320
  // FRAMESIZE_VGA,      // 640x480
  // FRAMESIZE_SVGA,     // 800x600
  // FRAMESIZE_XGA,      // 1024x768
  // FRAMESIZE_HD,       // 1280x720
  // FRAMESIZE_SXGA,     // 1280x1024
  // FRAMESIZE_UXGA,     // 1600x1200

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // Wi-Fi connection
  initWiFi();

  // Start streaming web server
  startCameraServer();

  // LINE notify
  LINE.setToken(LINE_TOKEN);
  LINE.notify("Start Camera");
  camera_fb_t *fb = NULL;
  fb = esp_camera_fb_get();
  LINE.notifyPicture("Click stream " + WiFi.localIP().toString() + ":80", fb->buf, fb->len);
  esp_camera_fb_return(fb);

  //Time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  // Initialize camera and SD card
  initSDCard();
  // Start recording immediately
  printLocalTime();
  while (loop_s) startRecording(date_s, hour_s);
}


////////////////////////////////////////////////////////////////////////////////////////////////////

void loop() {  

  if(WiFi.status() == WL_CONNECTED) printLocalTime();

  // ฟังกชันบันทึกวีดีโอ กำหนดจำนวนเฟรมต่อวินาที
  if (millis() - Last_frame >= delayTime) {
    captureAndWriteVideo();
  }
  
  // ส่งรูปที่ไลน์แจ้งเตือนส่วนตัว
  if (Line) {
    Camera_capture();
    Line = false;
  }

  // หยุดการทำงาน 
  if (Sleep) {
    Sleep = false; 
    // esp_deep_sleep_enable_timer_wakeup(10 * 60 * 1000 * 1000);  // กำหนดให้ตื่นเมื่อครบ 10 นาทีข้างหน้า
    esp_deep_sleep_start();                                    // เริ่มเข้าสู่โหมด Deep Sleep
  }
}


//**************************************************************************************************

// Take a Picture
void Camera_capture() {
  camera_fb_t *fb = NULL;
  // Take Picture with Camera
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }
  Send_line(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

// Send LINE
void Send_line(uint8_t *image_data, size_t image_size) {
  LINE.notifyPicture(currentTime + " , " + WiFi.localIP().toString() + ":80", image_data, image_size);
  // Serial.println("Send Line Notify");
}




// ส่วนของฟังกชันจัดการการบันทึกวีดีโอ
///////////////////////////////////////////////////////////////////

// Function to start SD card
void initSDCard() {
  SD_MMC.begin("/sdcard", true); // ไฟแฟลชจะใช้ร่วมกับช่อง sd ดังนี้สามารถปิดแฟลชที่คำสั่งนี้

  if (!SD_MMC.begin()) {
      Serial.println("Card Mount Failed");
      return;
  }

  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
      Serial.println("No SD card attached");
      return;
  }
}



// Function to capture and write video frames continuously
void captureAndWriteVideo() {
  camera_fb_t *fb = esp_camera_fb_get(); // ดึงภาพมา
  if (!fb) {
      Serial.println("Camera capture failed");
      ESP.restart();
      return;
  }

  if (videoFile) {
      videoFile.write(fb->buf, fb->len); // Write frame to file
      videoFile.flush();                 // ใช้ฟังก์ชัน flush() เพื่อเขียนข้อมูลลง SD card
  }
  esp_camera_fb_return(fb);  // คืนหน่วยความจำที่จองไว้

  // Check if it's time to switch to a new file
  if (millis() - lastRecordTime >= recordingInterval) {
      stopRecording();
      loop_s = true;
      while (loop_s) startRecording(date_s, hour_s);
  }

  Last_frame = millis();
}


// Function to get current date and time as strings
String getDateString() {
  int max = 0;
  File root = SD_MMC.open("/"); // เช็คว่ามีถึงวันที่เท่าไรก็ให้ต่อจากวันนั้นไป เพื่อไม่สับสน
  File file = root.openNextFile();
  bool date95 = true;
  while (file) {
    if (file.isDirectory()) {
      if (String(file.name()) != "System Volume Information") {
        if (date95) {
          dateDelete = String(file.name());
          date95 = false;
        } 
        String num = String(file.name() + 5);
        if (num.length() < 3) max = num.toInt();
      }
    }
    file = root.openNextFile();
  }

  if (date < max) {
    date = max;
  }
  return "date_" + String(date);
}

String getTimeString() {
  hour++;
  if (hour > 24) {
    hour = 1;
    date++;
  }
  return "hour_" + String(hour);
}


void startRecording(String date, String hour) {
    String currentDate = "";
    String currentHour = "";
    // Get current date and hour strings
    // ถ้าเชื่อมเน็ตมีการรับวันที่ เวลา ก็ให้ใส่ชื่อไฟล์เป็นวันที่เวลา
    if (date.length() > 0) {
      currentDate = "date_" + date;
      currentHour = "hour_" + hour;
      // ล้างข้อมูลเพื่อจะไม่ค้าง ทำให้คิดว่ามีวันเวลาอยู่ตลอด
      date_s = "";
      hour_s = "";
    } else {
      currentHour = getTimeString();
      currentDate = getDateString();
    }

    deleteFolder95();

    String folderPath = "/" + currentDate;
    if (!SD_MMC.exists(folderPath.c_str())) { // เช็คว่ามีโฟลเดอร์นี้หรือไม่
      SD_MMC.mkdir(folderPath.c_str()); 
    }
    currentFileName = folderPath + "/" + currentHour + ".mp4";
    if (SD_MMC.exists(currentFileName.c_str())) return; // เช็คว่ามีไฟล์นี้หรือไม่
      
    Serial.printf("Starting recording: %s\n", currentFileName.c_str());

    // Open the file to write video data
    videoFile = SD_MMC.open(currentFileName.c_str(), FILE_WRITE);
    if (!videoFile) {
      record_msg = "Failed to open file";
      Serial.println("Failed to open file in writing mode");
      ESP.restart(); // restart
    } else {
      record_msg = "SDcard => " + String(currentFileName);
      Serial.printf("File opened: %s\n", currentFileName.c_str());
    }

    loop_s = false;
    lastRecordTime = millis();
}

// Function to stop recording video
void stopRecording() {
    if (videoFile) {
        videoFile.close();
        Serial.printf("File saved: %s\n", currentFileName.c_str());
    }
}
// ลบข้อมูลหากเกิน 95 เปอร์เซ็นต์ของพื้นที่
void deleteFolder95(){
  String fol95 = "/" + dateDelete;
  uint64_t totalBytes = SD_MMC.totalBytes();
  uint64_t usedBytes = SD_MMC.usedBytes();
  uint8_t pers = (100.0 / totalBytes) * usedBytes;
  
  if (pers > 95.0) {
    removeDirectory(fol95);
  }
}

// ฟังกชันลบข้อมูลทั้งโฟลเดอร์
void removeDirectory(const String& path) {
    File dir = SD_MMC.open(path);
    if (!dir) {
        Serial.println("Failed to open directory delete");
        return;
    }

    File file = dir.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            removeDirectory(path + "/" +file.name()); // Recursive call for subdirectories
        } else {
            if (SD_MMC.remove(path + "/" + file.name())) Serial.println("Deleted file: " + String(file.name()));
            else Serial.println("Failed to delete file: " + String(file.name()));
        }
        file = dir.openNextFile();
    }
    dir.close();
    if (SD_MMC.rmdir(path)) Serial.println("Deleted directory: " + path);
    else Serial.println("Failed to delete directory: " + path);
}