#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <FS.h>
#include "SPIFFS.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Wire.h> 

const char* ssid = "ASUS_Guest1";
const char* password = "";

// TCP server at port 80 will respond to HTTP requests
WebServer server(80);

#define si7021Addr 0x40


// Use this calibration code in setup():
//uint16_t calData[5] = { 409, 3303, 436, 3147, 7 };
// tft.setTouch(calData);


#define MAX_JSON_DOC_ENTRIES 30
#define JSON_EXTRA_MEM 512

struct OptionPair { char *id; byte ptype; String val; byte bval; };
enum { STRING_PROP, BYTE_PROP };

OptionPair gridOptions[]={ 
  {"ercot_url", STRING_PROP, "http://www.ercot.com/content/cdr/html/hb_lz" },
  {"ercot_for", STRING_PROP, "http://www.ercot.com/content/cdr/html/YYYYMMDD_dam_spp" },
  {"stlmt_pnt", STRING_PROP, "LZ_NORTH" },
  {"grid_url", STRING_PROP,  "https://app.gogriddy.com:443/api/v1/insights/getnow"},
  {"grid_sha1", STRING_PROP, "75 79 81 57 B7 F2 55 EF E5 E2 75 5C 8D 00 CA 7B 71 0E 05 00" },
  {"grid_mid", STRING_PROP,  "1122334455"},
  {"grid_acct", STRING_PROP, "1122334455"},
  {"rt_ven", STRING_PROP,   "ER"},
  {"for_ven", STRING_PROP,  "ER"}
};
int gridEntries = (sizeof gridOptions/sizeof gridOptions[0]);

enum {
ERCOT_REALTIME_URL,
ERCOT_FORECAST_URL,
SETTLEMENT_POINT,
GRIDDY_URL,
GRIDDY_SHA1,
GRIDDY_METER_ID,
GRIDDY_ACCT_ID,
REALTIME_VENDOR,
FORECAST_VENDOR
};

OptionPair sysOptions[]={ 
  {"prc_lb",STRING_PROP, "Last" },
  {"c_lim", STRING_PROP, "C2" },
  {"prc_fc", BYTE_PROP, "", 1 },
  {"prel", BYTE_PROP, "", 1},
  {"pl_hrs", BYTE_PROP, "", 3 },
  {"pl_temp", BYTE_PROP, "", 2},
  {"pl_prc", BYTE_PROP, "", 30},
  {"eh", BYTE_PROP, "", 0},
  {"eh_temp", BYTE_PROP, "", 60},
  {"eh_difft", BYTE_PROP, "", 5},
  {"eh_prc", BYTE_PROP, "", 10},
  {"idle_mn", BYTE_PROP, "", 5},
  {"cyc_mn", BYTE_PROP, "", 10},
  {"cyc_mx", BYTE_PROP, "", 60},
  {"fan_c", BYTE_PROP, "",25},
  {"fan_pre", BYTE_PROP, "",2},
  {"fan_post", BYTE_PROP, "",2},
  {"c1_min", BYTE_PROP, "",69},
  {"c1_max", BYTE_PROP, "",74},
  {"c2_min", BYTE_PROP, "",67},
  {"c2_max", BYTE_PROP, "",76},
  {"c2_prc", BYTE_PROP, "",10},
  {"c3_min", BYTE_PROP, "",65},
  {"c3_max", BYTE_PROP, "",78},
  {"c3_prc", BYTE_PROP, "",15},
  {"c4_min", BYTE_PROP, "",60},
  {"c4_max", BYTE_PROP, "",81},
  {"c4_prc", BYTE_PROP, "",50}
};
int sysEntries = (sizeof sysOptions/sizeof sysOptions[0]);

enum {
PRICE_LOOKBACK,
RAMPDOWN_C_LIMIT,
PRICE_FORECAST_ENABLED,
PRICE_SPIKE_PRELOAD_ENABLED,
PRICE_PRELOAD_HOURS,
PRICE_PRELOAD_TEMP,
PRICE_PRELOAD_PRICE_LIMIT,
EMERGENCY_HEAT_ENABLED,
EMERGENCY_HEAT_TEMP_LIMIT,
EMERGENCY_HEAT_TEMP_DIFF_MIN,
EMERGENCY_HEAT_PRICE_LIMIT,
HVAC_IDLE_MIN_TIME,
HVAC_MIN_RUNTIME,
HVAC_MAX_RUNTIME,
HVAC_FAN_CYCLE_PERCENT,
HVAC_FAN_PRERUN_TIME,
HVAC_FAN_POSTRUN_TIME,
C1_MIN_TEMP,
C1_MAX_TEMP,
C2_MIN_TEMP,
C2_MAX_TEMP,
C2_PRICE_LIMIT,
C3_MIN_TEMP,
C3_MAX_TEMP,
C3_PRICE_LIMIT,
C4_MIN_TEMP,
C4_MAX_TEMP,
C4_PRICE_LIMIT
};


void setup(void)
{  
    Serial.begin(115200);

    Wire.begin();
    Wire.beginTransmission(si7021Addr);
    Wire.endTransmission();
    delay(300);

  

    loadPreferences("griddy", gridOptions, gridEntries );
    loadPreferences("griddy", sysOptions, sysEntries );


    // Connect to WiFi network
    WiFi.begin(ssid, password);
    Serial.println("");

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    setClock();
    
    if (!MDNS.begin("griddy")) {
        Serial.println("Error setting up MDNS responder!");
        while(1) {
            delay(1000);
        }
    }
    Serial.println("mDNS responder started");

    server.on("/getconfig", HTTP_GET, handleGetConfig);
    server.on("/saveconfig", HTTP_GET, handleSaveConfig);

    // Start TCP (HTTP) server
    server.onNotFound([]() {                              // If the client requests any URI
      if (!handleFileRead(server.uri()))                  // send it if it exists
         handleNotFound();
    });
    server.begin(); 
    
    Serial.println("TCP server started");

    // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);

    if(!SPIFFS.begin(true)){
      Serial.println("An Error has occurred while mounting SPIFFS");
      return;
    }
}


void loop(void)
{
   readTemp();
   get_ercot_prices();
   get_griddy_prices();
   server.handleClient();
 //  Serial.println("Done handling client..");
}

void readTemp()
{
  static unsigned long check_delay=60000;
  static unsigned long time_now = -check_delay;

  if ( millis() > time_now + check_delay )
  {
    time_now = millis();
    
  unsigned int data[2];
   
  Wire.beginTransmission(si7021Addr);
  //Send humidity measurement command
  Wire.write(0xF5);
  Wire.endTransmission();
  delay(100);
     
  // Request 2 bytes of data
  Wire.requestFrom(si7021Addr, 2);
  // Read 2 bytes of data to get humidity
  if(Wire.available() == 2)
  {
    data[0] = Wire.read();
    data[1] = Wire.read();
  }
     
  // Convert the data
  float humidity  = ((data[0] * 256.0) + data[1]);
  humidity = ((125 * humidity) / 65536.0) - 6;
 
  Wire.beginTransmission(si7021Addr);
  // Send temperature measurement command
  Wire.write(0xF3);
  Wire.endTransmission();
  delay(100);
     
  // Request 2 bytes of data
  Wire.requestFrom(si7021Addr, 2);
   
  // Read 2 bytes of data for temperature
  if(Wire.available() == 2)
  {
    data[0] = Wire.read();
    data[1] = Wire.read();
  }
 
  // Convert the data
  float temp  = ((data[0] * 256.0) + data[1]);
  float celsTemp = ((175.72 * temp) / 65536.0) - 46.85;
  float fahrTemp = celsTemp * 1.8 + 32;
    
  // Output data to serial monitor
  Serial.print("Humidity : ");
  Serial.print(humidity);
  Serial.println(" % RH");
  Serial.print("Celsius : ");
  Serial.print(celsTemp);
  Serial.println(" C");
  Serial.print("Fahrenheit : ");
  Serial.print(fahrTemp);
  Serial.println(" F");
  }
}


void setClock() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  Serial.print(F("Get NTP time: "));
  time_t nowSecs = time(nullptr);
  while (nowSecs < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    yield();
    nowSecs = time(nullptr);
  }

  Serial.println();
  struct tm timeinfo;
  gmtime_r(&nowSecs, &timeinfo);
  Serial.print(F("Current time: "));
  Serial.print(asctime(&timeinfo));
}



void loadPreferences(char *pname, OptionPair opts[], int num_opts )
{
      Preferences prefs;
      prefs.begin(pname, true);

      Serial.println("Loading prefs...");

    for (int i=0; i<num_opts; i++ )
    {
       String val = prefs.getString(opts[i].id,opts[i].val);  //Load saved values... or use app defaults.
       if ( opts[i].ptype==STRING_PROP )
        opts[i].val =val;
       else
        opts[i].bval = val.toInt();


       Serial.print("Preferences: ");
       Serial.print(opts[i].id);
       Serial.println(" = " + val);
    }

    prefs.end();
}

void savePreferences(char *pname, OptionPair opts[], int num_opts )
{
    Preferences prefs;
    prefs.begin(pname, false);

    Serial.println("Saving prefs...");

    for (int i=0; i<num_opts; i++ )
    {
        String val;
        if ( opts[i].ptype==STRING_PROP )
          val=opts[i].val;
       else
          val=String(opts[i].bval);
        
       prefs.putString(opts[i].id, val);  //savedvalues...

       Serial.print("Preferences: ");
       Serial.print(opts[i].id);
       Serial.println(" = " + opts[i].val);
    }
    prefs.end();
}


void updateOptionPairValue( OptionPair opts[], int num_opts, String name, String val )
{
  for ( int i=0; i< num_opts; i++ )
  {
    if ( name == opts[i].id )
    {
      Serial.print("Update option: ");
      Serial.println( name + " = " + val );
      if ( opts[i].ptype==STRING_PROP )
        opts[i].val =val;
      else
        opts[i].bval = val.toInt();
      return;
    }
  }
}

void updateAllOptionPairs( OptionPair opts[], int num_opts )
{
   for (uint8_t i = 0; i < server.args(); i++) {    
      updateOptionPairValue( opts, num_opts, server.argName(i), server.arg(i));    
  }  
  
  savePreferences("griddy", opts, num_opts );
}

void handleSaveConfig(){
   for (uint8_t i = 0; i < server.args(); i++)
      Serial.println("Save: " + server.arg(i));   

    if ( server.args() > 0 )
      if ( server.arg(0) == "grid" ){
        updateAllOptionPairs( gridOptions, gridEntries);  
      } 
      else if ( server.arg(0) == "sys" ){
        updateAllOptionPairs( sysOptions, sysEntries);  
      } 
       
  server.sendHeader("Location","/settings_updated.html"); 
  server.send(302,"No data");
}


String getConfigJson( OptionPair opts[], int num_opts ){

const size_t capacity = JSON_OBJECT_SIZE(MAX_JSON_DOC_ENTRIES)+JSON_EXTRA_MEM;
DynamicJsonDocument doc(capacity);

  for ( int i=0; i< num_opts; i++ )
  {
    Serial.print("Sending config: " );
    Serial.print( opts[i].id  );

    if ( opts[i].ptype==STRING_PROP )
    {
      Serial.println( " = " + opts[i].val );
      doc[ opts[i].id ] = opts[i].val.c_str();
    }else{
      Serial.print( " = " );
      Serial.println( opts[i].bval );
      doc[ opts[i].id ] = opts[i].bval;
    }
  }
  String result;
  serializeJson(doc, result);

  return result;
}


void handleGetConfig(){
  String json="";
  if ( server.args() > 0 )
    if ( server.arg(0) == "grid" ){
      json = getConfigJson( gridOptions, gridEntries); 
    } else if ( server.arg(0) == "sys" ){
      json = getConfigJson( sysOptions, sysEntries); 
    }

  server.sendHeader("Cache-Control","no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma","no-cache");
  server.sendHeader("Expires","0");
  server.send(200,"application/json", json );

  Serial.println("Sent Configs: ");
  Serial.println( json );

}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", message);
}

void get_griddy_prices()
{
  static unsigned long check_delay=120000;
  static unsigned long time_now = -check_delay;

  if ( millis() > time_now + check_delay )
  {
    time_now = millis();
    
    HTTPClient http;
    http.begin(gridOptions[GRIDDY_URL].val , const_cast<char *> (gridOptions[GRIDDY_SHA1].val.c_str()) );
    http.addHeader("Content-Type", "application/json");
    
    String postMessage = "{\"meterID\":\"";
    postMessage += gridOptions[GRIDDY_METER_ID].val; 
    postMessage += "\",\"memberID\":\"";
    postMessage += gridOptions[GRIDDY_ACCT_ID].val; 
    postMessage += "\",\"settlement_point\":\"";
    postMessage += gridOptions[SETTLEMENT_POINT].val; 
    postMessage += "\"}";
    Serial.println("Sending: " + postMessage );

    int httpCode = http.POST(postMessage);
    Serial.print("http result:");
    Serial.println(httpCode);

    if ( httpCode > 0 )
    {
    //  http.writeToStream(&Serial);
      String data;
      float price;

      Stream *stream = http.getStreamPtr();      

      do {
        data=stream->readStringUntil('\n');
        int idx;
        if ( (idx=data.indexOf("date\":")) != -1 )
        {
          Serial.print("Date: ");
          Serial.println( data.substring(idx+8,idx+28));
        }
        if ( (idx=data.indexOf("\"price_ckwh\":")) != -1 )
        {
          Serial.print("Price: ");
          price = data.substring(idx+15).toFloat();
          Serial.println( price,2 );
        }
      }
      while ( data.length() > 0 );
    }else{
      Serial.printf("[HTTPS] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  }
}



void get_ercot_prices() {

      static unsigned long check_delay=120000;
      static unsigned long time_now = -check_delay;

      if ( millis() > time_now + check_delay )
      {
        time_now = millis();
        char state=1, tdcnt=0;
        float price;
        int idx;
        
        HTTPClient http;
        http.begin(gridOptions[ERCOT_REALTIME_URL].val);

        //Serial.print("[HTTP] GET...\n");
        // start connection and send HTTP header
        int httpCode = http.GET();

        // httpCode will be negative on error
        if(httpCode > 0) {
            // HTTP header has been send and Server response header has been handled
            //Serial.printf("[HTTP] GET... code: %d\n", httpCode);

            // file found at server
            if(httpCode == HTTP_CODE_OK) {

                WiFiClient *respStream = http.getStreamPtr();

                String data;
                do {
                  
                  data=respStream->readStringUntil('\n');

                  if (state==2 && (idx=data.indexOf("tdLeft"))!=-1){
                      tdcnt--;
                      if ( tdcnt==0 ){
                         //Serial.print("Searching: ");
                         //Serial.print( data.substring( idx+8));
                         price = data.substring( idx+8).toFloat()/10.0;
                         break;
                      }
                  }
                    
                  if (state==1 && data.indexOf(gridOptions[SETTLEMENT_POINT].val)!=-1) { state=2; tdcnt=3; }

                  //Serial.print("*");
                  //Serial.println(data);
                  //Serial.print("*");
                }
                while ( data.length() > 0 );

                Serial.print("Ercot R/T Price: "); Serial.println(price,2);
            }
        } else {
            Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
        }

        http.end();
    }
}


String getContentType(String filename) { // convert the file extension to the MIME type
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){  // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.html";           // If a folder is requested, send the index file
  String contentType = getContentType(path);             // Get the MIME type
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){  // If the file exists, either as a compressed archive, or normal
    if(SPIFFS.exists(pathWithGz))                          // If there's a compressed version available
      path += ".gz";                                         // Use the compressed version
    File file = SPIFFS.open(path, "r");                    // Open the file
    size_t sent = server.streamFile(file, contentType);    // Send it to the client
    file.close();                                          // Close the file again
    Serial.println(String("\tSent file: ") + path);
    return true;
  }
  Serial.println(String("\tFile Not Found: ") + path);
  return false;                                          // If the file doesn't exist, return false
}
