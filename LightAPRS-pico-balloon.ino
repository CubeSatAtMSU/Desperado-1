#include <LibAPRS.h>        //Modified version of https://github.com/markqvist/LibAPRS
#include <SoftwareSerial.h>
#include <TinyGPS++.h>      //https://github.com/mikalhart/TinyGPSPlus
#include <LowPower.h>       //https://github.com/rocketscream/Low-Power
#include <Wire.h>
#include <Adafruit_BMP085.h>//https://github.com/adafruit/Adafruit-BMP085-Library
#include "GEOFENCE.h"       // Modified version of https://github.com/TomasTT7/TT7F-Float-Tracker/blob/master/Software/ARM_GEOFENCE.c
#include <avr/wdt.h>
#include <EEPROM.h>

#define RfPDPin     19
#define GpsVccPin   18
#define RfPwrHLPin  21
#define RfPttPin    20
#define BattPin     A2
#define PIN_DRA_RX  22
#define PIN_DRA_TX  23

#define ADC_REFERENCE REF_3V3
#define OPEN_SQUELCH false

#define GpsON         digitalWrite(GpsVccPin, LOW)//PNP
#define GpsOFF        digitalWrite(GpsVccPin, HIGH)
#define RfON          digitalWrite(RfPDPin, HIGH)
#define RfOFF         digitalWrite(RfPDPin, LOW)
#define RfPwrHigh     pinMode(RfPwrHLPin, INPUT)
#define RfPwrLow      pinMode(RfPwrHLPin, OUTPUT);digitalWrite(RfPwrHLPin, LOW)
#define RfPttON       digitalWrite(RfPttPin, HIGH)//NPN
#define RfPttOFF      digitalWrite(RfPttPin, LOW)
#define AprsPinInput  pinMode(12,INPUT);pinMode(13,INPUT);pinMode(14,INPUT);pinMode(15,INPUT)
#define AprsPinOutput pinMode(12,OUTPUT);pinMode(13,OUTPUT);pinMode(14,OUTPUT);pinMode(15,OUTPUT)
#define SERIAL_TELEM
// #define DEVMODE // Development mode. Uncomment to enable for debugging.

//https://github.com/lightaprs/LightAPRS-1.0/wiki/Tips-&-Tricks-for-Pico-Balloons

//****************************************************************************
char  CallSign[7]="NOCALL"; //DO NOT FORGET TO CHANGE YOUR CALLSIGN
int   CallNumber=11; //SSID http://www.aprs.org/aprs11/SSIDs.txt
char  Symbol='O'; // '/O' for balloon, '/>' for car, for more info : http://www.aprs.org/symbols/symbols-new.txt
bool  alternateSymbolTable = false ; //false = '/' , true = '\'

char comment[50] = "http://www.lightaprs.com"; // Max 50 char
char StatusMessage[50] = "LightAPRS by TA2NHP & TA2MUN";
//*****************************************************************************


unsigned int   BeaconWait=60;  //seconds sleep for next beacon (TX).
unsigned int   BattWait=60;    //seconds sleep if super capacitors/batteries are below BattMin (important if power source is solar panel) 
float BattMin=4.5;        // min Volts to wake up.
float DraHighVolt=5.0;    // min Volts for radio module (DRA818V) to transmit (TX) 1 Watt, below this transmit 0.5 Watt.
float GpsMinVolt=4.0; //min Volts for GPS to wake up. (important if power source is solar panel) 

boolean aliveStatus = false; //for tx status message on first wake-up just once.

//do not change WIDE path settings below if you don't know what you are doing :) 
byte  Wide1=1; // 1 for WIDE1-1 path
byte  Wide2=1; // 1 for WIDE2-1 path

/**
Airborne stations above a few thousand feet should ideally use NO path at all, or at the maximum just WIDE2-1 alone.  
Due to their extended transmit range due to elevation, multiple digipeater hops are not required by airborne stations.  
Multi-hop paths just add needless congestion on the shared APRS channel in areas hundreds of miles away from the aircraft's own location.  
NEVER use WIDE1-1 in an airborne path, since this can potentially trigger hundreds of home stations simultaneously over a radius of 150-200 miles. 
 */
int pathSize=2; // 2 for WIDE1-N,WIDE2-N ; 1 for WIDE2-N
boolean autoPathSizeHighAlt = true; //force path to WIDE2-N only for high altitude (airborne) beaconing (over 1.000 meters (3.280 feet)) 

boolean beaconViaARISS = false; //there are no iGates in some regions (such as North Africa,  Oceans, etc) so try to beacon via ARISS (International Space Station) https://www.amsat.org/amateur-radio-on-the-iss/

// GEOFENCE 
uint32_t GEOFENCE_APRS_frequency      = 144800000; //default frequency before geofencing. This variable will be updated based on GPS location.
uint32_t GEOFENCE_no_tx               = 0; 
boolean arissModEnabled = false; //do not change this, temp value. 

boolean radioSetup = false;
boolean GpsFirstFix=false;
boolean ublox_high_alt_mode_enabled = false; //do not change this.

static char telemetry_buff[100];// telemetry buffer
uint16_t TxCount = 1;

struct Date 
{ 
    int d, m, y; 
}; 

const int monthDays[12] = {31, 28, 31, 30, 31, 30, 
                           31, 31, 30, 31, 30, 31}; 

TinyGPSPlus gps;
Adafruit_BMP085 bmp;

void setup() {
  wdt_enable(WDTO_8S);
  analogReference(INTERNAL2V56);
  pinMode(RfPDPin, OUTPUT);
  pinMode(GpsVccPin, OUTPUT);
  pinMode(RfPwrHLPin, OUTPUT);
  pinMode(RfPttPin, OUTPUT);
  pinMode(BattPin, INPUT);
  pinMode(PIN_DRA_TX,INPUT);

  RfOFF;
  GpsOFF;
  RfPwrLow;
  RfPttOFF;

  Serial.begin(57600);
  Serial1.begin(9600);
#if defined(DEVMODE)
  Serial.println(F("Start"));
#endif
      
  APRS_init(ADC_REFERENCE, OPEN_SQUELCH);
  APRS_setCallsign(CallSign,CallNumber);
  APRS_setDestination("APLIGA", 0);
  APRS_setMessageDestination("APLIGA", 0);
  APRS_setPath1("WIDE1", Wide1);
  APRS_setPath2("WIDE2", Wide2);
  APRS_useAlternateSymbolTable(alternateSymbolTable); 
  APRS_setSymbol(Symbol);
  //increase following value (for example to 500UL) if you experience packet loss/decode issues. 
  APRS_setPreamble(350UL);  
  APRS_setPathSize(pathSize);
  AprsPinInput;
  
  bmp.begin();
}

void loop() {
   wdt_reset();
  
  if (readBatt() > BattMin) {
  
  
  if(aliveStatus){

      //send status tx on startup once (before gps fix)
      
      #if defined(DEVMODE)
        Serial.println(F("Sending"));
      #endif
      sendStatus();
      #if defined(DEVMODE)
        Serial.println(F("Sent"));
      #endif
      
      aliveStatus = false;

      while (readBatt() < BattMin) {
        sleepSeconds(BattWait); 
      }
      
   }
    
    updateGpsData(1000);
    gpsDebug();
    // updateTelemetry();

    
    if ((gps.location.age() < 1000 || gps.location.isUpdated()) && gps.location.isValid()) {
      if (gps.satellites.isValid() && (gps.satellites.value() > 3)) {
      updatePosition();
      updateTelemetry();
      
      GpsOFF;
      ublox_high_alt_mode_enabled = false; //gps sleep mode resets high altitude mode.
      GpsFirstFix=true;

      if(!arissModEnabled && autoPathSizeHighAlt && gps.altitude.feet()>3000){
            //force to use high altitude settings (WIDE2-n)
            APRS_setPathSize(1);
        } else {
            //use default settings  
            APRS_setPathSize(pathSize);
        }
          
      //APRS frequency isn't the same for the whole world. (for pico balloon only)
      if (!radioSetup) {
        configureFreqbyLocation();
      }          
      
      //send status message every 60 minutes
      if(gps.time.minute() == 0){               
        sendStatus();       
      } else {

        //in some countries Airborne APRS is not allowed. (for pico balloon only)
        if(isAirborneAPRSAllowed()){
          sendLocation();
        }
      }

      freeMem();
      Serial.flush();
      sleepSeconds(BeaconWait);

      } else {
#if defined(DEVMODE)
      Serial.println(F("Not enough sattelites"));
#endif
      }
    } 
  } else {

    sleepSeconds(BattWait);
    
  }
  
}

void aprs_msg_callback(struct AX25Msg *msg) {
  //do not remove this function, necessary for LibAPRS
}

void sleepSeconds(int sec) {  
  if (GpsFirstFix){//sleep gps after first fix
      GpsOFF;
      ublox_high_alt_mode_enabled = false;//gps sleep mode resets high altitude mode.
    } 
  RfOFF;
  RfPttOFF;
  Serial.flush();
  wdt_disable();
  for (int i = 0; i < sec; i++) {
    if (readBatt() < GpsMinVolt){
      GpsOFF;
      ublox_high_alt_mode_enabled = false;//gps sleep mode resets high altitude mode.
    } 
    LowPower.powerDown(SLEEP_1S, ADC_OFF, BOD_ON);   
  }
   wdt_enable(WDTO_8S);
}


boolean isAirborneAPRSAllowed() {

  float tempLat = gps.location.lat();
  float tempLong = gps.location.lng();

  GEOFENCE_position(tempLat,tempLong);
  
  boolean airborne = true;

  if(GEOFENCE_no_tx==1) {
      airborne = false;
  }

  return airborne;
}


boolean inARISSGeoFence(float tempLat, float tempLong) {


  boolean ariss = false;
  //North Africa
  if(tempLat>0 && tempLat<32 && tempLong>0 && tempLong<32){ariss = true;}
  //North Pacific
  if(tempLat>28 && tempLat<50 && tempLong>-180 && tempLong<-130){ariss = true;}
  //North Atlantic
  if(tempLat>25 && tempLat<42 && tempLong>-60 && tempLong<-33){ariss = true;} 
 
  return ariss;
}


void configureFreqbyLocation() {

  float tempLat = gps.location.lat();
  float tempLong = gps.location.lng();


  if(beaconViaARISS && inARISSGeoFence(tempLat, tempLong)) {
    APRS_setPath1("ARISS", Wide1);
    APRS_setPath2("WIDE2", Wide2);
    APRS_setPathSize(2);
    configDra818("145.8250");
    arissModEnabled = true;
  } else {

    GEOFENCE_position(tempLat,tempLong);  
    float dividedFreq = GEOFENCE_APRS_frequency / 1000000.f;
    char aprsFreq_buff[9];
    dtostrf(dividedFreq, 8, 4, aprsFreq_buff);
    configDra818(aprsFreq_buff);    
    arissModEnabled = false;
  }
  
  radioSetup = true;
}


byte configDra818(char *freq)
{
  SoftwareSerial Serial_dra(PIN_DRA_RX, PIN_DRA_TX);
  Serial_dra.begin(9600);
  RfON;
  char ack[3];
  int n;
  delay(2000);
  char cmd[50];
  sprintf(cmd, "AT+DMOSETGROUP=0,%s,%s,0000,4,0000", freq, freq);
  Serial_dra.println(cmd);
  ack[2] = 0;
  while (ack[2] != 0xa)
  {
    if (Serial_dra.available() > 0) {
      ack[0] = ack[1];
      ack[1] = ack[2];
      ack[2] = Serial_dra.read();
    }
  }
  Serial_dra.end();
  RfOFF;
  pinMode(PIN_DRA_TX,INPUT);
#if defined(DEVMODE)
  if (ack[0] == 0x30) Serial.println(F("Frequency updated...")); else Serial.println(F("Frequency update error!"));
#endif
  return (ack[0] == 0x30) ? 1 : 0;
}

void updatePosition() {
  // Convert and set latitude NMEA string Degree Minute Hundreths of minutes ddmm.hh[S,N].
  char latStr[10];
  int temp = 0;

  double d_lat = gps.location.lat();
  double dm_lat = 0.0;

  if (d_lat < 0.0) {
    temp = -(int)d_lat;
    dm_lat = temp * 100.0 - (d_lat + temp) * 60.0;
  } else {
    temp = (int)d_lat;
    dm_lat = temp * 100 + (d_lat - temp) * 60.0;
  }

  dtostrf(dm_lat, 7, 2, latStr);

  if (dm_lat < 1000) {
    latStr[0] = '0';
  }

  if (d_lat >= 0.0) {
    latStr[7] = 'N';
  } else {
    latStr[7] = 'S';
  }

  APRS_setLat(latStr);

  // Convert and set longitude NMEA string Degree Minute Hundreths of minutes ddmm.hh[E,W].
  char lonStr[10];
  double d_lon = gps.location.lng();
  double dm_lon = 0.0;

  if (d_lon < 0.0) {
    temp = -(int)d_lon;
    dm_lon = temp * 100.0 - (d_lon + temp) * 60.0;
  } else {
    temp = (int)d_lon;
    dm_lon = temp * 100 + (d_lon - temp) * 60.0;
  }

  dtostrf(dm_lon, 8, 2, lonStr);

  if (dm_lon < 10000) {
    lonStr[0] = '0';
  }
  if (dm_lon < 1000) {
    lonStr[1] = '0';
  }

  if (d_lon >= 0.0) {
    lonStr[8] = 'E';
  } else {
    lonStr[8] = 'W';
  }

  APRS_setLon(lonStr);
}


void updateTelemetry() {
 
  sprintf(telemetry_buff, "%03d", gps.course.isValid() ? (int)gps.course.deg() : 0);
  telemetry_buff[3] = '/';
  sprintf(telemetry_buff + 4, "%03d", gps.speed.isValid() ? (int)gps.speed.knots() : 0);
  telemetry_buff[7] = '/';
  telemetry_buff[8] = 'A';
  telemetry_buff[9] = '=';
  //sprintf(telemetry_buff + 10, "%06lu", (long)gps.altitude.feet());

  //fixing negative altitude values causing display bug on aprs.fi
  float tempAltitude = gps.altitude.feet();

  if (tempAltitude>0){
    //for positive values
    sprintf(telemetry_buff + 10, "%06lu", (long)tempAltitude);
  } else{
    //for negative values
    sprintf(telemetry_buff + 10, "%06d", (long)tempAltitude);
    } 
  
  telemetry_buff[16] = ' ';
  sprintf(telemetry_buff + 17, "%03d", TxCount);
  telemetry_buff[20] = 'T';
  telemetry_buff[21] = 'x';
  telemetry_buff[22] = 'C';
  telemetry_buff[23] = ' '; float tempC = bmp.readTemperature();//-21.4;//
  dtostrf(tempC, 6, 2, telemetry_buff + 24);
  telemetry_buff[30] = 'C';
  telemetry_buff[31] = ' '; float pressure = bmp.readPressure() / 100.0; //Pa to hPa
  dtostrf(pressure, 7, 2, telemetry_buff + 32);
  telemetry_buff[39] = 'h';
  telemetry_buff[40] = 'P';
  telemetry_buff[41] = 'a';
  telemetry_buff[42] = ' ';
  dtostrf(readBatt(), 5, 2, telemetry_buff + 43);
  telemetry_buff[48] = 'V';
  telemetry_buff[49] = ' ';
  sprintf(telemetry_buff + 50, "%02d", gps.satellites.isValid() ? (int)gps.satellites.value() : 0);
  telemetry_buff[52] = 'S';
  telemetry_buff[53] = ' ';
  sprintf(telemetry_buff + 54, "%s", comment);
  
#if defined(SERIAL_TELEM)
  Serial.print("Telemetry: ");
  Serial.println(telemetry_buff);
#endif

#if defined(DEVMODE)
  Serial.println(telemetry_buff);
#endif

}


void sendLocation() {

#if defined(DEVMODE)
      Serial.println(F("Location sending with comment"));
#endif
  if ((readBatt() > DraHighVolt) && (readBatt() < 10)) RfPwrHigh; //DRA Power 1 Watt
  else RfPwrLow; //DRA Power 0.5 Watt

  int hh = gps.time.hour();
  int mm = gps.time.minute();
  int ss = gps.time.second();

  char timestamp_buff[7];

  sprintf(timestamp_buff, "%02d", gps.time.isValid() ? (int)gps.time.hour() : 0);
  sprintf(timestamp_buff + 2, "%02d", gps.time.isValid() ? (int)gps.time.minute() : 0);
  sprintf(timestamp_buff + 4, "%02d", gps.time.isValid() ? (int)gps.time.second() : 0);
  timestamp_buff[6] = 'h';
  AprsPinOutput;
  RfON;
  delay(2000);
  RfPttON;
  delay(1000);
  
  //APRS_sendLoc(telemetry_buff, strlen(telemetry_buff)); //beacon without timestamp
  APRS_sendLocWtTmStmp(telemetry_buff, strlen(telemetry_buff), timestamp_buff); //beacon with timestamp
  delay(50);
  while(digitalRead(1)){;}//LibAprs TX Led pin PB1
  delay(50);
  RfPttOFF;
  RfOFF;
  AprsPinInput;
#if defined(DEVMODE)
  Serial.println(F("Location sent with comment"));
#endif

  TxCount++;
}

void sendStatus() {
  if ((readBatt() > DraHighVolt) && (readBatt() < 10)) RfPwrHigh; //DRA Power 1 Watt
  else RfPwrLow; //DRA Power 0.5 Watt

  char status_buff[60];

  int firstDayCheck = 0;
  int difference = 0;

  //start day counter if balloon launched
  if(gps.satellites.isValid() && gps.satellites.value()>3 && gps.date.isValid() && gps.altitude.feet()>15000){

    firstDayCheck = EEPROM.read(0);

    Date today = {gps.date.day(), gps.date.month(), gps.date.year()};
    
    int firstDay = 0;
    int firstMonth = 0;
    int firstYear = 0;

    if(firstDayCheck == 1){
      firstDay = EEPROM.read(1);
      firstMonth = EEPROM.read(2);
      firstYear = EEPROM.read(3);

      Date firstDate = {firstDay, firstMonth, firstYear+2000};    

      difference = getDifference(firstDate,today);
      char diffBuf[4];
      sprintf(diffBuf,"%d",difference+1);

      sprintf(status_buff, "%s%s%s%s", "Day ",diffBuf," ",StatusMessage);
          
    } else {
      //write today's (first day) date
      EEPROM.update(0, 1); //check
      EEPROM.update(1, gps.date.day()); //day
      EEPROM.update(2, gps.date.month()); //month
      EEPROM.update(3, gps.date.year() % 2000); //year

      sprintf(status_buff, "%s%s", "Day 1 ",StatusMessage);
    }    
    
  } else {
    
    sprintf(status_buff, "%s", StatusMessage);
  }
  
  AprsPinOutput;
  RfON;
  delay(2000);
  RfPttON;
  delay(1000);
    
  APRS_sendStatus(status_buff, strlen(status_buff));
  delay(50);
  while(digitalRead(1)){;}//LibAprs TX Led pin PB1
  delay(50);
  RfPttOFF;
  RfOFF;
  AprsPinInput;
#if defined(DEVMODE)
  Serial.println(F("Status sent"));
#endif

  TxCount++;

}

int countLeapYears(Date d) 
{ 
    int years = d.y; 
  
    // Check if the current year needs to be considered 
    // for the count of leap years or not 
    if (d.m <= 2) 
        years--; 
  
    // An year is a leap year if it is a multiple of 4, 
    // multiple of 400 and not a multiple of 100. 
    return years / 4 - years / 100 + years / 400; 
} 
  
int getDifference(Date dt1, Date dt2) 
{ 
    // COUNT TOTAL NUMBER OF DAYS BEFORE FIRST DATE 'dt1' 
  
    // initialize count using years and day 
    long int n1 = dt1.y*365 + dt1.d; 
  
    // Add days for months in given date 
    for (int i=0; i<dt1.m - 1; i++) 
        n1 += monthDays[i]; 
  
    // Since every leap year is of 366 days, 
    // Add a day for every leap year 
    n1 += countLeapYears(dt1); 
  
    // SIMILARLY, COUNT TOTAL NUMBER OF DAYS BEFORE 'dt2' 
  
    long int n2 = dt2.y*365 + dt2.d; 
    for (int i=0; i<dt2.m - 1; i++) 
        n2 += monthDays[i]; 
    n2 += countLeapYears(dt2); 
  
    // return difference between two counts 
    return (n2 - n1); 
} 


static void updateGpsData(int ms)
{
  GpsON;

  if(!ublox_high_alt_mode_enabled){
      //enable ublox high altitude mode. increase ublox max. altitude limit from 12.000 meters to 50.000 meters.
      delay(100);
      setGPS_DynamicModel6();
      #if defined(DEVMODE)
        Serial.println(F("ublox DynamicModel6 enabled..."));
      #endif      
      ublox_high_alt_mode_enabled = true;
      
   }
  
  while (!Serial1) {
    delayMicroseconds(1); // wait for serial port to connect.
  }
    unsigned long start = millis();
    unsigned long bekle=0;
    do
    {
      while (Serial1.available()>0) {
        char c;
        c=Serial1.read();
        gps.encode(c);
        bekle= millis();
      }
      if (bekle!=0 && bekle+10<millis())break;
    } while (millis() - start < ms);

}

float readBatt() {
  float R1 = 560000.0; // 560K
  float R2 = 100000.0; // 100K
  float value = 0.0;
  do { 
    value =analogRead(BattPin);
    delay(5);
    value =analogRead(BattPin);
    value=value-8;
    value = (value * 2.56) / 1024.0;
    value = value / (R2/(R1+R2));
  } while (value > 16.0);
  return value ;
}

void freeMem() {
#if defined(DEVMODE)
  Serial.print(F("Free RAM: ")); Serial.print(freeMemory()); Serial.println(F(" byte"));
#endif
}

void gpsDebug() {
#if defined(DEVMODE)
  Serial.println();
  Serial.println(F("Sats HDOP Latitude   Longitude   Fix  Date       Time     Date Alt    Course Speed Card Chars Sentences Checksum"));
  Serial.println(F("          (deg)      (deg)       Age                      Age  (m)    --- from GPS ----  RX    RX        Fail"));
  Serial.println(F("-----------------------------------------------------------------------------------------------------------------"));

  printInt(gps.satellites.value(), gps.satellites.isValid(), 5);
  printInt(gps.hdop.value(), gps.hdop.isValid(), 5);
  printFloat(gps.location.lat(), gps.location.isValid(), 11, 6);
  printFloat(gps.location.lng(), gps.location.isValid(), 12, 6);
  printInt(gps.location.age(), gps.location.isValid(), 5);
  printDateTime(gps.date, gps.time);
  printFloat(gps.altitude.meters(), gps.altitude.isValid(), 7, 2);
  printFloat(gps.course.deg(), gps.course.isValid(), 7, 2);
  printFloat(gps.speed.kmph(), gps.speed.isValid(), 6, 2);
  printStr(gps.course.isValid() ? TinyGPSPlus::cardinal(gps.course.value()) : "*** ", 6);

  printInt(gps.charsProcessed(), true, 6);
  printInt(gps.sentencesWithFix(), true, 10);
  printInt(gps.failedChecksum(), true, 9);
  Serial.println();

#endif
}


static void printFloat(float val, bool valid, int len, int prec)
{
#if defined(DEVMODE)
  if (!valid)
  {
    while (len-- > 1)
      Serial.print('*');
    Serial.print(' ');
  }
  else
  {
    Serial.print(val, prec);
    int vi = abs((int)val);
    int flen = prec + (val < 0.0 ? 2 : 1); // . and -
    flen += vi >= 1000 ? 4 : vi >= 100 ? 3 : vi >= 10 ? 2 : 1;
    for (int i = flen; i < len; ++i)
      Serial.print(' ');
  }
#endif
}

static void printInt(unsigned long val, bool valid, int len)
{
#if defined(DEVMODE)
  char sz[32] = "*****************";
  if (valid)
    sprintf(sz, "%ld", val);
  sz[len] = 0;
  for (int i = strlen(sz); i < len; ++i)
    sz[i] = ' ';
  if (len > 0)
    sz[len - 1] = ' ';
  Serial.print(sz);
#endif
}

static void printDateTime(TinyGPSDate &d, TinyGPSTime &t)
{
#if defined(DEVMODE)
  if (!d.isValid())
  {
    Serial.print(F("********** "));
  }
  else
  {
    char sz[32];
    sprintf(sz, "%02d/%02d/%02d ", d.month(), d.day(), d.year());
    Serial.print(sz);
  }

  if (!t.isValid())
  {
    Serial.print(F("******** "));
  }
  else
  {
    char sz[32];
    sprintf(sz, "%02d:%02d:%02d ", t.hour(), t.minute(), t.second());
    Serial.print(sz);
  }

  printInt(d.age(), d.isValid(), 5);
#endif
}

static void printStr(const char *str, int len)
{
#if defined(DEVMODE)
  int slen = strlen(str);
  for (int i = 0; i < len; ++i)
    Serial.print(i < slen ? str[i] : ' ');
#endif
}

//following GPS code from : https://github.com/HABduino/HABduino/blob/master/Software/habduino_v4/habduino_v4.ino
void setGPS_DynamicModel6()
{
 int gps_set_sucess=0;
 uint8_t setdm6[] = {
 0xB5, 0x62, 0x06, 0x24, 0x24, 0x00, 0xFF, 0xFF, 0x06,
 0x03, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00,
 0x05, 0x00, 0xFA, 0x00, 0xFA, 0x00, 0x64, 0x00, 0x2C,
 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0xDC };
 
 while(!gps_set_sucess)
 {
 sendUBX(setdm6, sizeof(setdm6)/sizeof(uint8_t));
 gps_set_sucess=getUBX_ACK(setdm6);
 }
}

void sendUBX(uint8_t *MSG, uint8_t len) {
 Serial1.flush();
 Serial1.write(0xFF);
 delay(500);
 for(int i=0; i<len; i++) {
 Serial1.write(MSG[i]);
 }
}
boolean getUBX_ACK(uint8_t *MSG) {
 uint8_t b;
 uint8_t ackByteID = 0;
 uint8_t ackPacket[10];
 unsigned long startTime = millis();
 
// Construct the expected ACK packet
 ackPacket[0] = 0xB5; // header
 ackPacket[1] = 0x62; // header
 ackPacket[2] = 0x05; // class
 ackPacket[3] = 0x01; // id
 ackPacket[4] = 0x02; // length
 ackPacket[5] = 0x00;
 ackPacket[6] = MSG[2]; // ACK class
 ackPacket[7] = MSG[3]; // ACK id
 ackPacket[8] = 0; // CK_A
 ackPacket[9] = 0; // CK_B
 
// Calculate the checksums
 for (uint8_t ubxi=2; ubxi<8; ubxi++) {
 ackPacket[8] = ackPacket[8] + ackPacket[ubxi];
 ackPacket[9] = ackPacket[9] + ackPacket[8];
 }
 
while (1) {
 
// Test for success
 if (ackByteID > 9) {
 // All packets in order!
 return true;
 }
 
// Timeout if no valid response in 3 seconds
 if (millis() - startTime > 3000) {
 return false;
 }
 
// Make sure data is available to read
 if (Serial1.available()) {
 b = Serial1.read();
 
// Check that bytes arrive in sequence as per expected ACK packet
 if (b == ackPacket[ackByteID]) {
 ackByteID++;
 }
 else {
 ackByteID = 0; // Reset and look again, invalid order
 }
 }
 }
}
