#include <LiquidCrystal_I2C.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiAP.h>

#include <WebServer.h>

#include <WiFiUdp.h>
#include "Arduino.h"

#include <Wire.h>

#include <Joystick.h>
#include <AxisJoystick.h>

#include <EEPROM.h>


#define button_left A0
#define button_right A1
#define button_forward A2
#define button_backward A3
#define button_throtle A4

#define EEPROM_SIZE 50

class zeppelin {
  public:
    int ID =0;
    IPAddress ip; //(0, 0, 0, 0);
    int vbat = 0;
    int charging = 0;
    int timeout = 0;  // time from last msg recieved

    void reset();
  
};

void zeppelin::reset() {
  ID = 0;
  vbat = 0;
  charging = 0;
  timeout = 0;
}


const int vectorsize = 6;
zeppelin container[vectorsize];

// Set these to your desired credentials.
const char *ssid = "NoGargoyle1";
const char *password = "polibuda238";
const int udpPort = 3333;

// const char * udpAddressZeppelin = "192.168.4.102";
const char * udpBroadcast = "192.168.4.2";    //////////////    !!!!!!!!!!!!!

WiFiUDP udp;

WebServer server(80);
bool onlineCalib = 0;
uint8_t calib_phase = 0;

LiquidCrystal_I2C lcd = LiquidCrystal_I2C(0x3F, 20, 4); // 0x27 for 2x16

int dirT = 0;
int dirL = 0;
int dirR = 0;

int throtle = 0;
int pwmL = 0;
int pwmR = 0;

int pitch = 0;
int roll = 0;

void calculate_output();
void parseUdpData(int, char, zeppelin);

int msgTimer[6] = {0, 0, 0, 0, 0, 0};
hw_timer_t * timer = NULL;

int restartTimer = 0;

void IRAM_ATTR onTimer(){
//Serial.println("timer interrupt ..................................................."); ///////////////////////////////////////////////////////////////////
  for (int i = 0; i < 6; i++) {
    container[i].timeout++;
  }
  restartTimer++;
  if( restartTimer >= 600  ) {
    ESP.restart();
  }
}

void(* resetFunc) (void) = 0; //declare reset function @ address 0

void setup() {
///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
//   dodac pinMode dla pinow wejsciowych

  pinMode(button_left, INPUT); // INPUT
  pinMode(button_right, INPUT);
  pinMode(A2, INPUT);
  pinMode(button_backward, INPUT);
  pinMode(button_throtle, INPUT);

  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(115200);

  EEPROM.begin(EEPROM_SIZE);
    
  Serial.println();
  Serial.println("Configuring access point...");

  WiFi.softAP(ssid, password, 1, false, 6);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  //This initializes the transfer buffer
  udp.begin(WiFi.localIP(),udpPort);

  lcd.init();
  lcd.backlight();

  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 1000000, true); 
  timerAlarmEnable(timer);

  delay(100);

  server.on("/", handle_OnConnect);
  server.on("/webcalib", handleWebCalib_ON);
  server.onNotFound(handle_NotFound);

  server.begin();
  Serial.println("HTTP server started");
  
}


void loop() {
  calculate_output();

  while ( int packetSize = udp.parsePacket() ) {
    char packetBuffer[255];
    int len = udp.read(packetBuffer, 255);
    if (len > 0) {
      packetBuffer[len] = 0;
    }
    zeppelin zep;
    parseUdpData(len, packetBuffer, &zep);
    container[zep.ID] = zep;
  }
  
  i2cLCD();

  // tu wybrac sterowiec
  int cnt = 0; // how much in the air
  int zepNum = 0;
  int inTheAir = 0;
  for (int i = 0 ; i <= 5 ; i++ ) {
    if ( container[i].timeout > 5 ) container[i].reset();
    if ( container[i].ID != 0 ) zepNum++;
    if ( container[i].charging == 1 ) {  // 2-charging, 1-flying, 0-default
      cnt++;
      inTheAir = i;
    }
  }


  if ( cnt == 1 ) { // if ( cnt == 1 and container[inTheAir].vbat > 350 ) {   // dobrac wartosc v bat
    udp.beginPacket(container[inTheAir].ip, udpPort);
    udp.printf("L%i%03iR%i%03iT%i%03i", dirL, pwmL, dirR, pwmR, dirT, throtle);
    Serial.printf("L%d%03dR%d%03d T%d%03d \n", dirL, pwmL, dirR, pwmR, dirT, throtle);
    udp.endPacket();
  }

  server.handleClient();
  if( onlineCalib ) {
      EEPROM.begin(EEPROM_SIZE);
      EEPROM.write(0, 1); // address 0, write data = 1
      if ( EEPROM.commit() ) {
        Serial.println("calibration eeprom variable set to 1");
      }
      else {
        Serial.println("error_calibration_variable_EEPROM_writting_failed");
      }
      resetFunc();  //call reset
  }
 
  delay(20);
  
}


void calculate_output() {
///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
//    dodac odczyt wejsc

  int left_motor_mixer = 0;
  int right_motor_mixer = 0;
  int throttle_motor_mixer = 0;
  
  if (digitalRead(button_forward)) {
    left_motor_mixer++;
    right_motor_mixer++;
    Serial.println(" digital read FALSE ---------------");
  }

  if (digitalRead(button_backward)) {
    left_motor_mixer--;
    right_motor_mixer--;
  }
  
  if (digitalRead(button_left)) {
    left_motor_mixer--;
    right_motor_mixer++;
  }
  
  if (digitalRead(button_right)) {
    left_motor_mixer++;
    right_motor_mixer--;
  }

  if (digitalRead(button_throtle)) {
    throttle_motor_mixer = 1;
    throtle = 255;
  }
  else {
    throtle = 0;
  }

  if (right_motor_mixer != 0) {
    pwmR = 255;
    if ( right_motor_mixer > 0 ) {
      dirR = 0;   // odwrocilem dirR bo bylo inaczej na sterowcach
    }
    else {
      dirR = 1;
    }
  }
  else {
    pwmR = 0;
  }

  if (left_motor_mixer != 0) {
    pwmL = 255;
    if ( left_motor_mixer > 0 ) {
      dirL = 1;
    }
    else {
      dirL = 0;
    }
  }
  else {
    pwmL = 0;
  }
Serial.printf("R: %i  L: %i \n ", right_motor_mixer, left_motor_mixer);
}

void parseUdpData(int len, char bufer[], zeppelin *zep) {
  if ( len == 9 and bufer[0] == '.' and bufer[2] == '.' and bufer[6] == '.' and bufer[8] == '.' ) {

     int zeppelinID = bufer[1] - '0';
     int zeppelinVBAT = (bufer[3] - '0') * 100 + (bufer[4] - '0') * 10 + (bufer[5] - '0');
     int zeppelinUSBflag = bufer[7] - '0';
     if ( zeppelinID >= 0 and zeppelinID <= 5 and zeppelinVBAT < 450 and ( zeppelinUSBflag == 1 or zeppelinUSBflag == 2 ) ) {
       // dobra ramka wyslana
       zep->ip = udp.remoteIP();
       zep->ID = zeppelinID;
       zep->charging = zeppelinUSBflag;
       zep->vbat = zeppelinVBAT;
       zep->timeout = 0;
     }
     else {
       Serial.println("wrong_values_in_data_recieved");
       Serial.printf("zeppelinID %i \n", zeppelinID);
       Serial.printf("zeppelinVBAT %i \n", zeppelinVBAT);
       Serial.printf("zeppelinUSBflag %i\n ", zeppelinUSBflag);
     }
  }
  else {
    // Serial.println("wrong_data_format_recieved : ");
    // Serial.println(bufer);
  }
}


void i2cLCD() {
  //lcd.clear();
  
  lcd.setCursor(0, 0); // Set the cursor on the first column and first row.
  lcd.print("      Sterowce");
  lcd.setCursor(0, 1);
  i2cZep(container[0]);
  lcd.setCursor(10, 1);
  i2cZep(container[1]);
  lcd.setCursor(0, 2);
  i2cZep(container[2]);
  lcd.setCursor(10, 2);
  i2cZep(container[3]);
  lcd.setCursor(0, 3);
  i2cZep(container[4]);
  lcd.setCursor(10, 3);
  i2cZep(container[5]);
   
}

void i2cZep(zeppelin zep) {
  //lcd.print(zep.ID);
  //lcd.print(" ");
  //lcd.printf("%1i V%03i S%1i",zep.ID, zep.vbat, zep.charging);
  int bat_proc = map(zep.vbat, 325, 385, 0, 100);
  if( bat_proc > 100 ) bat_proc = 100;
  if( bat_proc < 0 ) bat_proc = 0;
  lcd.printf("%1i %03i%% S%1i|",zep.ID, bat_proc, zep.charging);
}

void handle_OnConnect() {
  onlineCalib = 0;
  Serial.println("online calib in now 0");
  server.send(200, "text/html", SendHTML(onlineCalib, calib_phase));
}

void handleWebCalib_ON() {
  onlineCalib = 1;
  Serial.println("online calib set to 1");
  server.send(200, "text/html", SendHTML(onlineCalib, calib_phase)); 
}

void handle_NotFound(){
  server.send(404, "text/plain", "Not found");
}

String SendHTML(uint8_t bool_cal, uint8_t cal_phase){
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<title>zeppelin calibration</title>\n";
  ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n";
  ptr +=".button {display: block;width: 80px;background-color: #3498db;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 4px;}\n";
  ptr +=".button-on {background-color: #3498db;}\n";
  ptr +=".button-on:active {background-color: #2980b9;}\n";
  ptr +=".button-off {background-color: #34495e;}\n";
  ptr +=".button-off:active {background-color: #2c3e50;}\n";
  ptr +="p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";
  ptr +="</style>\n";
  ptr +="<meta http-equiv=”refresh” content=”30″>\n";  ///////////////////////// ODSWIERZANIE CO 30 SEKUND STRONY
  ptr +="</head>\n";
  ptr +="<body>\n";
  ptr +="<h1>ESP32 Web Server</h1>\n";
  ptr +="<h3>Using Access Point(AP) Mode</h3>\n";
  
  if(!bool_cal) {
    ptr +="<p>calibration status: OFF</p><a class=\"button button-on\" href=\"/webcalib\">ON</a>\n";
  }
  
  for(int ci=0; ci<=5; ci++) {
    if(container[ci].ID != 0) {
      ptr +="<h1> sterowiec nr. ";
      ptr += String(ci);
      ptr += " w sieci \n";
    }
  }

  ptr +="</body>\n";
  ptr +="</html>\n";
  return ptr;
}
