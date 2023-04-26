/*
	ESP8266WiFiMonitor: ESP8266实现一个简单的wifi信号监测工具
	(A simple WiFi signal monitoring tool achieved by ESP8266)
    Copyright (C) 2023  LiHangBing

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

github: https://github.com/LiHangBing
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <time.h>
extern "C" {
  #include "user_interface.h"
}


//一些HTML模板
const char HTTP_HEADER[] PROGMEM          = "<!DOCTYPE html><html lang=\"zh-cn\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/><title>{v}</title>";
const char HTTP_STYLE[] PROGMEM           = "<style>.c{text-align: center;} div,input{padding:5px;font-size:1em;} input{width:95%;} body{text-align: center;font-family:verdana;} button{border:0;border-radius:0.3rem;background-color:#1fa3ec;color:#fff;line-height:2.4rem;font-size:1.2rem;width:100%;} .q{float: right;width: 64px;text-align: right;} .l{background: url(\"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAMAAABEpIrGAAAALVBMVEX///8EBwfBwsLw8PAzNjaCg4NTVVUjJiZDRUUUFxdiZGSho6OSk5Pg4eFydHTCjaf3AAAAZElEQVQ4je2NSw7AIAhEBamKn97/uMXEGBvozkWb9C2Zx4xzWykBhFAeYp9gkLyZE0zIMno9n4g19hmdY39scwqVkOXaxph0ZCXQcqxSpgQpONa59wkRDOL93eAXvimwlbPbwwVAegLS1HGfZAAAAABJRU5ErkJggg==\") no-repeat left center;background-size: 1em;}</style>";
const char HTTP_HEADER_END[] PROGMEM      = "</head><body><div style='text-align:left;display:inline-block;min-width:260px;'>";
const char HTTP_ITEM[] PROGMEM            = "<div><a>{v}</a>&nbsp;<a>{b}</a>&nbsp;<span class='q {i}'>{r}dB</span></div>";
const char HTTP_END[] PROGMEM             = "</div></body></html>";


//Sta网络，同步时间（可选）
#define STASSID "XXXXXXXX"
#define STAPSWD "XXXXXXXX"
#define TIMEZONE 8			//时区（东八）

String SSID;
std::unique_ptr<DNSServer>        dnsServer;	//DNS服务
std::unique_ptr<ESP8266WebServer> server;		//Web服务
int numsWifi;           //当前扫描到的wifi数量
#define MAXWIFIS 10     //最大wifi保存数量
std::vector<String> wifiSSID;          //SSID数组
std::vector<String> wifiBSSID;          //BSSID数组
std::vector<int> wifiRSSI;          //强度数组
std::vector<bool> wifiPSWD;          //是否加密数组

//回调函数
void handleRoot();
void handleNotFound();

//一些流程函数
void scanWifi();				//扫描wifi
bool haveUpdate = false;       //wifi是否有更新
void updateFlies();				//保存到文件

//文件相关  从littleFS参考而来
void listDir(const String& dir);
void readFile(const String& path);
void writeFile(const String& path, const String& message);
void appendFile(const String& path, const String& message);
void renameFile(const String& path1, const String& path2);
void deleteFile(const String& path);
struct tm timNow;           //当前时间
int flieIdx = 0;            //文件名编号
int timeIdx = 0;            //时间帧编号
bool handleFileRead(String path);       //读取文件并传送给客户端

unsigned long time_next;                //下一次扫描时间
#define scan_intv_s  5                  //扫描间隔

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  //sta连接网络并校准时间
  WiFi.mode(WIFI_STA);
  Serial.print("\nConnecting to ");
  WiFi.begin(STASSID, STAPSWD);
  int connectTime = 0;
  while (WiFi.status() != WL_CONNECTED && connectTime++ < 10) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println();
  if(connectTime <= 10){     //连接成功 //联网同步时间
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println("Contacting Time Server");
    configTime(3600 * TIMEZONE, 0, "time.nist.gov", "time.windows.com");
    timNow ={0};
    if(!getLocalTime(&timNow, 5000)){
      Serial.println("Contacting Time Server faild");
      goto selfTim;      //操作超时5s
    }
    Serial.printf("Now is : %d-%02d-%02d %02d:%02d:%02d\n", (timNow.tm_year) + 1900, (timNow.tm_mon) + 1, timNow.tm_mday, timNow.tm_hour, timNow.tm_min, timNow.tm_sec);
  }
  else{     //使用自定义时间设置
    Serial.println("WiFi connected faild");
selfTim:
    Serial.println("Using Self Time");
    timNow ={0};
    timNow.tm_year = 2023-1900; timNow.tm_mon = 4-1; timNow.tm_mday=1; timNow.tm_hour=0; timNow.tm_min = 0;
    time_t timeSinceEpoch = mktime(&timNow);  struct timeval now = { .tv_sec = timeSinceEpoch };
    settimeofday(&now, NULL);
    Serial.printf("Now is : %d-%02d-%02d %02d:%02d:%02d\n", (timNow.tm_year) + 1900, (timNow.tm_mon) + 1, timNow.tm_mday, timNow.tm_hour, timNow.tm_min, timNow.tm_sec);
  }

  SSID = "WiFiMonitor" + String(ESP.getChipId());
  WiFi.mode(WIFI_AP);				//断开sta并进入AP
  Serial.println(F("Configuring access point... "));
  Serial.println(SSID);
  WiFi.softAP(SSID);        //开启AP
  delay(500); // Without delay I've seen the IP address blank
  Serial.println(F("AP IP address: "));
  Serial.println(WiFi.softAPIP());

  //SPIFFS.format();
  Serial.println("Mount SPIFFS");
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS mount failed");
    panic();
  }
  FSInfo info;
  if(SPIFFS.info(info))
  {
    Serial.println("SPIFFS info");
    Serial.printf("\ttotalBytes=%lu\n",info.totalBytes);
    Serial.printf("\tusedBytes=%lu\n",info.usedBytes);
    Serial.printf("\tpageSize=%lu\n",info.pageSize);
    Serial.printf("\tmaxOpenFiles=%lu\n",info.maxOpenFiles);
    Serial.printf("\tmaxPathLength=%lu\n",info.maxPathLength);
  }
  else
    Serial.println("SPIFFS info failed");

  dnsServer.reset(new DNSServer());			//给智能指针重新分配
  server.reset(new ESP8266WebServer(80));
  // Setup the DNS server redirecting all the domains to the apIP
  dnsServer->setErrorReplyCode(DNSReplyCode::NoError);	//DNS错误码
  //启动DNS服务（只支持一个域名） 端口号、映射的域名、映射的IP   *代表映射所有域名，使用ESP8266默认IP(192.168.4.1)
  dnsServer->start(53, "*", WiFi.softAPIP());
  //设置HTTP请求回调函数，每当来一个HTTP请求时回调对应函数
  server->on(String(F("/")).c_str(), handleRoot);
  server->onNotFound(handleNotFound);
  server->begin(); // Web server start		//启动Web服务
  Serial.println(F("HTTP server started"));
  

  time_next = millis() + scan_intv_s*1000;
  scanWifi();             //扫描WiFi并保存到文件
}

void loop() {
  // put your main code here, to run repeatedly:

  //扫描wifi
  if( millis() > time_next)
  {
    time_next = time_next + scan_intv_s*1000;
    scanWifi();     //扫描WiFi并保存到文件
    //Serial.printf("scan timeMs = %d\n", (int)(millis()-time_cur));    //同步扫描要2秒钟

    if(time_next < scan_intv_s*1000)        //溢出处理（跳过这段溢出）
    {
      while(millis() > scan_intv_s*1000)  //溢出前不扫描
      {
        //DNS	处理DNS请求（循环调用）
        dnsServer->processNextRequest();
        //HTTP	处理客户端请求（循环调用）
        server->handleClient();
        //更新文件
        if(haveUpdate)
        {
          updateFlies();
          haveUpdate = false;
        }
        yield();			//留给后面协议栈运行
      }
    }
  }

  //DNS	处理DNS请求（循环调用）
  dnsServer->processNextRequest();
  //HTTP	处理客户端请求（循环调用）
  server->handleClient();

  //更新文件
  if(haveUpdate)
  {
    updateFlies();
    haveUpdate = false;
  }
}


void scanWifi()
{
  //int n = WiFi.scanNetworks();	//扫描可用WIFI网络，返回可用数量，采用同步扫描模式，耗时2秒（太慢了）
  
  //异步扫描模式（回调在Loop后执行）
  WiFi.scanNetworksAsync([](int n){

    int indices[n];
    for (int i = 0; i < n; i++) {
      indices[i] = i;
    }
    //根据信号强度dBm排序,排序好的下标存在数组indices
    std::sort(indices, indices + n, [](const int & a, const int & b) -> bool{return WiFi.RSSI(a) > WiFi.RSSI(b);});

    //更新wifi信息
    if(n > MAXWIFIS) n = MAXWIFIS;
    numsWifi = n;
    wifiSSID.resize(n);
    wifiBSSID.resize(n);
    wifiRSSI.resize(n);
    wifiPSWD.resize(n);
    
    for (int i = 0; i < n; i++) {
      wifiSSID[i] = WiFi.SSID(indices[i]);
      wifiBSSID[i] = WiFi.BSSIDstr(indices[i]);
      wifiRSSI[i] = WiFi.RSSI(indices[i]);
      wifiPSWD[i] = (WiFi.encryptionType(indices[i]) != ENC_TYPE_NONE);
    }
    haveUpdate = true;		//推测这个回调函数是在中断中处理，所以文件写入操作被放置在loop中
  });
}

void updateFlies()
{
  String appendStr;         //用于后面写入文件
  noInterrupts();         //关中断，防止读取信息被打断
  for (int i = 0; i < numsWifi; i++)
      appendStr += wifiSSID[i] + "," + wifiBSSID[i] + "," + wifiRSSI[i] + "," + (wifiPSWD[i] ? "Y" : "N") + "\n";
  interrupts();

  //写入文件
  FSInfo info;
  if(SPIFFS.info(info) && (info.totalBytes - info.usedBytes) >= 500)      //剩余空间>500Bytes
  {
    if(timeIdx > 100)         //一个文件最大放100帧
    { timeIdx = 0; flieIdx++; }
    String fileName = String(flieIdx)+".csv";
    if(timeIdx == 0)      //第一次写入，需要创建文件并写入开头信息
    {
      getLocalTime(&timNow, 10);      //操作超时10ms
      writeFile(fileName, String("creat at:")+ (timNow.tm_year+1900) + "-" + (timNow.tm_mon+1) + '-' + timNow.tm_mday
                                                  + " " + timNow.tm_hour + ":" + timNow.tm_min + ":" + timNow.tm_sec
                                                  + String(" with scan_intv:") + scan_intv_s + "s\n");
      appendFile(fileName, String("SSID,") + "BSSID," + "RSSI/dB," + "encryption\n");
    }
    appendFile(fileName, String(timeIdx) + "\n");
    appendFile(fileName,appendStr);

    timeIdx++;
    //readFile(fileName);         //测试
  }

}

void handleRoot()
{
  Serial.println(F("Handle root"));
  String page = FPSTR(HTTP_HEADER);
  page.replace("{v}", "WiFiMonitor");
  page += FPSTR(HTTP_STYLE);
  page += FPSTR(HTTP_HEADER_END);
  page += String(F("<h1>"));
  page += SSID;
  page += String(F("</h1>"));
  page += String(F("<h2>WiFiMonitor</h2>"));

  //时间信息
  getLocalTime(&timNow, 10);      //操作超时10ms
  String timStr;
  timStr += String("Now is : ") + (timNow.tm_year+1900) + "-" + (timNow.tm_mon+1) + '-' + timNow.tm_mday;
  timStr += String(' ') + timNow.tm_hour + ":" + timNow.tm_min + ":" + timNow.tm_sec;
  page += String(F("<h3>")) + timStr + String(F("</h3>"));

  //添加扫描到的信息
  if(numsWifi==0)
    page += F("<h3>No networks found. Refresh to scan again.</h3>");
  for (int i = 0; i < numsWifi; i++) {
    String item = FPSTR(HTTP_ITEM);	//条目
    
    item.replace("{v}", wifiSSID[i]);	//替换条目内的SSID和信号质量
    item.replace("{b}", wifiBSSID[i]);
    item.replace("{r}", String(wifiRSSI[i]));
    if (wifiPSWD[i]) {
      item.replace("{i}", "l");		//加密，锁的样式.q .l
    } else {
      item.replace("{i}", "");
    }
    page += item;
  }
  page += "<br/>";

  //添加文件信息和连接
  Dir root = SPIFFS.openDir(String());
  while (root.next()) {
    File file = root.openFile("r");
    page += String("<a href=\"") + root.fileName() + "\">" + root.fileName() + "</a>";
    page += "&nbsp;";
    page += String("<a href=\"") + root.fileName() + "\" download=\"" + root.fileName() + "\">Click to download</a>";
    page += "&nbsp;";
    page += String("<a>") + file.size() + "Bytes" + "</a>";
    page += "<br/>";
    file.close();
  }

  page += FPSTR(HTTP_END);
  
  //定义报头，发送HTTP响应（200,OK 内容html 响应体
  server->sendHeader("Content-Length", String(page.length()));
  server->send(200, "text/html", page);
}

//-------------------------------处理404
void handleNotFound() {
  if (handleFileRead(server->uri())) 
    return;
  Serial.println(F("Handle NotFound"));
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server->uri();		//请求的uri
  message += "\nMethod: ";
  message += ( server->method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server->args();	//请求附带的参数数量
  message += "\n";

  for ( uint8_t i = 0; i < server->args(); i++ ) {	//请求附带的所有参数
    message += " " + server->argName ( i ) + ": " + server->arg ( i ) + "\n";
  }
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
  server->sendHeader("Content-Length", String(message.length()));
  server->send ( 404, "text/plain", message );
}

void listDir(const String& dir) {
  Serial.printf("Listing directory: %s\n", dir.c_str());

  Dir root = SPIFFS.openDir(dir);

  while (root.next()) {
    File file = root.openFile("r");
    Serial.printf("  FILE: %s  SIZE: %lu\n",root.fileName().c_str(),file.size());
    file.close();
  }
}

void readFile(const String& path) {
  Serial.printf("Reading file: %s\n", path.c_str());

  File file = SPIFFS.open(path, "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: \n");
  while (file.available()) { Serial.write(file.read()); }
  file.close();
}

void writeFile(const String& path, const String& message) {
  Serial.printf("Writing file: %s\n", path.c_str());

  File file = SPIFFS.open(path, "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message.c_str())) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

void appendFile(const String& path, const String& message) {
  Serial.printf("Appending to file: %s\n", path.c_str());

  File file = SPIFFS.open(path, "a");
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (file.print(message.c_str())) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}

void renameFile(const String& path1, const String& path2) {
  Serial.printf("Renaming file %s to %s\n", path1.c_str(), path2.c_str());
  if (SPIFFS.rename(path1, path2)) {
    Serial.println("File renamed");
  } else {
    Serial.println("Rename failed");
  }
}

void deleteFile(const String& path) {
  Serial.printf("Deleting file: %s\n", path.c_str());
  if (SPIFFS.remove(path)) {
    Serial.println("File deleted");
  } else {
    Serial.println("Delete failed");
  }
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  if(path.startsWith("/"))  path.remove(0,1);
  Serial.printf("handleFileRead file: %s\n", path.c_str());
  if (SPIFFS.exists(path)) { // If the file exists
    File file = SPIFFS.open(path, "r"); // Open it
    if (!file) {
      Serial.println("Failed to open file for sending");
      return false;
    }
    server->streamFile(file, "text/plain"); // And send it to the client
    file.close(); // Then close the file again
    Serial.printf("Send file %s\n",path.c_str());
    return true;
  }
  return false; // If the file doesn't exist, return false
}
