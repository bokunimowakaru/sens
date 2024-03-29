/*******************************************************************************
IoT Sensor Core for ESP32

https://github.com/bokunimowakaru/sens

										   Copyright (c) 2019 Wataru KUNINO
*******************************************************************************/

// 推奨コンパイルオプション
/*
	Flash Mode		: QIO
	CPU Frequency	: 80MHz(WiFi/BT)
	PSRAM			: Disabled
	Flash Size		: 4MB
	Partition Scheme: Huge APP 3MB No OTA 1MB SPIFFS
	Flash Frequency : 80Mz	<- 必須
	Upload Speed	: 921600 (通常) / 115200 (CH340用)
	Core Debug Level: なし
*/

#define  VERSION "1.08"							// バージョン表示

/*******************************************************************************
Ver. 1.08
 - Si7021 のドライバで HTU21 が読めない点を暫定修正

Ver. 1.07
 - Ver1.05および1.06でDeepSleep復帰後にUDP送信しないデグレードを解消
 - 権利情報表示および bokunimo.net へのリンクの追加

Ver. 1.06
 - Windows版 インストール説明書の作成 (Espressif DOWNLOAD TOOL使用)
 - デバイス発見用 ブロードキャスト ident_0 送信

Ver. 1.05
 - 湿度センサDHT11の個体ばらつきによって起動が不安定ものがあったので改善
 - 30分を超えるスリープが設定できないバグを修正（uint変換に誤り）

Ver. 1.04
 - SSIDにMAC下4桁を追加する機能(教室などで複数のIoT SensorCoreを利用する場合を想定)

ToDo
 - 初期設定ウィザード
 - ASONG HR202L 対応
 - Language 設定

*******************************************************************************/

#include <SPIFFS.h>
#include <WiFi.h>								// ESP32用WiFiライブラリ
#include <WiFiUdp.h>							// UDP通信を行うライブラリ
#include <ESPmDNS.h>							// ESP32用マルチキャストDNS
#include <esp_wps.h>							// ESP32用Wi-Fi WPSライブラリ
#include "Ambient.h"							// Ambient接続用 ライブラリ

#define  FILENAME "/wifiset.txt" 				// Wi-Fi 設定用ファイル名
#define  Ambient_Data_Num 1						// Ambient データ番号d(n)

// ユーザ設定
RTC_DATA_ATTR char SSID_AP[16]="iot-core-esp32";	// 本機のSSID 15文字まで
RTC_DATA_ATTR char PASS_AP[16]="password";			// 本機のPASS 15文字まで
RTC_DATA_ATTR char 		SSID_STA[17] = "";		// STAモードのSSID(お手持ちのAPのSSID)
RTC_DATA_ATTR char 		PASS_STA[65] = "";		// STAモードのPASS(お手持ちのAPのPASS)
RTC_DATA_ATTR boolean	SSID_MAC	= false;	// SSIDにMACを付与
RTC_DATA_ATTR boolean	WPS_STA		= false;	// STAモードのWPS指示
RTC_DATA_ATTR byte 		BOARD_TYPE	= 1;		// 0:AE-ESP, 1:DevKitC, 2:TTGO T-Koala
RTC_DATA_ATTR boolean	MDNS_EN=true;			// MDNS responder
RTC_DATA_ATTR byte 		PIN_LED		= 2;		// GPIO 2(24番ピン)にLEDを接続
RTC_DATA_ATTR byte 		PIN_SW		= 0;		// GPIO 0(25番ピン)にスイッチを接続
RTC_DATA_ATTR byte 		PIN_PIR		= 26;		// GPIO 26に人感センサを接続
RTC_DATA_ATTR byte		PIN_IR_IN	= 26;		// GPIO 26にIRセンサを接続
RTC_DATA_ATTR byte		PIN_IR_OUT	= 2;		// GPIO 2(24番ピン)赤外線LEDの接続ポート
RTC_DATA_ATTR byte 		PIN_VDD		= 25;		// GPIO 25をHIGH出力に設定(不可=0,2,15,12)
RTC_DATA_ATTR byte 		PIN_GND		= 27;		// GPIO 27をLOW出力に設定
RTC_DATA_ATTR byte 		PIN_LUM		= 33;		// GPIO 33に照度センサを接続
RTC_DATA_ATTR byte 		PIN_TEMP	= 33;		// GPIO 33に温度センサを接続
RTC_DATA_ATTR byte 		WIFI_AP_MODE= 1;		// Wi-Fi APモード ※2:STAモード
RTC_DATA_ATTR uint16_t	SLEEP_SEC	= 0;		// スリープ間隔
RTC_DATA_ATTR uint16_t	SEND_INT_SEC= 30;		// 自動送信間隔(非スリープ時)
RTC_DATA_ATTR uint16_t	TIMEOUT		= 12000;	// タイムアウト 12秒
RTC_DATA_ATTR uint16_t	UDP_PORT	= 1024; 	// UDP ポート番号
RTC_DATA_ATTR byte		UDP_MODE	= 1;		// 0:OFF, 1:個々, 2:全値, 3:両方
RTC_DATA_ATTR char		DEVICE[6]	= "esp32";	// デバイス名(5文字)
RTC_DATA_ATTR char 		DEVICE_NUM	= '2';		// デバイス番号
RTC_DATA_ATTR int		AmbientChannelId = 0; 	// チャネル名(整数) 0=無効
RTC_DATA_ATTR char		AmbientWriteKey[17]="0123456789abcdef";	// ライトキー(16文字)

// デバイス有効化
RTC_DATA_ATTR byte		LCD_EN=0;
RTC_DATA_ATTR boolean	NTP_EN=false;
RTC_DATA_ATTR boolean	TEMP_EN=true;
RTC_DATA_ATTR int8_t	TEMP_ADJ=0;
RTC_DATA_ATTR boolean	HALL_EN=false;
RTC_DATA_ATTR byte		ADC_EN=0;
RTC_DATA_ATTR byte		BTN_EN=0;				// 1:ON(L) 2:PingPong
RTC_DATA_ATTR boolean	PIR_EN=false;
RTC_DATA_ATTR byte		IR_IN_EN=0;
RTC_DATA_ATTR byte		IR_OUT_EN=false;
RTC_DATA_ATTR boolean	AD_LUM_EN=false;
RTC_DATA_ATTR byte		AD_TEMP_EN=0;			// 1:LM61, 2:MCP9700
RTC_DATA_ATTR byte		I2C_HUM_EN=0;			// 1:SHT31, 2:Si7021
RTC_DATA_ATTR byte		I2C_ENV_EN=0;			// 1:BME280, 2:BMP280
RTC_DATA_ATTR boolean	I2C_ACCEM_EN=false;
RTC_DATA_ATTR boolean	TIMER_EN=false;

IPAddress IP;									// 本機IPアドレス
byte MAC[6];									// 本機MACアドレス
const IPAddress IP_AP=IPAddress(192,168,254,1);	// AP用IPアドレス
IPAddress IP_STA;								// STA用IPアドレス
IPAddress IP_BC;								// ブロードキャストIPアドレス
Ambient ambient;								// クラウドサーバ Ambient用
WiFiClient ambClient;							// Ambient接続用のTCPクライアント

unsigned long TIME=0;							// 1970年からmillis()＝0までの秒数
unsigned long TIME_NEXT=0;						// 次回の送信時刻(ミリ秒)
boolean TIME_NEXT_b=false;						// 桁あふれフラグ

const String Line = "------------------------";

boolean setupWifiAp(){
	boolean ret;
	Serial.println("Start Wi-Fi Soft AP ----"+ Line);
	
	if( WIFI_AP_MODE == 1 ){					// WiFi_AP 動作時(AP+STA時は待機不要)
		for(int i=0; i < 5000; i++){			// ソフトAPモードの起動を遅らせる
			delay(1);							// (起動直後のSTAからの接続があった場合のHUP対策)
			if(i % 500 == 0 ) Serial.print(".");
		}
		Serial.println();
	}
	for(int tries=0; tries<3 ; tries++){		// 192.168.4.1が割り当てられる対策
		WiFi.softAPConfig(
			IP_AP, 								// AP側の固定IPアドレス
			IP_AP, 								// 本機のゲートウェイアドレス
			IPAddress(255,255,255,0)			// ネットマスク
		);
		for(int i=0;i<500;i++) delay(1);		// 待ち時間が必要(起動後HUP対策)
		char s[22];
		if( SSID_MAC ) sprintf(s,"%s-%02x%02x",SSID_AP,MAC[4],MAC[5]);
		else strcpy(s,SSID_AP);
		ret = WiFi.softAP(s,PASS_AP);		// ソフトウェアAPの起動
		if(ret){
			IPAddress ip = WiFi.softAPIP();
			Serial.print("SoftAP  IP = "); Serial.println(ip);
			if( ip == IP_AP ){
				Serial.println("Software AP started");
				break;
			}
			Serial.println("Retring to start AP...");
			ret=0;
		}
		for(int i=0;i<3000;i++) delay(1);
	}
	if(!ret){
		html_error("Failed to start SoftAP","SoftAP 起動失敗","SoftAP");
		TimerWakeUp_setSleepTime(TIMEOUT / 1000);
		sleep();
	}
	return ret;
}

boolean setupWifiSta(){
	Serial.println("Start Wi-Fi STA --------"+ Line);
	unsigned long start_ms=millis();			// 初期化開始時のタイマー値を保存
	
	if(WPS_STA){
		Serial.println("WPS Commission Started");
		esp_wps_config_t config;
	//	下記の行は ESP32 Ver 1.0.4用。
	//	config.crypto_funcs = &g_wifi_default_wps_crypto_funcs;
		config.wps_type = WPS_TYPE_PBC;			// WPS_TYPE_PBC または WPS_TYPE_PIN
		strcpy(config.factory_info.manufacturer, "BOKUNIMO.NET");
		strcpy(config.factory_info.model_number, "EG.66");
		strcpy(config.factory_info.model_name, "IOT-CORE-ESP32");
		strcpy(config.factory_info.device_name, "IOT SENSOR");
		
		int wps = esp_wifi_wps_enable(&config);	// WPS初期化
		if(wps != ESP_OK){
			html_error("WPS negotiation Failed","WPS 初期化失敗");
			Serial.println("esp_wifi_wps_enable: " + String(wps));
			return false;
		}
		for(int i=0;i<500;i++) delay(1);
		wps = esp_wifi_wps_start(TIMEOUT);		// WPS接続(blocking time,最大120秒)
		if(wps != ESP_OK){
			html_error("AP not found","WPS 動作中 AP なし","WPSｼｯﾊﾟｲ");
			Serial.println("esp_wifi_wps_start: " + String(wps));
			return false;
		}
		
		String ssid;
		String pass;
		while(millis()-start_ms < TIMEOUT){ 	// WPS 有効中
			for(int i=0;i<500;i++) delay(1);	// 待ち時間処理
			digitalWrite(PIN_LED,!digitalRead(PIN_LED));	// LEDの点滅
			Serial.print(".");
			ssid = WiFi.SSID();
			pass = WiFi.psk();
			if(ssid.length()>0 && pass.length()>0){
				Serial.print("!");
				break;
			}
		}
		Serial.println();							// 改行をシリアル出力
		if(ssid.length()>0 && pass.length()>0){
			if( ssid.length()>16 || pass.length()>64){
				html_error("too long WPS SSID","WPS SSID 文字列超過");
			}else{
				ssid.toCharArray(SSID_STA,17);
				pass.toCharArray(PASS_STA,65);
				Serial.println("Stored SSID and PASS to RTC memory.");
			}
		}else{
			html_error("WPS negotiation Failed","WPS 認証失敗");
			esp_wifi_wps_disable();
			return false;
		}
		Serial.print("Station ID = ");
		Serial.println(WiFi.SSID());				// SSIDをシリアル表示
		Serial.print("       psk = ");
		Serial.println(WiFi.psk());					// PASSをシリアル表示
		esp_wifi_wps_disable();
	}
	Serial.println("Wi-Fi STA Started connection");
	for(int i=0;i<10;i++) delay(1);
	WiFi.begin(SSID_STA,PASS_STA);				// 無線LANアクセスポイントへ接続
	start_ms=millis();							// 初期化開始時のタイマー値を保存
	char c;										// 接続状態フラグ
	do{ 										// 接続に成功するまで繰り返し
		digitalWrite(PIN_LED,!digitalRead(PIN_LED));	// LEDの点滅
		c = debug_wl_status(WiFi.status());
		Serial.print(c);
		if(c == '#'){
			start_ms=millis();
			while(c == '#'){
				if(millis()-start_ms > TIMEOUT) break;
			//	WiFi.disconnect();					// WiFiアクセスポイントを切断する
				for(int i=0;i<500;i++)delay(1);		// 待ち時間処理
				WiFi.begin(SSID_STA,PASS_STA);
				for(int i=0;i<1000;i++)delay(1);	// 待ち時間処理
				digitalWrite(PIN_LED,!digitalRead(PIN_LED));	// LEDの点滅
				c = debug_wl_status(WiFi.status());
				Serial.print(c);
			}
		}
		for(int i=0;i<500;i++) delay(1);		// 待ち時間処理
		if(millis()-start_ms>TIMEOUT){			// 待ち時間後の処理
			WiFi.disconnect();					// WiFiアクセスポイントを切断する
			Serial.println();					// 改行をシリアル出力
			html_error("No Internet AP","AP接続失敗");
			return false;
		}
	}while(c != '!');							// WL_CONNECTEDを検出するまで
	Serial.println();							// 改行をシリアル出力
	WPS_STA=false;
	Serial.print("Station IP = ");
	Serial.println(WiFi.localIP());				// IPアドレスをシリアル表示
	Serial.print("      Mask = ");
	Serial.println(WiFi.subnetMask());			// ネットマスクをシリアル表示
	Serial.print("   Gateway = ");
	Serial.println(WiFi.gatewayIP());			// ゲートウェイをシリアル表示
	Serial.println("Station started");
	for(int i=0;i<10;i++) delay(1);
	return true;
}

String sendUdp(const char *device, String &payload){
	if(UDP_PORT > 0 && payload.length() > 0){
		String S = String(device) + "_" + String(DEVICE_NUM) + "," + payload;
		WiFiUDP udp;								// UDP通信用のインスタンスを定義
		udp.beginPacket(IP_BC, UDP_PORT);			// UDP送信先を設定
		udp.println(S);						 		// 送信
		udp.endPacket();							// UDP送信の終了(実際に送信する)
		udp.flush();
		udp.stop();
		Serial.println("udp://" + html_ipAdrToString(IP_BC) +":" + String(UDP_PORT) + " \"" + S + "\"");
		for(int i=0;i<10;i++) delay(1);
		return S;
	} else return "";
}

String sendUdp(String &payload){
	DEVICE[5]='\0';									// 終端
	return sendUdp(DEVICE, payload);
}

boolean sentToAmbient(String &payload){
	if(AmbientChannelId == 0) return false;
	if(WiFi.status() != WL_CONNECTED) return false;
	if(payload.length() == 0) return false;

	ambient.begin(AmbientChannelId, AmbientWriteKey, &ambClient);
	int Sp=0;
	char s[16];
	for(int num = Ambient_Data_Num; num <= 8; num++){
		float val = payload.substring(Sp).toFloat();
		Serial.println("http://ambidata.io/ POST {\"d"
			+ String(num)
			+ "\":"
			+ String(val)
			+ "}"
		);
		dtostrf(val,-15,3,s);
		ambient.set(num,s);
		Sp = payload.indexOf(",", Sp) + 1;
		if( Sp <= 0 ) break;
		if( Sp >= payload.length() ) break;
	}
	boolean ret = ambient.send();
	if( ambClient.available() ) ambClient.stop();
	return ret;
}

String sendSensorValues(){
	Serial.println("Start: send Sensor Values");
	String payload = String(sensors_get());
	if( payload.length() ){
		Serial.println("Done: send UDP to LAN");
		if( sentToAmbient(payload) ){
			Serial.println("Done: send to Ambient");
		}else if( AmbientChannelId > 0 ){
			html_error("Feiled connection, Ambient","Ambientへの接続失敗","Ambient");
		}
	}
	return payload;
}

void setup(){
	esp_efuse_mac_get_default(MAC);
	pinMode(0,INPUT_PULLUP);
	sensors_init();
	Serial.begin(115200);
	Serial.println(Line);
	int wake = TimerWakeUp_init();
	int sw = 1;
	if(BTN_EN > 0){
		pinMode(PIN_SW,INPUT_PULLUP);
		if(wake == 1 || wake == 2){		// RTC_IO, RTC_CNTL
			sensors_btnPush(true);
			sw = 0;
		}
		if(wake == 3 || wake == 4){		// timer, touchpad
			sw = digitalRead(PIN_SW);
		}
	}
	if(IR_IN_EN > 0){
		pinMode(PIN_IR_IN,INPUT_PULLUP);
		pinMode(PIN_GND,OUTPUT);	digitalWrite(PIN_GND,LOW);
		pinMode(PIN_VDD,OUTPUT);	digitalWrite(PIN_VDD,HIGH);
		if(wake == 1 || wake == 2){		// RTC_IO, RTC_CNTL
			if( sw ){
				sw = digitalRead(PIN_IR_IN);
				if( !sw ) sw = !sensors_irRead(true);
			}
		}
	}
	if(PIR_EN){
		pinMode(PIN_PIR,INPUT_PULLUP);
		pinMode(PIN_GND,OUTPUT);	digitalWrite(PIN_GND,LOW);
		pinMode(PIN_VDD,OUTPUT);	digitalWrite(PIN_VDD,HIGH);
		if(wake == 1 || wake == 2){		// RTC_IO, RTC_CNTL
			sensors_pirPush(true);
			sw = 0;
		}
		if(wake == 3 || wake == 4){		// timer, touchpad
			if( sw ) sw = !digitalRead(PIN_PIR);
		}
	}
	if( (BTN_EN || IR_IN_EN || PIR_EN) && sw ) sleep();
	
	Serial.println("-------- IoT Sensor Core ESP32 Ver." + String(VERSION) + " by Wataru KUNINO --------");
		
//	/* SPIFFS //////////////////////////
	if( ( !SPIFFS.begin() || !digitalRead(0) ) && wake == 0 ){	// ファイルシステムSPIFFSの開始
		Serial.println("Formating SPIFFS.");
		SPIFFS.format();
		SPIFFS.begin();	// エラー時にSPIFFSを初期化
	}
	
	if( wake == 0 ){
		/*
		File root = SPIFFS.open("/");
		if (!root) {
			Serial.println("Failed to open directory");
		}else if (!root.isDirectory()) {
			Serial.println("Not a directory");
		}else{
			File file = root.openNextFile();
			while (file) {
				if (file.isDirectory()) {
					Serial.print("  DIR : ");
					Serial.println(file.name());
				} else {
					Serial.print("  FILE: ");
					Serial.print(file.name());
					Serial.print("  SIZE: ");
					Serial.println(file.size());
				}
				file = root.openNextFile();
			}
			file.close();
		}
		root.close();
		*/

		File file = SPIFFS.open(FILENAME,"r");	// ファイルを開く
		if(file){
			int size = 1 + 16 + 16 + 17 + 65 + 1 + 1 + 1;
			char d[(1 + 16 + 16 + 17 + 65 + 1 + 1 + 1) + 1];
			int end = 0;
			memset(d,0,size + 1);
			if(file.available()){
				Serial.println("loading settings");
				file.read((byte *)d,size);
			/*	if(
					d[0] >= '0' && d[0] <= '2'&&
				//	strlen(&d[1])>0 &&
				//	strlen(&d[1+16])>0 &&
				//	strlen(&d[1+16+16])>0 &&
				//	strlen(&d[1+16+16+17])>0 &&
				//	strlen(&d[1+16+16+17+65])>0 &&
				//	d[1+16+16+17+65+1] >= '1' && d[1+16+16+17+65+1] <= '3'
				){
			*/
					BOARD_TYPE= (byte)d[end] - '0';			// Serial.printf("BOARD_TYPE  =%d\n",BOARD_TYPE);
					end++;
					strncpy(SSID_AP,d+end,16); end += 16;	// Serial.printf("SSID_AP     =%s\n",SSID_AP);
					strncpy(PASS_AP,d+end,16); end += 16;	// Serial.printf("PASS_AP     =%s\n",PASS_AP);
					strncpy(SSID_STA,d+end,17); end += 17;	// Serial.printf("SSID_STA    =%s\n",SSID_STA);
					strncpy(PASS_STA,d+end,65); end += 65;	// Serial.printf("PASS_STA    =%s\n",PASS_STA);
					WIFI_AP_MODE = (byte)d[end] - '0';		// Serial.printf("WIFI_AP_MODE=%d\n",WIFI_AP_MODE);
					end++;
					MDNS_EN = (byte)d[end] - '0';			// Serial.printf("MDNS_EN=%d\n",MDNS_EN);
					end++;
					SSID_MAC = (byte)d[end] - '0';			// Serial.printf("SSID_MAC=%d\n",SSID_MAC);
					end++;
					// int sizeと char dでデータサイズを設定する
					// ファイルの書き込みは html.ino
			//	}
				file.close();
			}else Serial.println("no setting files.");
		}
	}
	SPIFFS.end();
	if(BOARD_TYPE > 2 || strlen(SSID_AP) == 0 || WIFI_AP_MODE > 3){
		Serial.println("ERROR Invalid value, configs revoked");
		SPIFFS.format();
		for(int i=0;i<100;i++)delay(1);	// 待ち時間処理
		BOARD_TYPE = 1;
		WIFI_AP_MODE = 1;
		strcpy(SSID_AP,"iot-core-esp32");
		strcpy(PASS_AP,"password");
	}
//	*/
	Serial.printf("MAC Address= %02x:%02x:%02x:%02x:%02x:%02x\r\n", MAC[0], MAC[1], MAC[2], MAC[3], MAC[4], MAC[5]);
	Serial.print("Wi-Fi Mode = ");
	char mode_s[4][7]={"OFF","AP","STA","AP+STA"};
	Serial.println( mode_s[WIFI_AP_MODE] );
	
	if( (WIFI_AP_MODE & 1) == 1){
		Serial.print("SSID_AP    = " + String(SSID_AP) );
		if(SSID_MAC) Serial.printf("-%02x%02x",MAC[4],MAC[5]);
		Serial.println("\r\nPASS_AP    = " + String(PASS_AP) );
	}
	if( (WIFI_AP_MODE & 2) == 2){
		if(strlen(SSID_STA)>0)Serial.println("SSID_STA   = " + String(SSID_STA) );
		if(strlen(PASS_STA)>0){
			Serial.print("PASS_STA   = ");
			for(int i=0;i<strlen(PASS_STA);i++) Serial.print("*");
			Serial.println();
		}
	}
	
//	IP_AP = IPAddress(192,168,254,1);
	for(int i=0;i<10;i++) delay(1);				// ESP32に必要な待ち時間
	switch(WIFI_AP_MODE){
		case 1:	// WIFI_AP
			WiFi.mode(WIFI_AP); 				// 無線LANを[AP]モードに設定
			setupWifiAp();
			IP = WiFi.softAPIP();
			IP_STA = IP;
			IP_BC = (uint32_t)IP | IPAddress(0,0,0,255);
			break;
		case 2:	// WIFI_STA
			WiFi.mode(WIFI_STA);				// 無線LANを[STA]モードに設定
			setupWifiSta();
			IP = WiFi.localIP();
			IP_STA = IP;
			IP_BC = (uint32_t)IP | ~(uint32_t)(WiFi.subnetMask());
			break;
		case 3:	// WIFI_AP_STA
			WiFi.mode(WIFI_AP_STA);				// 無線LANを[AP+STA]モードに設定
			setupWifiSta();
			setupWifiAp();
			IP = WiFi.softAPIP();
			IP_STA = WiFi.localIP();
			IP_BC = (uint32_t)(WiFi.localIP()) | IPAddress(0,0,0,255);
			break;
		default: // WIFI_OFF
			WiFi.mode(WIFI_OFF);
			IP = IPAddress(0,0,0,0);
			IP_STA = IPAddress(0,0,0,0);
			IP_BC = IPAddress(255,255,255,255);
	}
	if( (uint32_t)IP > 0 ) digitalWrite(PIN_LED,HIGH);
	else{
		digitalWrite(PIN_LED,LOW);
		Serial.println("Wi-Fi Mode = OFF");
		TimerWakeUp_setSleepTime(SLEEP_SEC);
		TimerWakeUp_sleep();
	}

	if(WiFi.status() == WL_CONNECTED){			// 接続に成功した時
		if(NTP_EN){
			Serial.println("NTP client started");
			TIME=getNtp();						// NTP時刻を取得
			TIME-=millis()/1000;				// カウント済み内部タイマー事前考慮
		}
	}
	
	// mDND サーバ Bonjour
	if(MDNS_EN){
		MDNS_EN=MDNS.begin("iot");
		if(MDNS_EN) Serial.println("MDNS responder started");
	}
	
	// Web サーバ
	html_init("iot",IP,IP_AP,IP_STA);
	Serial.print("WebServ IP = ");
	Serial.println( IP );
	Serial.print("     BC IP = ");
	Serial.println( IP_BC );
	if(MDNS_EN)Serial.println(" URL(mDNS) = http://iot.local/");
	Serial.print("   URL(IP) = http://");
	Serial.print(IP);
	Serial.println("/");
	
	Serial.println("Finished Setting Up ----" + Line + "\r\n");
	// 起動時の送信
	if( wake == 0 ){
		String ip_S = String(IP[0])+","+String(IP[1])+","+String(IP[2])+","+String(IP[3]);
		sendUdp("ident",ip_S);
		for(int i=0;i<100;i++)delay(1);	// 待ち時間処理
	}
	sendSensorValues();
	
	TIME_NEXT = millis() + (unsigned long)SEND_INT_SEC * 1000;
	// Wi-Fi スリープ間隔180秒超過(10分以上を設定)、または 起動回数 360回超過(30秒間隔で3時間)で即sleep
	if(SLEEP_SEC > 180 || TimerWakeUp_bootCount() > 360) sleep();
	Serial.println("Waiting for Trigger ----" + Line);
}

void loop(){
	unsigned long time=millis();			// ミリ秒の取得
	
	html_handleClient();
	yield();
	sensors_btnRead("Triggered by Button ----" + Line);
	sensors_pirRead("Triggered by PIR Sensor " + Line);
	sensors_irRead( "Triggered by IR Sensor -" + Line);
	
	if( ((WIFI_AP_MODE & 2) == 2) && (SLEEP_SEC > 0) ){		// WiFi_STA 動作時
		if( time > TIMEOUT ) sleep();
	}
	
	if(time<100){
		TIME_NEXT_b = false;
		if(NTP_EN){
			Serial.println("NTP client started");
			TIME=getNtp();					// NTP時刻を取得
			TIME-=millis()/1000;			// カウント済み内部タイマー事前考慮
		}
		while( millis() < 100 ) delay(1);	// 待ち時間処理(最大100ms)
	}
	if(time > TIME_NEXT && !TIME_NEXT_b){
		Serial.println("Triggered by Timer -----" + Line);
		Serial.println("MCU Clock_s= " + String(time/1000));
		sendSensorValues();
		if(SEND_INT_SEC){
			TIME_NEXT = millis() + (unsigned long)SEND_INT_SEC * 1000;
			if( TIME_NEXT < (unsigned long)SEND_INT_SEC * 1000 ) TIME_NEXT_b = true;
		}else{
			TIME_NEXT = millis() + 5000ul;
			if( TIME_NEXT < 5000ul ) TIME_NEXT_b = true;
		}
	}
}

void sleep(){
	boolean led;
	Serial.println("Shutting down (Working Time = " + String((float)millis() / 1000,1) + ")");
//	WiFi.disconnect();

	pinMode(PIN_SW,INPUT_PULLUP);
	
	if(I2C_ENV_EN > 0) i2c_bme280_stop();
	while(!digitalRead(PIN_SW)){
		digitalWrite(PIN_LED,!digitalRead(PIN_LED));
		for(int i=0;i<50;i++)delay(1);	// 待ち時間処理
	}
	digitalWrite(PIN_LED,LOW);
	
	if(!PIR_EN && !IR_IN_EN && SLEEP_SEC > 0){
		Serial.println("Deep Sleep Mode --------" + Line);
		for(int i=0;i<10;i++) delay(1);
		esp_deep_sleep(SLEEP_SEC * 1000000u);	// 間欠動作用(タイマー割り込みのみ)
	}
	
	// ボタン・入力待機用スリープ
	TimerWakeUp_setExternalInput((gpio_num_t)PIN_SW, LOW);
	if(PIR_EN){
		pinMode(PIN_PIR,INPUT);
		while(digitalRead(PIN_PIR)){
			digitalWrite(PIN_LED,!digitalRead(PIN_LED));
			for(int i=0;i<50;i++)delay(1);	// 待ち時間処理
		}
		TimerWakeUp_setExternalInput((gpio_num_t)PIN_PIR, HIGH);
	}
	if(IR_IN_EN){
		pinMode(PIN_IR_IN,INPUT);
		while(!digitalRead(PIN_IR_IN)){
			digitalWrite(PIN_LED,!digitalRead(PIN_LED));
			for(int i=0;i<50;i++)delay(1);	// 待ち時間処理
		}
		TimerWakeUp_setExternalInput((gpio_num_t)PIN_IR_IN, LOW);
	}
	
//	if(ESP.getChipRevision() != 0 ){
		// RTC_IO
		//						// IO0	Booting Mode|Default:Pull-up
		rtc_io_setPin(2,0);		// IO2	Booting Mode|Default:Pull-down
		//						// IO5	Timing SDIO	|Default:Pull-up
		rtc_io_setPin(12,0);	// IO12	VDD_SDIO	|Default:Pull-down
		//						// IO15	DebuggingLog|Default:Pull-up
		if(PIR_EN || IR_IN_EN){
			rtc_io_setPin(PIN_GND,0);
			rtc_io_setPin(PIN_VDD,1);
		}
		rtc_io_on();
//	}

	if(SLEEP_SEC > 0) TimerWakeUp_setSleepTime(SLEEP_SEC);
	digitalWrite(PIN_LED,LOW);
	TimerWakeUp_sleep();
	return;
}
