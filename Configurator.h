#include <FS.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <SPIFFS.h>

#include <ArduinoJson.h> 
// #define DEBUG_DEV true
#define TRIGGER_PIN 4

// wifimanager can run in a blocking mode or a non blocking mode
// Be sure to know how to process loops with no delay() if using non blocking
bool wm_nonblocking = true; // change to true to use non blocking
bool Portal_Active = false;

WiFiManager wm; // global wm instance

/* CONFIGURATION PARAMETERS */
char teamId[4] = "145";
char tz[4] = "-5";
char brightness[4] = "90";
char serverAddress[81] = "http://soxmon.azurewebsites.net";

void ShowPortalInfo();


WiFiManagerParameter custom_teamId("teamId", "three digit team id", teamId, 3);
WiFiManagerParameter custom_brightness("brightness", "Brightness", brightness, 3);
WiFiManagerParameter custom_tz("tz", "Time Zone", tz, 3);
WiFiManagerParameter custom_serverAddress("serverAddress", "Server Address", serverAddress, 80);
WiFiManagerParameter custom_field; 

bool shouldSaveConfig = false;

void saveConfigCallback() {
  shouldSaveConfig = true;
}

String getParam(String name){
  //read parameter from server, for customhmtl input
  String value;
  if(wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}

void saveParamCallback(){
  Serial.println("saving config");
  DynamicJsonDocument json(1024);
  json["teamId"] = getParam("teamId");
  json["brightness"] = getParam("brightness");
  json["tz"] = getParam("tz");
  json["serverAddress"] = getParam("serverAddress");

  Serial.println("Params set");

//  matrix.stop();

  if(SPIFFS.begin()) {
    Serial.println("Mounted file system");
  }
  else
  {
    Serial.println("Could not mount file system.");
    return;
  }
  
  File configFile = SPIFFS.open("/config.json", "w");
  Serial.println("File opened.");
  if (!configFile) {
    Serial.println("failed to open config file for writing");
  }
  Serial.println("about to serialize");
  serializeJson(json, Serial);
  serializeJson(json, configFile);
  configFile.close();
  
//  matrix.begin();
  Serial.println("Done saving.");

  // Reload global params
  strcpy(teamId, json["teamId"]);
  char* ptr;
  Team_ID = strtol(teamId, &ptr, 10);
  strcpy(brightness, json["brightness"]);
  Brightness = max(min((int)strtol(brightness, &ptr, 10), 255),0);
  Serial.print("Brightness: "); Serial.println(Brightness);
  dma_display->setBrightness8(Brightness); //0-255

  //end save
}

boolean GetConfigFromFile()
{
   //clean FS, for testing
//  SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {

// DELETE FILE FOR TESTING
//      SPIFFS.remove("/config.json");
    
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);

        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if ( ! deserializeError ) {
          Serial.println("\nparsed json");
          strcpy(teamId, json["teamId"]);
          if(json.containsKey("brightness")) strcpy(brightness, json["brightness"]);
          else (strncpy(brightness, "89",3));
          strcpy(tz, json["tz"]);
          strcpy(serverAddress, json["serverAddress"]);
        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
      return true;
    }
  } else {
    Serial.println("failed to mount FS");
    return false;
  }
  return false;
  //end read
}

void SetupWifiManager()
{
//    wm.resetSettings(); // wipe settings

  custom_teamId.setValue(teamId, 3);
  custom_tz.setValue(tz, 3);
  custom_serverAddress.setValue(serverAddress, 80);
  Serial.print("Setting value for brightness: ");Serial.println(brightness);
  custom_brightness.setValue(brightness,3);
  
 // wm.addParameter(&custom_teamId);
  wm.addParameter(&custom_tz);
  wm.addParameter(&custom_serverAddress);
  wm.addParameter(&custom_brightness);

//  wm.setSaveConfigCallback(saveConfigCallback);

  // Setup menu
  std::vector<const char *> menu = {"wifi","info","param","sep","erase","restart","exit"};
  wm.setMenu(menu);

  const char* custom_select = "<br/><label for='teams'>Team</label><select name='teamId' id='teams'><optgroup label='American League'><option value='110'>Baltimore Orioles</option><option value='111'>Boston Red Sox</option><option value='145'>Chicago White Sox</option><option value='114'>Cleveland Guardians</option><option value='116'>Detroit Tigers</option><option value='117'>Houston Astros</option><option value='118'>Kansas City Royals</option><option value='108'>Los Angeles Angels</option><option value='142'>Minnesota Twins</option><option value='147'>New York Yankees</option><option value='133'>Oakland Athletics</option><option value='136'>Seattle Mariners</option><option value='139'>Tampa Bay Rays</option><option value='140'>Texas Rangers</option><option value='141'>Toronto Blue Jays</option></optgroup><optgroup label='National League'><option value='109'>Arizona Diamondbacks</option><option value='144'>Atlanta Braves</option><option value='112'>Chicago Cubs</option><option value='113'>Cincinnati Reds</option><option value='115'>Colorado Rockies</option><option value='119'>Los Angeles Dodgers</option><option value='146'>Miami Marlins</option><option value='158'>Milwaukee Brewers</option><option value='121'>New York Mets</option><option value='143'>Philadelphia Phillies</option><option value='134'>Pittsburgh Pirates</option><option value='135'>San Diego Padres</option><option value='137'>San Francisco Giants</option><option value='138'>St. Louis Cardinals</option><option value='120'>Washington Nationals</option></optgroup></select>";
  new (&custom_field) WiFiManagerParameter(custom_select); // custom html input
  wm.addParameter(&custom_field);
  
  if(wm_nonblocking) wm.setConfigPortalBlocking(false);

// This line crashes shit on setting up network.
  wm.setSaveParamsCallback(saveParamCallback);

  // set dark theme
  wm.setClass("invert");
  wm.setConfigPortalTimeout(90); // auto close configportal after n seconds

}

bool checkButton(){
  // check for button press
  if ( digitalRead(TRIGGER_PIN) == LOW ) {
    // poor mans debounce/press-hold, code not ideal for production
    delay(50);
    if( digitalRead(TRIGGER_PIN) == LOW ){
      Serial.println("Button Pressed");
      ShowPortalInfo();
      // still holding button for 3000 ms, reset settings, code not ideaa for production
      delay(5000); // reset delay hold
      if( digitalRead(TRIGGER_PIN) == LOW ){
        Serial.println("Button Held");
        Serial.println("Erasing Config, restarting");
        wm.resetSettings();
        ESP.restart();
      }
      
      // start portal w delay
      Serial.println("Starting config portal");
      wm.setConfigPortalTimeout(120);
      
      wm.startWebPortal(); 
      Portal_Active = true;
      Serial.println("Web portal started");

      return true;
      
    }
  }
  return false;
}
