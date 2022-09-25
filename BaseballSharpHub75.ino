#include <WiFi.h>

// #include <Adafruit_GFX.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
// #include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/TomThumb.h>
// #include <Fonts/Picopixel.h>
#include <time.h>
#include <ArduinoOTA.h>

#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "Icons.h"


#define TZ (-5*60*60)
#define UPDATE_SECONDS 10

#define R1 25
#define G1 26
#define BL1 27
#define R2 21
#define G2 22
#define BL2 23
#define CH_A 12
#define CH_B 16
#define CH_C 17
#define CH_D 18
#define CH_E -1 // assign to any available pin if using two panels or 64x64 panels with 1/32 scan
#define CLK 15
#define LAT 32
#define OE 33


/*--------------------- MATRIX PANEL CONFIG -------------------------*/
#define PANEL_RES_X 64      // Number of pixels wide of each INDIVIDUAL panel module. 
#define PANEL_RES_Y 32     // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 1      // Total number of panels chained one to another


//Another way of creating config structure
//Custom pin mapping for all pins
HUB75_I2S_CFG::i2s_pins _pins = {R1, G1, BL1, R2, G2, BL2, CH_A, CH_B, CH_C, CH_D, CH_E, LAT, OE, CLK};
HUB75_I2S_CFG mxconfig(
  64,   // width
  32,   // height
  1,   // chain length
  _pins,   // pin mapping
  HUB75_I2S_CFG::FM6126A      // driver chip
);


MatrixPanel_I2S_DMA *dma_display = nullptr;

// Module configuration
/*HUB75_I2S_CFG mxconfig(
  PANEL_RES_X,   // module width
  PANEL_RES_Y,   // module height
  PANEL_CHAIN    // Chain length
  );
*/

//mxconfig.gpio.e = -1; // Assign a pin if you have a 64x64 panel
//mxconfig.clkphase = false; // Change this if you have issues with ghosting.
//mxconfig.driver = HUB75_I2S_CFG::FM6126A; // Change this according to your pane.



// #include <FastLED.h>

int Team_ID = 0;
int Tz=0;
int Brightness = 80;

long Total_Api_Calls = 0;

#include "Configurator.h"
#include "SoxLogo.h"
#define SERVER_IP "http://soxmon.azurewebsites.net"

const char compile_date[] = __DATE__ " " __TIME__;

void setup() {

  Serial.begin(115200);
  Serial.println("BaseballSharpHub75.ino. Aug 2022. C.Crawford");
  Serial.println(compile_date);
  Serial.println(__FILE__);
  
  pinMode(TRIGGER_PIN, INPUT_PULLUP);


  // Check to see if SPIFFS exists and is formatted.

  // SPIFFS.format();
  if (!GetConfigFromFile())
  {
    Serial.println("Could not read configuration. Reset the file system and call for setup.");
  }


  Serial.print("Cur teamId: "); Serial.println(teamId);
  char buf[5];
  char* ptr;
  Team_ID = strtol(teamId, &ptr, 10);
  Brightness = max(min((int)strtol(brightness, &ptr, 10), 255), 0);
  Serial.print("Cur Brightness: "); Serial.println(Brightness);
  Serial.print("Cur Tz: "); Serial.println(Tz);
  Serial.print("Cur Server Address: "); Serial.println(serverAddress);

  /************** DISPLAY **************/
  // Setup early so we can display error messages and setup messages.

  Serial.println("...Starting Display");
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(Brightness); //0-255
  dma_display->clearScreen();
  dma_display->setFont(&TomThumb);
  dma_display->setCursor(1, 6);
  dma_display->printf("Welcome.");
  dma_display->setCursor(1, 12);
  dma_display->printf("ver:%s",__DATE__);
  

  SetupWifiManager();

  // Block if we can't get a WiFi connection. Will remove later.
  wm.setConfigPortalBlocking(true);

  wm.setConfigPortalTimeout(10);
  while (!wm.autoConnect("ScoreboardSetup"))
  {
    showSetupMessage();
    wm.setConfigPortalTimeout(90);
    Serial.println("Could not connect...");
    delay(1000);
  }

  // dma_display->clearScreen();
  

  Serial.println("Connected!");
  dma_display->setCursor(1, 18);
  dma_display->printf(WiFi.localIP().toString().c_str());

  //OTA SETUP

  ArduinoOTA.setHostname("BaseballSharpClient32");

  ArduinoOTA
  .onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()

    //    dma_display->stop();
    Serial.println("Start updating " + type);
  })
  .onEnd([]() {
    Serial.println("\nEnd");
  })
  .onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  })
  .onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();

  time_t t;
  configTime(Tz*3600, 0, "0.pool.ntp.org", "1.pool.ntp.org"); // enable NTP
  while(time(&t) < 200)
  {
    Serial.print(".");
    dma_display->setCursor(1, 18);
    dma_display->printf(".");
    delay(200);
  }

  struct tm *lt;

  time(&t);
  lt = localtime(&t);

  char tbuf[80];
  strftime(tbuf, 80, "%a, %b %d   %I:%M", lt);
  Serial.printf("Cur time: %s\n",tbuf);
  
  dma_display->setCursor(1, 24);
  dma_display->printf(tbuf);
  

  checkButton();


}

DynamicJsonDocument doc(3200);

void showError(char* message)
{
  Serial.println(message);
  dma_display->fillScreen(0);

  dma_display->setTextColor(dma_display->color444(15, 15, 15));
  dma_display->setFont(&TomThumb);
  dma_display->setCursor(1, 6);
  dma_display->printf(message);

  dma_display->setCursor(1, 20);
  dma_display->printf("IP:");
  dma_display->printf(WiFi.localIP().toString().c_str());

}

void showSetupMessage()
{
  dma_display->fillScreen(0);
  dma_display->setFont(&TomThumb);
  dma_display->setTextColor(dma_display->color444(6, 6, 15));
  dma_display->setCursor(0, 6);
  dma_display->printf("Welcome!");
  dma_display->setCursor(0, 12);
  dma_display->printf("Connect wifi to:");
  dma_display->setCursor(0, 20);
  dma_display->setTextColor(dma_display->color444(15, 2, 2));
  dma_display->printf("ScoreboardSetup");
  dma_display->setCursor(0, 28);
  dma_display->setTextColor(dma_display->color444(15, 15, 15));
  dma_display->printf("AP to configure.");
}

void showMessage(char* message)
{
  Serial.print("ShowMessage:"); Serial.println(message);
  dma_display->fillScreen(0);

  dma_display->setTextColor(dma_display->color444(15, 15, 15));
  dma_display->setFont(&TomThumb);
  dma_display->setCursor(0, 6);
  dma_display->printf(message);
}

void ShowPortalInfo()
{
  dma_display->fillScreen(0);
  dma_display->setFont(&TomThumb);
  dma_display->setTextColor(dma_display->color444(6, 6, 15));
  dma_display->setCursor(0, 6);
  dma_display->printf("Config Portal");
  dma_display->setCursor(0, 12);
  dma_display->printf("Open browser to:");
  dma_display->setCursor(0, 20);
  dma_display->setTextColor(dma_display->color444(15, 2, 2));
  dma_display->printf(WiFi.localIP().toString().c_str());
  dma_display->setCursor(0, 28);
  dma_display->setTextColor(dma_display->color444(15, 15, 15));
  dma_display->printf("Set team, etc.");
}

int ShowSoxLogo()
{
    dma_display->setFont(&TomThumb);
    dma_display->fillScreen(0);
    dma_display->setTextColor(dma_display->color444(15,15,15));
    dma_display->setCursor(6,6);
    dma_display->printf("top: [8 bot: ]9");
  return 0;
   static int prettyIndex = 0;
   prettyIndex = GetAllGameData("2022-08-02", prettyIndex, true);
   return prettyIndex;   

  /*
    for(unsigned long i = 0; i<17; i+=1)
    {
    dma_display->fillScreen(i);
    dma_display->setTextColor(dma_display->color444(0,0,0));
    dma_display->setCursor(6,6);
    dma_display->printf("%i",i);
    dma_display->swapBuffer();
    Serial.print(".");
    delay(200);
    }
  */
  /*
    const char * filename="/Astros.bmp";
    fs::File imgFile = SPIFFS.open(filename, "r");
    uint16_t buf[1164];
    int siz = imgFile.size();
    Serial.print("img file size:");Serial.println(siz);
    if(siz == 0) Serial.println("Image not found.");
    int i = 0;
    while(siz > 0) {
      // size_t len = min((int)(sizeof(buf) - 1), siz);
      //imgFile.read((uint16_t *)buf, len);
      uint16_t result;
      ((uint8_t *)&result)[0] = imgFile.read();
      ((uint8_t *)&result)[1] = imgFile.read();
      buf[i++] = result;
      Serial.print(result);
      siz = siz - 2;
      imgFile.close();
    }
    Serial.println(i);
    Serial.println(siz);

    dma_display->drawRGBBitmap(0,16,buf,16,16);
  */

  dma_display->fillScreen(0);
  dma_display->drawRGBBitmap(0, 0, (const uint16_t *)soxlogo, 58,32);
/*
  // dma_display->fillRect(2,0,16,16,dma_display->color444(15,4,4));
  dma_display->drawRGBBitmap(0, 0, (const uint16_t *)RoyalsLogo, 16, 16);
  //  dma_display->drawRGBBitmap(0,0,(const uint16_t *)AngelsIcon,32,32);
  // dma_display->fillRect(2,16,16,16,dma_display->color444(13,13,1));
  dma_display->drawRGBBitmap(0, 16, (const uint16_t *)WhiteSoxLogo, 16, 16);

  
  dma_display->setTextColor(dma_display->color444(14,14,14));
  dma_display->setFont(&TomThumb);

  // Away (Top) team
  dma_display->setCursor(18, 5);
  dma_display->printf("KC");

  // Home (Bot) team
  dma_display->setCursor(18,21);
  dma_display->printf("CWS");

  //Score and inning
  dma_display->setFont();
  
  dma_display->setCursor(18,6);
  dma_display->printf("2");

  dma_display->setCursor(18,22);
  dma_display->printf("1");

  //inning
  dma_display->setCursor(40,21);
  dma_display->printf("F");

  dma_display->drawLine(32,0,32,31,dma_display->color444(14,14,14));

  
  // Base runners

  dma_display->drawRGBBitmap(44,1, (const uint16_t*)RunnerOn,9,9);
  dma_display->drawRGBBitmap(38,7, (const uint16_t*)NoRunner,9,9);
  dma_display->drawRGBBitmap(50,7, (const uint16_t*)RunnerOn,9,9);
  // draw missing pixels
  dma_display->drawPixel(46,7,dma_display->color444(14,14,14));
  dma_display->drawPixel(50,7,dma_display->color444(14,14,14));

  // Top Bottom of inning
  // dma_display->drawBitmap(34,24,bm_TopInning,5,3,dma_display->color444(14,14,14));
  dma_display->drawBitmap(34,24,bm_BotInning,5,3,dma_display->color444(14,14,14));

  // Outs
  dma_display->drawBitmap(48,23,bm_NoOut,4,4,dma_display->color444(5,5,5));
  dma_display->drawBitmap(53,23,bm_NoOut,4,4,dma_display->color444(5,5,5));
  dma_display->drawRGBBitmap(58,23, (const uint16_t*)BitmapOut,4,4);

  time_t t;
  struct tm *lt;

  time(&t);
  lt = localtime(&t);

  char tbuf[80];
  strftime(tbuf, 80, "%a, %b %d   %I:%M", lt);
  dma_display->setFont(&TomThumb);
  // dma_display->printf(tbuf);
*/
}

void LogoTest(int index)
{
  dma_display->fillScreen(0);

  int i = index % 4;

  if (i == 0)
  {
    dma_display->drawRGBBitmap(0, 0, (const uint16_t *)AngelsLogo, 16, 16);
    dma_display->drawRGBBitmap(0, 16, (const uint16_t *)AthleticsLogo, 16, 16);
    dma_display->drawRGBBitmap(16, 0, (const uint16_t *)AstrosLogo, 16, 16);
    dma_display->drawRGBBitmap(16, 16, (const uint16_t *)BlueJaysLogo, 16, 16);
    dma_display->drawRGBBitmap(32, 0, (const uint16_t *)BravesLogo, 16, 16);
    dma_display->drawRGBBitmap(32, 16, (const uint16_t *)BrewersLogo, 16, 16);
    dma_display->drawRGBBitmap(48, 0, (const uint16_t *)CardinalsLogo, 16, 16);
    dma_display->drawRGBBitmap(48, 16, (const uint16_t *)CubsLogo, 16, 16);
  }
  else if (i == 1)
  {
    dma_display->drawRGBBitmap(0, 0, (const uint16_t *)DiamondbacksLogo, 16, 16);
    dma_display->drawRGBBitmap(0, 16, (const uint16_t *)DodgersLogo, 16, 16);
    dma_display->drawRGBBitmap(16, 0, (const uint16_t *)GuardiansLogo, 16, 16);
    dma_display->drawRGBBitmap(16, 16, (const uint16_t *)MarinersLogo, 16, 16);
    dma_display->drawRGBBitmap(32, 0, (const uint16_t *)MarlinsLogo, 16, 16);
    dma_display->drawRGBBitmap(32, 16, (const uint16_t *)MetsLogo, 16, 16);
    dma_display->drawRGBBitmap(48, 0, (const uint16_t *)NationalsLogo, 16, 16);
    dma_display->drawRGBBitmap(48, 16, (const uint16_t *)OriolesLogo, 16, 16);
  }
  else if (i == 2)
  {
    dma_display->drawRGBBitmap(0, 0, (const uint16_t *)PadresLogo, 16, 16);
    dma_display->drawRGBBitmap(0, 16, (const uint16_t *)PhilliesLogo, 16, 16);
    dma_display->drawRGBBitmap(16, 0, (const uint16_t *)PiratesLogo, 16, 16);
    dma_display->drawRGBBitmap(16, 16, (const uint16_t *)RangersLogo, 16, 16);
    dma_display->drawRGBBitmap(32, 0, (const uint16_t *)RaysLogo, 16, 16);
    dma_display->drawRGBBitmap(32, 16, (const uint16_t *)RedsLogo, 16, 16);
    dma_display->drawRGBBitmap(48, 0, (const uint16_t *)RedSoxLogo, 16, 16);
    dma_display->drawRGBBitmap(48, 16, (const uint16_t *)RockiesLogo, 16, 16);
  }
  else
  {
    dma_display->drawRGBBitmap(0, 0, (const uint16_t *)RoyalsLogo, 16, 16);
    dma_display->drawRGBBitmap(0, 16, (const uint16_t *)TigersLogo, 16, 16);
    dma_display->drawRGBBitmap(16, 0, (const uint16_t *)TwinsLogo, 16, 16);
    dma_display->drawRGBBitmap(16, 16, (const uint16_t *)WhiteSoxLogo, 16, 16);
    dma_display->drawRGBBitmap(32, 0, (const uint16_t *)YankeesLogo, 16, 16);
    dma_display->drawRGBBitmap(32, 16, (const uint16_t *)GiantsLogo, 16, 16);
  }

}




int mode = 2;





bool liveGameMode = false;
bool leagueIdle = false;  // League idle is when no games are in progress.
time_t nextGame;          // Time when league goes off idle.
time_t portalStartTime = 0;


void loop()
{

  static time_t last_t;
  int lastGame, nextUnplayedGame, gameInProgress;
  static int retryCount = 0;
  static int localDow = -1;
  static unsigned long lastMillis = 0;
  static time_t nextUpdateTime = 0;
  static int lastPrimaryTeam = Team_ID;
  bool refreshData = false;
  

  ArduinoOTA.handle();

  if (wm_nonblocking) wm.process(); // avoid delays() in loop when non-blocking and other long running code
  if (checkButton()) ShowPortalInfo();

  time_t t = time(NULL);
  if (last_t + UPDATE_SECONDS >= t)
  {
    // Scroll display if we're not changing pages.
    if (mode == 2 && (millis() - lastMillis) > 60)
    {
      ScrollStatus();
      lastMillis = millis();
    }
    return;
  }

  last_t = t;
  struct tm *lt = localtime(&t);

  // FIX THIS, doesn't seem to be ticking over.
  Serial.printf("today: %d",lt->tm_wday);
  if (lt->tm_wday != localDow) // Do daily updates
  {
    Serial.println("Need the daily update.");
    localDow = lt->tm_wday;
    // Update time from time server
    configTime(Tz*3600, 0, "0.pool.ntp.org", "1.pool.ntp.org");
    // Start showing tomorrow's games instead of today's. Force a refresh.
    nextUpdateTime = 0;
  }

  if (t >= nextUpdateTime || Team_ID != lastPrimaryTeam)
  {
    time_t secondsUntilUpdate = GetNextUpdateSeconds();
    nextUpdateTime = t + secondsUntilUpdate;
    refreshData = true;
    lastPrimaryTeam = Team_ID; // Refresh if the primary team changed in the UI.
    Serial.println("Time to update");
  }
  else
  {
    refreshData = false;
    Serial.print("Next update: "); Serial.println(nextUpdateTime - t);
  }

  char dtStr[32];
  strftime(dtStr, sizeof dtStr, "%Y-%m-%d", lt);


  Serial.print("Mode: "); Serial.println(mode);
  Serial.print("Refresh: "); Serial.println(refreshData);
  Serial.print("Total API calls: "); Serial.println(Total_Api_Calls);

  // TODO: Get us out of Mode 0 after a while.
  static int offset = 0;
  static int standingsIndex = 0;
  static int logoIndex = 0;
  static int prettyIndex = 0;

  switch (mode) {
    case 0: // Broken mode
      // START THE PORTAL.
      if (!wm.getWebPortalActive())
      {
        wm.setConfigPortalTimeout(120);
        wm.startWebPortal();
        Portal_Active = true;
      }

      Serial.println("Web portal started");

      Serial.print("In broken mode. Server address: ");
      Serial.println(serverAddress);
      if (retryCount++ > 10) {
        mode = 2;
        retryCount = 0;
      }

      break;
    case 1: // Previous game result
      ShowPrevBoxScore(refreshData);
      mode++;
      break;

    case 2: // Game in progress or about to start
      // ShowFullScreen returns true if we're in a game. stop the rotation.
      if (!ShowLiveBoxScore(refreshData)) mode++;
      break;

    case 3: // Current standings

      GetAllStandings(standingsIndex++ % 6, refreshData);
      //ShowFullScreenStandings(Team_ID, refreshData);
      mode++;
      break;
    case 4: // Next game
      ShowNextGameInfo(Team_ID, refreshData);
      mode++;
      break;
    case 5:
      if (!ShowLeagueGames(dtStr, offset, refreshData)) //Returns number of rows remaining.
      {
        offset = 0;
        mode++;
      }
      else offset += 4;
      break;
    case 6: //
      if (Portal_Active)
      {
        ShowPortalInfo();
        // Increment counter. Turn off portal after a while.
        if (portalStartTime == 0)
        {
          portalStartTime = time(NULL);
        }
        if (time(NULL) - portalStartTime > 120)
        {
          portalStartTime = 0;
          wm.stopWebPortal();
          Serial.println("Stopping web portal. It's been running too long.");
        }
        
      }
      else
      {
//        ShowSoxLogo();

//I        if(ShowSoxLogo() == 0) mode++;
        GetAllGameData(dtStr, prettyIndex, refreshData);
      }
      mode++;
      break;
    case 7:
      //      LogoTest(logoIndex++);

      GetWildcardStandings(0, refreshData);
      mode++;
      break;
    case 8:
      //      LogoTest(logoIndex++);

      GetWildcardStandings(1, refreshData);
      mode++;
      break;
    case 9:
      prettyIndex = GetAllGameData(dtStr, prettyIndex, refreshData);
      mode = 1;
      break;
    case 100:
      wm.setConfigPortalTimeout(120);
      
      wm.startWebPortal(); 
      Portal_Active = true;
      
      Serial.println("Web portal started");

      

      prettyIndex = GetAllGameData(dtStr, prettyIndex, refreshData);
      if (prettyIndex == 0) { 
        Serial.println("Done with pp");
        mode = 1;
      }
      else
      {
        Serial.print("PrettyIndex: "); Serial.println(prettyIndex);
      }
      break;
    default:
      mode = 1;
     

  }

 // return;

}

int GetDocFromServer(char* query)
{
  HTTPClient http;
  Serial.print(query);
  http.begin(query);
  int httpResponseCode = http.GET();
  Serial.print("Back from Get: "); Serial.println(httpResponseCode);

  if (httpResponseCode != 200)
  {
    char sbuf[30];
    sprintf(sbuf, "Svr Error: %d",httpResponseCode);
    showError(sbuf);
    if (httpResponseCode == -1)
    {
      Serial.print("The server isn't available or is misconfigured.");
      mode = 0;
    }
    return 1;
  }

  String response = http.getString();
  http.end();
  Total_Api_Calls++;

  DeserializationError error = deserializeJson(doc, response);
  if (error) {
    Serial.println(error.f_str());
    showError("No data back");
    return 1;
  }

  return 0;
}

int GetJsonFromServer(char* query, DynamicJsonDocument* docPtr)
{
  HTTPClient http;
  Serial.print(query);
  http.begin(query);
  int httpResponseCode = http.GET();
  Serial.print("Back from Get: "); Serial.println(httpResponseCode);
  Total_Api_Calls++;
  if (httpResponseCode != 200)
  {
    char sbuf[30];
    sprintf(sbuf, "Svr Error: %d",httpResponseCode);
    showError(sbuf);if (httpResponseCode == -1)
    {
      Serial.print("The server isn't available or is misconfigured.");
      mode = 0;
    }
    return 1;
  }

  String response = http.getString();
  http.end();

  DeserializationError error = deserializeJson(*docPtr, response);
  if (error) {
    Serial.println(error.f_str());
    showError("No data back");
    return 1;
  }

  return 0;
}

int GetIntFromServer(char* query)
{
  HTTPClient http;
  Serial.print("GIFS: "); Serial.println(query);
  Total_Api_Calls++;
  http.begin(query);
  int httpResponseCode = http.GET();
  Serial.print("Back from Get: "); Serial.println(httpResponseCode);
  if (httpResponseCode != 200)
  {
    char errMsg[80];
    sprintf(errMsg, "Srvr error: %d", httpResponseCode);
    showError(errMsg);
    if (httpResponseCode == -1)
    {
      Serial.print("The server isn't available or is misconfigured.");
      mode = 0;
    }
    return 0;
  }

  String response = http.getString();
  Serial.println(response);
  http.end();

  return response.toInt();

}

String statusText = "";

bool ScrollStatus()
{
  // if (statusText = "") return false;

  static int i = 63;
  int msgLen = statusText.length();
  dma_display->setTextWrap(false);
  dma_display->setCursor(i, 31);
  dma_display->writeFillRect(0, 25, 64, 7, 0);
  dma_display->setTextColor(dma_display->color444(15, 15, 15));

  char statusBuf[400];
  statusText.toCharArray(statusBuf, 399);
  dma_display->printf(statusBuf);
  i--;
  if (i < -(msgLen * 4)) i = 63;
  return true;
}

int GetNextUpdateSeconds()
{
  char buf[80];
  sprintf(buf, "%s/api/TimeForNextUpdate?teamId=%d", serverAddress, Team_ID);
  int secondsToUpdate = GetIntFromServer(buf);
  Serial.print("Back from TimeForNextUpdate"); Serial.println(secondsToUpdate);
  return secondsToUpdate;
}

int GetLastGame(int teamId)
{
  char buf[80];
  sprintf(buf, "%s/api/GetLastGameId/%d", serverAddress, teamId);
  int gameId = GetIntFromServer(buf);
  Serial.print("Back from GetLastGame "); Serial.println(gameId);
  return gameId;
}

int GetNextGame(int teamId)
{
  char buf[80];
  sprintf(buf, "%s/api/GetNextGameId/%d", serverAddress, teamId);
  int gameId = GetIntFromServer(buf);
  Serial.print("Back from GetNextGame "); Serial.println(gameId);
  return gameId;
}

int GetNextUnplayedGame(int teamId)
{
  char buf[80];
  sprintf(buf, "%s/api/GetNextScheduledGame/%d", serverAddress, teamId);
  int gameId = GetIntFromServer(buf);
  Serial.print("Back from Unplayed Game"); Serial.println(gameId);
  return gameId;
}

int GetCurrentGame(int teamId)
{
  // Service returns 0 if no current game, else return the gamePk
  char buf[80];
  sprintf(buf, "%s/api/GetGameInProgress/%d", serverAddress, teamId);
  int gameId = GetIntFromServer(buf);
  Serial.print("Back from GetCurrentGame "); Serial.println(gameId);
  return gameId;
}

// Pretty Game
bool DisplayPrettyGame(JsonVariant value)
{

  const char* awayA = value["awayAbbr"];
  const char* homeA = value["homeAbbr"];
  int homeId = value["homeId"];
  int awayId = value["awayId"];
  const char* gameStatus = value["state"];
  int inning = value["inning"];
  int homeScore = value["homeScore"];
  int awayScore = value["awayScore"];
  time_t gameTime = value["unixTime"];
  bool isTop = value["isTop"];
  int outs = value["outs"];
  bool onFirst = value["on1"];
  bool onSecond = value["on2"];
  bool onThird = value["on3"];

  dma_display->fillScreen(0);

  // dma_display->fillRect(2,0,16,16,dma_display->color444(15,4,4));
  dma_display->drawRGBBitmap(0, 0, getLogo(awayId), 16, 16);
  //  dma_display->drawRGBBitmap(0,0,(const uint16_t *)AngelsIcon,32,32);
  // dma_display->fillRect(2,16,16,16,dma_display->color444(13,13,1));
  dma_display->drawRGBBitmap(0, 16, getLogo(homeId), 16, 16);

  
  dma_display->setTextColor(dma_display->color444(14,14,14));
  dma_display->setFont(&TomThumb);

  // Away (Top) team
  dma_display->setCursor(18, 5);
  dma_display->printf(awayA);

  // Right Align code
  int16_t  x1, y1;
  uint16_t w, h;
  dma_display->getTextBounds(homeA,18,5,&x1,&y1,&w,&h);
  char sbuf[40];
  sprintf(sbuf,"%s x1:%d, y1:%d, w:%d, h:%d",homeA,x1,y1,w,h);
  Serial.println(sbuf);

  // Home (Bot) team
  dma_display->setCursor(18,21);
  dma_display->printf(homeA);

  dma_display->drawLine(32,0,32,31,dma_display->color444(7,7,7));


  // Display based on current game state

  // Pre game show game time

  // During game show stats

  if(!strncmp(gameStatus,"S",1) || !strncmp(gameStatus,"P",1))
  {
    // Show game start time.
    char tbuf[6];
    strftime(tbuf, sizeof(tbuf), "%I:%M", localtime(&gameTime));
    Serial.printf("GameTime: %d",gameTime);
    struct tm * timeinfo;
    timeinfo = (localtime(&gameTime));
    Serial.printf("Timeinfo: %s",asctime(timeinfo));
    Serial.printf("LocalTime: %s",tbuf);
    dma_display->setCursor(34,21);
    dma_display->setFont();
    if (!strncmp(tbuf, "0", 1)) dma_display->printf(tbuf + 1);
    else dma_display->printf(tbuf);
    return false;
  }

  //Score and inning
  dma_display->setFont();
  
  dma_display->setCursor(18,6);
  dma_display->printf("%d",awayScore);

  dma_display->setCursor(18,22);
  dma_display->printf("%d",homeScore);

  // Post game show final and clear outs, runners.
  if(!strncmp(gameStatus,"F",1))
  {
    dma_display->setCursor(34,21);
    dma_display->printf("Final");
    return false;
  }
  if(!strncmp(gameStatus,"D",1))
  {
    dma_display->setCursor(34,21);
    dma_display->setFont(&TomThumb);

    dma_display->printf("Delayed");
    return false;
  }


  //inning
  dma_display->setCursor(40,21);
  dma_display->printf("%d",inning);


  
  // Base runners

  dma_display->drawRGBBitmap(44,1, onSecond ? RunnerOn : NoRunner ,9,9);
  dma_display->drawRGBBitmap(38,7, onThird ? RunnerOn : NoRunner,9,9);
  dma_display->drawRGBBitmap(50,7, onFirst ? RunnerOn : NoRunner,9,9);
  // draw missing pixels
  dma_display->drawPixel(46,7,dma_display->color444(14,14,14));
  dma_display->drawPixel(50,7,dma_display->color444(14,14,14));

  // Top Bottom of inning
  dma_display->drawBitmap(34,24,isTop ? bm_TopInning : bm_BotInning,5,3,dma_display->color444(14,14,14));
  // dma_display->drawBitmap(34,24,bm_BotInning,5,3,dma_display->color444(14,14,14));

  // Outs ... could be done more cleanly.
  if(outs>0) dma_display->drawRGBBitmap(58,23, (const uint16_t*)BitmapOut,4,4);
  else dma_display->drawBitmap(58,23,bm_NoOut,4,4,dma_display->color444(5,5,5));
  if(outs>1) dma_display->drawRGBBitmap(53,23, (const uint16_t*)BitmapOut,4,4);
  else dma_display->drawBitmap(53,23,bm_NoOut,4,4,dma_display->color444(5,5,5));
  if(outs>2) dma_display->drawRGBBitmap(48,23, (const uint16_t*)BitmapOut,4,4);
  else dma_display->drawBitmap(48,23,bm_NoOut,4,4,dma_display->color444(5,5,5));
  
//  dma_display->drawBitmap(48,23,bm_NoOut,4,4,dma_display->color444(5,5,5));
//  dma_display->drawBitmap(53,23,bm_NoOut,4,4,dma_display->color444(5,5,5));
  
  return true;
}

// GET ALL GAME DATA

int GetAllGameData(char * dateString, int index, bool needRefresh)
{
  static DynamicJsonDocument doc(6144);
  int retVal = 0;
  static bool gamesInProgress = false;
  static bool gamesAllDone = true;
  static time_t nextGameStart = LONG_MAX; //
  char buf[80];
  sprintf(buf, "%s/api/GetAllGameResults/%s", serverAddress, dateString);
  if (doc.isNull() || needRefresh) // never refresh for subsequent pages if we have data.
  { //Save a call or two.
    GetJsonFromServer(buf, &doc);
  }

  JsonArray arr = doc.as<JsonArray>();

  if (arr.size() == 0 || arr.size() <= index)
  {
    return 0;
  }
  
  JsonVariant value = arr[index];
  DisplayPrettyGame(value);
  if(index>= (arr.size()-1)) return 0;
  else return index + 1;
}


//
// LEAGUE SCOREBOARD
// returns number of games still to show.
//

//AL: RED NL:BLUE

int ShowLeagueGames(char * dateString, int offset, bool needRefresh)
{
  static DynamicJsonDocument resultsDoc(3200);
  int retVal = 0;
  static bool gamesInProgress = false;
  static bool gamesAllDone = true;
  static time_t nextGameStart = LONG_MAX; //
  char buf[80];
  sprintf(buf, "%s/api/GetGamesForDate/%s", serverAddress, dateString);
  // GetDocFromServer(buf);
  // If we're on a later page, no need to make another call.
  // if we don't have cached results, get them.
  if (resultsDoc.isNull() || (needRefresh && offset == 0)) // never refresh for subsequent pages if we have data.
  { //Save a call or two.
    GetJsonFromServer(buf, &resultsDoc);
  }

  // Add check for no games.

  JsonArray arr = resultsDoc.as<JsonArray>();

  if (arr.size() == 0)
  {
    dma_display->fillScreen(0);
    dma_display->setTextColor(dma_display->color444(8, 15, 8));
    dma_display->setFont(&TomThumb);
    dma_display->setCursor(3, 12);
    dma_display->printf("No games today.");
    Serial.println("No games today.");
    return 0;
  }

  dma_display->fillScreen(0);
  dma_display->setTextColor(dma_display->color444(15, 15, 15));
  dma_display->setFont(&TomThumb);

  int index = 0;
  int row = 1;
  int col = 0;
  int colOffset = 32;

  for (JsonVariant value : arr)
  {
    if (index++ < offset) continue; //Burn the rows leading to offset.
    if (row >= 5) {
      retVal++;
      continue;
    }

    const char* awayA = value["awayAbbr"];
    const char* homeA = value["homeAbbr"];
    const char* gameStatus = value["gameStatus"];
    const char* inning = value["inning"];
    int score;
    bool homeWinning = false;
    bool awayWinning = false;
    time_t gameTime = value["gameTimeUnix"];

    // Check to see if scoreboard is live.
    // If there's a game in progress it's live.
    // If no games are live, restart at the time of the first game.
    // If all games are F, no need to update for rest of day.
    if (!strncmp(gameStatus, "I", 1))
    {
      gamesInProgress = true;
    }

    if (strncmp(gameStatus, "F", 1)) // If all the games aren't final
    {
      gamesAllDone = false;
    }
    // When's the next game to start.
    if ((!strncmp(gameStatus, "P", 1) || !strncmp(gameStatus, "S", 1)) && gameTime < nextGameStart)
    {
      nextGameStart = gameTime;
    }



    Serial.print(awayA); Serial.println(homeA);

    if (!strncmp(gameStatus, "F", 1) || !strncmp(gameStatus, "I", 1) || !strncmp(gameStatus, "O", 1))
    {
      if (value["awayTeamRuns"] > value["homeTeamRuns"]) awayWinning = true;
      if (value["awayTeamRuns"] < value["homeTeamRuns"]) homeWinning = true;
    }

    if (awayWinning) dma_display->setTextColor(dma_display->color444(0, 15, 0));
    else if (homeWinning) dma_display->setTextColor(dma_display->color444(15, 0, 0));
    else dma_display->setTextColor(dma_display->color444(15, 15, 15));
    dma_display->setCursor(0 + (col * colOffset), row * 6);
    dma_display->printf(awayA);

    if (homeWinning) dma_display->setTextColor(dma_display->color444(0, 15, 0));
    else if (awayWinning) dma_display->setTextColor(dma_display->color444(15, 0, 0));
    else dma_display->setTextColor(dma_display->color444(15, 15, 15));
    dma_display->setCursor(0 + (col * colOffset), (row + 1) * 6);
    dma_display->printf(homeA);

    dma_display->setTextColor(dma_display->color444(15, 15, 15));




    int shiftInning = 0;
    if (value["homeTeamRuns"] != nullptr && strncmp(gameStatus, "P", 1)) //Pre-game status has runs, but I don't like to show them.
    {
      dma_display->setCursor(15 + (col * colOffset), row * 6);
      score = value["awayTeamRuns"];
      itoa(score, buf, 10);
      dma_display->printf(buf);
      dma_display->setCursor(15 + (col * colOffset), (row + 1) * 6);
      score = value["homeTeamRuns"];

      itoa(score, buf, 10);
      dma_display->printf(buf);
      if (value["awayTeamRuns"] > 9 || value["homeTeamRuns"] > 9) shiftInning = 1;
    }

    // If game is delayed, show Del for inning
    if(!strncmp(gameStatus,"D",1))
    {
      dma_display->setCursor(13 + (col * colOffset), (row * 6) + 3);
      dma_display->printf("Del");
    }
    // Show game time if game not started.
    else if (value["homeTeamRuns"] == nullptr || !strncmp(inning, "n", 1) || !strncmp(inning, "d", 1))
    {
      if (!strncmp(inning, "d", 1)) dma_display->setTextColor(dma_display->color444(15, 15, 0));
      else if (!strncmp(inning, "n", 1)) dma_display->setTextColor(dma_display->color444(15, 8, 4));

      dma_display->setCursor(13 + (col * colOffset), (row * 6) + 3);
      char tbuf[6];
      strftime(tbuf, sizeof(tbuf), "%I:%M", localtime(&gameTime));
      if (!strncmp(tbuf, "0", 1)) dma_display->printf(tbuf + 1);
      else dma_display->printf(tbuf);
    }
    else
    {
      dma_display->setCursor(20 + (col * colOffset) + (shiftInning * 3), (row * 6) + 3);
      dma_display->printf(inning);
    }

    if (col) {
      col = 0;
      row += 3;
    }
    else {
      col++;
    }
  }
  //  dma_display->swapBuffer();

  //  sprintf(buf, "%i-%i",value["wins"].as<int>(), value["losses"].as<int>());
  return retVal;

}

void ShowNextGameInfo(int teamId, bool forceRefresh)
{
  //  Check to see if next game has changed based on date of next game. refresh as needed.
  //

  static DynamicJsonDocument doc(512);  // From the json assistant.
  time_t gameTimeUnix;
  char buf[80];
  sprintf(buf, "%s/api/GetUpcomingGameInfoForTeam/%d", serverAddress, teamId);

  if (!doc.isNull() || forceRefresh)
  {
    Serial.println("Document has content.");
    gameTimeUnix = doc["gameTimeUnix"];
    time_t curTime;
    time(&curTime);
    if (curTime > gameTimeUnix)
    {
      Serial.println("Need update to next game.");
      GetJsonFromServer(buf, &doc);
    }
    else
    {
      Serial.println("Reuse old data.");
      Serial.print("refresh in: "); Serial.print(gameTimeUnix - curTime);
    }
  }
  else
  {
    Serial.println("First Fetch.");
    GetJsonFromServer(buf, &doc);
  }

  const char* awayA = doc["awayAbbr"];
  const char* homeA = doc["homeAbbr"];
  const char* statusB = doc["statusBlurb"];
  const char* gameStatus = doc["gameStatus"];
  const char* homePitcher = doc["homePitcher"];
  const char* awayPitcher = doc["awayPitcher"];
  const char* gameTime = doc["gameTime"];
  gameTimeUnix = doc["gameTimeUnix"];


  dma_display->fillScreen(0);
  dma_display->setTextColor(dma_display->color444(15, 15, 15));
  dma_display->setFont();
  dma_display->setCursor(1, 1);
  dma_display->printf("%s @ %s", awayA, homeA);

  dma_display->setFont(&TomThumb);

  dma_display->setCursor(3, 16);
  dma_display->setTextColor(dma_display->color444(1, 15, 1));

  static const char* const wd[7] = {"Sun", "Mon", "Tue", "Wed", "Thr", "Fri", "Sat"};
  struct tm *tm;
  tm = localtime(&gameTimeUnix);

  dma_display->printf("%s at %d:%02d", wd[tm->tm_wday], tm->tm_hour % 12, tm->tm_min);

  dma_display->setCursor(1, 25);
  dma_display->setTextColor(dma_display->color444(0, 15, 8));
  dma_display->printf(homePitcher);
  dma_display->setCursor(1, 31);
  dma_display->setTextColor(dma_display->color444(0, 15, 8));
  dma_display->printf(awayPitcher);

}

void DisplayFullScreenStandings(JsonDocument& doc)
{
  JsonArray arr = doc.as<JsonArray>();

  dma_display->fillScreen(0);
  dma_display->setTextColor(dma_display->color444(15, 15, 15));
  dma_display->setFont(&TomThumb);

  int row = 0;

  for (JsonVariant team : arr)
  {
    if (team["teamId"] == Team_ID) dma_display->setTextColor(dma_display->color444(14, 0, 0));
    else dma_display->setTextColor(dma_display->color444(15, 15, 12));
    dma_display->setCursor(1, ++row * 6);
    dma_display->printf(team["divisionRank"].as<char*>());
    dma_display->printf(".");
    dma_display->setCursor(9, row * 6);
    dma_display->printf(team["teamName"].as<char*>());
    dma_display->setCursor(23, row * 6);
    char buf[8];
    sprintf(buf, "%i-%i", team["wins"].as<int>(), team["losses"].as<int>());
    dma_display->printf(buf);
    dma_display->setCursor(46, row * 6);
    dma_display->printf(team["gamesBack"].as<char*>());
  }

}

void DisplayFullScreenStandings(JsonDocument& doc, int index)
{
  Serial.println("Get division name from doc.");
  // Serial.println(doc[index].["teams"].size());
  Serial.println(doc.isNull() ? "Doc is null" : "Doc is not null");
  const char* divisionName = doc[index]["divisionName"];
  Serial.println(divisionName);


  JsonArray teams = doc[index]["teams"];
  Serial.print("teams size: "); Serial.println(teams.size());
  dma_display->fillScreen(0);
  dma_display->setTextColor(dma_display->color444(15, 15, 15));
  dma_display->setFont(&TomThumb);

  int row = 0;

  for (JsonVariant team : teams)
  {
    // Division name in upper right.
    dma_display->setTextColor(dma_display->color444(9, 3, 15));
    dma_display->setCursor(51, 6);
    dma_display->printf(doc[index]["divisionAbbr"].as<char*>());

    if (team["teamId"] == Team_ID) dma_display->setTextColor(dma_display->color444(14, 0, 0));
    else dma_display->setTextColor(dma_display->color444(15, 15, 12));
    dma_display->setCursor(1, ++row * 6);
    dma_display->printf(team["divisionRank"].as<char*>());
    dma_display->printf(".");
    dma_display->setCursor(9, row * 6);
    dma_display->printf(team["nameAbbr"].as<char*>());
    dma_display->setCursor(23, row * 6);
    char buf[8];
    sprintf(buf, "%i-%i", team["wins"].as<int>(), team["losses"].as<int>());
    dma_display->printf(buf);
    dma_display->setCursor(46, row * 6);
    if (strncmp((team["divisionRank"].as<char*>()), "1", 1)) dma_display->printf(team["gamesBack"].as<char*>());
  }
}

void DisplayWildcardStandings(JsonDocument& doc, int index)
{

  Serial.println(doc.isNull() ? "Doc is null" : "Doc is not null");
  const char* dispName = doc[index]["leagueName"];
  Serial.println(dispName);


  JsonArray teams = doc[index]["teams"];
  Serial.print("teams size: "); Serial.println(teams.size());
  dma_display->fillScreen(0);
  dma_display->setTextColor(dma_display->color444(15, 15, 15));
  dma_display->setFont(&TomThumb);

  int row = 0;

  // League name in upper right. American League blue, National League red
  if(!strncmp(dispName,"AL",2)) dma_display->setTextColor(dma_display->color444(13,0,0));
  else dma_display->setTextColor(dma_display->color444(4,4,13));
  dma_display->setCursor(47, 6);
  dma_display->printf("%s", dispName);
  dma_display->setCursor(47, 12);
  dma_display->printf("Wild");
  dma_display->setCursor(47, 18);
  dma_display->printf("Card");

  for (JsonVariant team : teams)
  {
    row++; //row number is 1 for first row on screen, not 0.
    if (row > 5) continue; //Only room for five rows.

    // Highlight team of interest
    if (team["teamId"] == Team_ID) dma_display->setTextColor(dma_display->color444(14, 0, 0));
    else dma_display->setTextColor(dma_display->color444(15, 15, 12));

    dma_display->setCursor(1, row * 6);
    dma_display->printf(team["wildCardRank"].as<char*>());
    dma_display->printf(".");

    dma_display->setCursor(9, row * 6);
    dma_display->printf(team["nameAbbr"].as<char*>());
    dma_display->setCursor(23, row * 6);

    dma_display->printf(team["wildCardGamesBack"].as<char*>());

    dma_display->setCursor(46, row * 6);
  }
}

void GetWildcardStandings(int index, bool forceUpdate)
{
  static DynamicJsonDocument doc(4096);
  char buf[80];
  sprintf(buf, "%s/api/WildcardStandings/", serverAddress);

  if (doc.isNull() || forceUpdate)
  {
    Serial.println("Fetch Wildcard Standings");
    GetJsonFromServer(buf, &doc);
  }
  else
  {
    Serial.println("Reusing old results");
  }

  DisplayWildcardStandings(doc, index);

}

void GetAllStandings(int index, bool forceUpdate)
{
  static DynamicJsonDocument doc(8192);
  char buf[80];
  sprintf(buf, "%s/api/GetStandings/", serverAddress);

  if (doc.isNull() || forceUpdate)
  {
    Serial.println("Fetch Standings");
    GetJsonFromServer(buf, &doc);
  }
  else
  {
    Serial.println("Reusing old results");
  }

  DisplayFullScreenStandings(doc, index);

  /*

    JsonObjectConst div0  = doc[0].as<JsonObject()>;
    Serial.println("Show Full Screen standings for 0");

    DisplayFullScreenStandings(div0);
  */
}

void ShowFullScreenStandings(int teamId, bool forceUpdate)
{
  static DynamicJsonDocument doc(1024);
  char buf[80];
  sprintf(buf, "%s/api/GetStandings/%d", serverAddress, teamId);

  if (doc.isNull() || forceUpdate)
  {
    Serial.println("Fetch Standings");
    GetJsonFromServer(buf, &doc);
  }
  else
  {
    Serial.println("Reusing old results");
  }

  DisplayFullScreenStandings(doc);
  return;
}

bool ShowPrevBoxScore(bool forceRefresh)
{

  int gamePk;
  bool retVal = false;
  char buf[80];
  static DynamicJsonDocument doc(2048);


  if (doc.isNull() || forceRefresh)
  {
    int gamePk = GetLastGame(Team_ID);
    sprintf(buf, "%s/api/GetBoxScore/%d", serverAddress, gamePk);

    Serial.println("Fetch BoxScore");
    GetJsonFromServer(buf, &doc);
  }
  else
  {
    Serial.println("Reusing old results");
  }

  DisplayBoxScore(doc);
}

bool ShowLiveBoxScore(bool needRefresh)
{
  bool retVal = false;
  char buf[80];
  static DynamicJsonDocument doc(2048);
  static int gameInProgress = 0;
  static bool gameActive = false;

  //TODO need to fix the overnight problem.

  if (!needRefresh) return false; //If there are no live games, then bail.

  if (!gameInProgress) //If there's a call for a refresh and we don't have the game, then update the game.
  {
    gameInProgress = GetCurrentGame(Team_ID);
    Serial.print("Game in progress gamePk: "); Serial.println(gameInProgress);
  }
  if ((needRefresh || doc.isNull()) && gameInProgress > 0)
  {
    Serial.println("Update Live Box Score Data");
    sprintf(buf, "%s/api/GetBoxScore/%d", serverAddress, gameInProgress);
    GetJsonFromServer(buf, &doc);
    int retVal = DisplayBoxScore(doc);
    return retVal;
  }
  return false;
}

bool DisplayBoxScore(const JsonDocument& doc)
{
  bool retVal = false;
  char buf[80];

  const char* awayA = doc["awayAbbr"];
  const char* homeA = doc["homeAbbr"];
  const char* statusB = doc["statusBlurb"];
  const char* awayL = doc["awayLineScore"] | "";
  const char* homeL = doc["homeLineScore"] | "";
  const char* gameStatus = doc["gameStatus"];
  const char* pitcher = doc["pitcher"];
  const char* batter = doc["batter"];
  const char* inningHalf = doc["inningHalf"] | "";
  const char* inningState = doc["inningState"] | "X";

  const char* lastComment = doc["lastComment"];

  bool manOnFirst = doc["manOnFirst"];
  bool manOnSecond = doc["manOnSecond"];
  bool manOnThird = doc["manOnThird"];

  int outs  = doc["outs"];
  int awayR = doc["awayteamRunsGame"];
  int homeR = doc["hometeamRunsGame"];
  int awayH = doc["awayteamHitsGame"];
  int homeH = doc["hometeamHitsGame"];
  int awayE = doc["awayteamErrorsGame"];
  int homeE = doc["hometeamErrorsGame"];

  dma_display->setTextWrap(false);
  sprintf(buf, "%s @ %s %s", awayA, homeA, statusB);


  dma_display->fillScreen(0);

  dma_display->setTextColor(dma_display->color444(15, 15, 15));
  dma_display->setFont(&TomThumb);
  dma_display->setCursor(0, 6);

  Serial.println(buf);
  dma_display->printf(buf);

  dma_display->setCursor(0, 16);
  if (!strncmp(gameStatus, "I", 1) && !strncmp(inningHalf, "T", 1)) dma_display->setTextColor(dma_display->color444(12, 13, 15));
  else dma_display->setTextColor(dma_display->color444(15, 14, 12));
  dma_display->printf(awayA);
  dma_display->setCursor(14, 16);
  dma_display->printf(awayL);

  dma_display->setCursor(0, 22);
  if (!strncmp(gameStatus, "I", 1) && !strncmp(inningHalf, "B", 1)) dma_display->setTextColor(dma_display->color444(12, 13, 15));
  else dma_display->setTextColor(dma_display->color444(15, 14, 12));
  dma_display->printf(homeA);

  dma_display->setCursor(14, 22);
  dma_display->printf(homeL);


    // Display outs as squares if game in progress
  if (!strncmp(gameStatus, "I", 1))
  {
    for(int i=3; i>0; i--)
    {
      dma_display->fillRect(53+(i*-3),3,2,2,(outs>=i)?dma_display->color444(15,15,0):dma_display->color444(3,3,3));
    }
  }

  
  // Only print runs, hits, errors if the game is in progress or done. This is getting sloppy.

  if (!strncmp(gameStatus, "I", 1) || !strncmp(gameStatus, "F", 1) || !strncmp(gameStatus, "O", 1))
  {


    
    char ibuf[3];
    dma_display->setCursor((awayR > 9) ? 49 : 53, 16);
    dma_display->setTextColor(dma_display->color444(15, 0, 0));
    itoa(awayR, ibuf, 10);
    dma_display->printf(ibuf);
    dma_display->setTextColor(dma_display->color444(15, 12, 12));
    dma_display->setCursor((awayH > 9) ? 57 : 61, 16);
    itoa(awayH, ibuf, 10);
    dma_display->printf(ibuf);

    dma_display->setTextColor(dma_display->color444(15, 0, 0));
    dma_display->setCursor((homeR > 9) ? 49 : 53, 22);
    itoa(homeR, ibuf, 10);
    dma_display->printf(ibuf);

    dma_display->setTextColor(dma_display->color444(15, 12, 12));
    dma_display->setCursor((homeH > 9) ? 57 : 61, 22);
    itoa(homeH, ibuf, 10);
    dma_display->printf(ibuf);

  }

  Serial.println(gameStatus);

  if (!strncmp(gameStatus, "I", 1))
  {
    if (strncmp(inningState, "E", 1) && strncmp(inningState, "M", 1)) //Not the end or Middle of inning.
    {
      retVal = true; //Only true if in progress and not in a break. TODO figure out if in a break between innings.
    }
    char *ln = strrchr(pitcher, ' ') + 1;

    dma_display->setTextColor(dma_display->color444(7, 7, 7));
    dma_display->setCursor(0, 31);

    // dma_display->printf(lastComment);
    statusText = lastComment;
    Serial.println(statusText);
    /*
        dma_display->printf("P:%s",ln);
        char *ab = strrchr(batter, ' ') + 1;
        dma_display->printf(" AB:%s", ab);
    */

    //Show BaseRunners
    static int emptyBase = dma_display->color444(3, 8, 15);
    static int onBase = dma_display->color444(15, 0, 0);

    dma_display->drawPixel(61, 5, emptyBase);
    dma_display->drawPixel(63, 3, manOnFirst ? onBase : emptyBase);
    dma_display->drawPixel(61, 1, manOnSecond ? onBase : emptyBase);
    dma_display->drawPixel(59, 3, manOnThird ? onBase : emptyBase);

  }
  else if (!strncmp(gameStatus, "F", 1))
  {
    Serial.println("game final");
  }
  // dma_display->swapBuffer(); // display the image written to the buffer

  return retVal;
}
