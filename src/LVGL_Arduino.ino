#include <lvgl.h>
#include <lv_conf.h>
#include "credits.h"
// #include <misc/lv_log.h>

#include <TFT_eSPI.h>
#include <misc/lv_color.h>

#include <XPT2046_Touchscreen.h>

// Standard Libraries
// ----------------------------

#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>
#include <StreamUtils.h>
#include <string>
#include <sstream>
#include <iomanip>

// Touch Screen pins
// ----------------------------

// The CYD touch uses some non default
// SPI pins
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

#define LCD_BACK_LIGHT_PIN 21
// use first channel of 16 channels (started from zero)
#define LEDC_CHANNEL_0     0
// use 12 bit precission for LEDC timer
#define LEDC_TIMER_12_BIT  12
// use 5000 Hz as a LEDC base frequency
#define LEDC_BASE_FREQ     5000


SPIClass touchscreenSpi = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);
uint16_t touchScreenMinimumX = 200, touchScreenMaximumX = 3700, touchScreenMinimumY = 240,touchScreenMaximumY = 3800;

LV_FONT_DECLARE(full_font_5);
#define WIND_SYMBOL "\xEF\x9C\xAE"
#define TEMP_SYMBOL "\xEF\x8B\x88"

// For HTTPS requests
WiFiClient client;

// For brightness control
float brightness_percent = 1.0;

lv_indev_t * indev; //Touchscreen input device
uint32_t lastTick = 0;  //Used to track the tick timer
uint32_t lastHTTPTick = 0;  //Used to track the tick timer
uint32_t lastHTTPWeatherTick = 0;  //Used to track the tick timer
uint32_t lastBrightnessTick = 0;  //Used to track the tick timer
uint16_t wasDimmed = 0;

/*Change to your screen resolution*/
static const uint16_t screenWidth  = 320;
static const uint16_t screenHeight = 240;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[ screenWidth * screenHeight / 10 ];

TFT_eSPI tft = TFT_eSPI(screenWidth, screenHeight); /* TFT instance */

std::string* departures;
std::string* weather;

// visual
static lv_style_t style_title;
static lv_style_t style_container;
static lv_style_t style_icon;

static const lv_font_t * font_large;
static const lv_font_t * font_normal;

/* DEFINE FUNCTIONS*/
static void profile_create(lv_obj_t * parent, char * dep1, char * dep2);
void sl_metro_widget(std::string* depatures, std::string* weather);
std::string* makeWeatherRequest();
std::string* makeTrafficRequest();
std::string floatToStr(float value, std::string suffix);

/*---------------------------------------------------*/

#if LV_USE_LOG != 0
/* Serial debugging */
void my_print(const char * buf)
{
    Serial.printf(buf);
    Serial.flush();
}
#endif


void ledcAnalogWrite(uint8_t channel, uint32_t value, uint32_t valueMax = 255) {
  // calculate duty, 4095 from 2 ^ 12 - 1
  uint32_t duty = (4095 / valueMax) * min(value, valueMax);

  // write duty to LEDC
  ledcWrite(channel, duty);
}

/* Display flushing */
void my_disp_flush( lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p )
{
    uint32_t w = ( area->x2 - area->x1 + 1 );
    uint32_t h = ( area->y2 - area->y1 + 1 );

    tft.startWrite();
    tft.setAddrWindow( area->x1, area->y1, w, h );
    tft.pushColors( ( uint16_t * )&color_p->full, w * h, true );
    tft.endWrite();

    lv_disp_flush_ready( disp_drv );
}

/*Read the touchpad*/
void my_touchpad_read( lv_indev_drv_t * indev_drv, lv_indev_data_t * data )
{
    if(touchscreen.touched())
    {
        TS_Point p = touchscreen.getPoint();
        //Some very basic auto calibration so it doesn't go out of range
        if(p.x < touchScreenMinimumX) touchScreenMinimumX = p.x;
        if(p.x > touchScreenMaximumX) touchScreenMaximumX = p.x;
        if(p.y < touchScreenMinimumY) touchScreenMinimumY = p.y;
        if(p.y > touchScreenMaximumY) touchScreenMaximumY = p.y;
        //Map this to the pixel position
        data->point.x = map(p.x,touchScreenMinimumX,touchScreenMaximumX,1,screenWidth); /* Touchscreen X calibration */
        data->point.y = map(p.y,touchScreenMinimumY,touchScreenMaximumY,1,screenHeight); /* Touchscreen Y calibration */
        data->state = LV_INDEV_STATE_PR;

        ledcAnalogWrite(LEDC_CHANNEL_0, 255);
        lastBrightnessTick = millis();
        wasDimmed = 0;
    }
    else
    {
        data->state = LV_INDEV_STATE_REL;
    }
}



std::string* makeTrafficRequest() {

  // Opening connection to server (Use 80 as port if HTTP)
  if (!client.connect(SL_HOST, 80))
  {
    Serial.println(F("Connection failed"));
    return nullptr;
  }

  client.setTimeout(5000);

  // give the esp a breather
  yield();

  // Send HTTP request
  client.print(F("GET "));
  // This is the second half of a request (everything that comes after the base URL)
  client.print("/v1/sites/9264/departures?transport=METRO&direction=1&line=14&forecast=60"); // %2C == ,
  client.println(F(" HTTP/1.1"));

  //Headers
  client.print(F("Host: "));
  client.println(SL_HOST);

  client.println(F("Cache-Control: no-cache"));

  if (client.println() == 0)
  {
    Serial.println(F("Failed to send request"));
    return nullptr;
  }
  //delay(100);
  // Check HTTP status
  char status[32] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0)
  {
    Serial.print(F("Unexpected response: "));
    Serial.println(status);
    return nullptr;
  }

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders))
  {
    Serial.println(F("Invalid response"));
    return nullptr;
  }

  // while (client.available() && client.peek() != '{')
  // {
  //   char c = 0;
  //   client.readBytes(&c, 1);
  //   Serial.print(c);
  //   Serial.println("BAD");
  // }

  JsonDocument doc; //For ESP32/ESP8266 you'll mainly use dynamic.

  DeserializationError error = deserializeJson(doc, client);

  if (!error) {
    

    // Check if JSON paths exist and are strings before constructing std::string objects
    auto getStringFromJson = [](const JsonVariant& var) -> std::string {
        if (!var.isNull() && var.is<const char*>()) {
            return std::string(var.as<const char*>());
        }
        return std::string();
    };

    std::string departure_1 = getStringFromJson(doc["departures"][0]["display"]);
    std::string departure_2 = getStringFromJson(doc["departures"][1]["display"]);
    std::string departure_3 = getStringFromJson(doc["departures"][2]["display"]);

    departure_1 += " | " + getStringFromJson(doc["departures"][0]["destination"]);
    departure_2 += " | " + getStringFromJson(doc["departures"][1]["destination"]);
    departure_3 += " | " + getStringFromJson(doc["departures"][2]["destination"]);


    Serial.print("Depart 1: ");
    Serial.println(departure_1.c_str());
    Serial.print("Depart 2: ");
    Serial.println(departure_2.c_str());
    Serial.print("Depart 3: ");
    Serial.println(departure_3.c_str());

    // return array of departures
    static std::string departures[3]; // Static so it persists after the function returns
    departures[0] = departure_1;
    departures[1] = departure_2;
    departures[2] = departure_3;

    return departures;
    
  } else {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return nullptr;
  }
}

std::string* makeWeatherRequest() {

  // Opening connection to server (Use 80 as port if HTTP)
  if (!client.connect(WEATHER_HOST, 80))
  {
    Serial.println(F("Connection failed"));
    return nullptr;
  }

  client.setTimeout(5000);

  // give the esp a breather
  yield();

  LoggingStream loggingClient(client, Serial);
  // Send HTTP request
  loggingClient.print(F("GET "));
  // This is the second half of a request (everything that comes after the base URL)
  loggingClient.print("/v1/current.json?key=");
  loggingClient.print(WEATHER_API_KEY);
  loggingClient.print("&q=Stockholm&aqi=no"); 
  loggingClient.println(F(" HTTP/1.1"));

  //Headers
  loggingClient.print(F("Host: "));
  loggingClient.println(WEATHER_HOST);

  loggingClient.println(F("Cache-Control: no-cache"));

  if (client.println() == 0)
  {
    Serial.println(F("Failed to send request"));
    return nullptr;
  }
  delay(100);
  // Check HTTP status
  char status[32] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0)
  {
    Serial.print(F("Unexpected response: "));
    Serial.println(status);
    return nullptr;
  }

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders))
  {
    Serial.println(F("Invalid response"));
    return nullptr;
  }

  while (client.available() && client.peek() != '{')
  {
    char c = 0;
    loggingClient.readBytes(&c, 1);
    Serial.println("BAD");
  }

  JsonDocument doc; //For ESP32/ESP8266 you'll mainly use dynamic.
  ReadLoggingStream loggingStream(client, Serial);
  DeserializationError error = deserializeJson(doc, loggingStream);

  if (!error) {
    
    float temp_c = doc["current"]["temp_c"];
    float wind_kph = doc["current"]["wind_kph"];
    float feelslike_c = doc["current"]["feelslike_c"];

    std::string temp = floatToStr(temp_c, "°C") + "/" + floatToStr(feelslike_c, "°C");
    std::string wind_speed = floatToStr(wind_kph, " km/h");
    std::string updated = doc["current"]["last_updated"];

    Serial.println("Updated: ");
    Serial.println(updated.c_str());
    Serial.print("Tempature: ");
    Serial.println((temp).c_str());
    Serial.print("Wind speed: ");
    Serial.println(wind_speed.c_str());

    static std::string weather[3]; // Static so it persists after the function returns
    weather[0] = updated;
    weather[1] = temp.c_str();
    weather[2] = wind_speed.c_str();

    return weather;
    
  } else {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return nullptr;
  }
}

std::string floatToStr(float value, std::string suffix) {
  std::stringstream stream;
  stream << std::fixed << std::setprecision(0) << value;
  return stream.str() + suffix;
}

void setup()
{
  Serial.begin( 115200 ); /* prepare for possible serial debug */

  String LVGL_Arduino = "LVGL version ";
  LVGL_Arduino += String('V') + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch();

  Serial.println( LVGL_Arduino );

  lv_init();

  // #if LV_USE_LOG != 0
  //     lv_log_register_print_cb( my_print ); /* register print function for debugging */
  // #endif

  touchscreenSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS); /* Start second SPI bus for touchscreen */
  touchscreen.begin(touchscreenSpi); /* Touchscreen init */
  touchscreen.setRotation(1); /* Landscape orientation */

  tft.begin();          /* TFT init */
  tft.setRotation( 1 ); /* Landscape orientation */

  lv_theme_t * th = lv_theme_basic_init(lv_disp_get_default());
  lv_disp_set_theme(NULL, th);

  lv_disp_draw_buf_init( &draw_buf, buf, NULL, screenWidth * screenHeight / 10 );

  /*Initialize the display*/
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init( &disp_drv );
  /*Change the following line to your display resolution*/
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register( &disp_drv );

  // init brightness controll
  #if ESP_IDF_VERSION_MAJOR == 5
  ledcAttach(LCD_BACK_LIGHT_PIN, LEDC_BASE_FREQ, LEDC_TIMER_12_BIT);
  #else
    ledcSetup(LEDC_CHANNEL_0, LEDC_BASE_FREQ, LEDC_TIMER_12_BIT);
    ledcAttachPin(LCD_BACK_LIGHT_PIN, LEDC_CHANNEL_0);
  #endif
  lastBrightnessTick = millis();

  /*Initialize the (dummy) input device driver*/
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init( &indev_drv );
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_t * my_indev = lv_indev_drv_register( &indev_drv );

  // Connect to the WiFI
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(WIFI_SSID);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  

  departures = makeTrafficRequest();
  weather = makeWeatherRequest();

  lv_style_init(&style_title);
  lv_style_init(&style_container);
  lv_style_init(&style_icon);

  sl_metro_widget(departures, weather);
  
  //Done
  Serial.println( "Setup done" );

}


void loop()
{   
    lv_tick_inc(millis() - lastTick); //Update the tick timer. Tick is new for LVGL 9
    lastTick = millis();
    lv_timer_handler();               //Update the UI

    // Update metro departures every 30 seconds
    if (millis() - lastHTTPTick > 30000) {
      Serial.println("Updating traffic");
      lastHTTPTick = millis();
      departures = makeTrafficRequest();
      Serial.println("Traffic updated");
      sl_metro_widget(departures, weather);
      Serial.println("sl_metro_widget called after traffic update");
    }

    // Update weather every minute
    if (millis() - lastHTTPWeatherTick > 600000) {
      Serial.println("Updating weather");
      lastHTTPWeatherTick = millis();
      weather = makeWeatherRequest();
      Serial.println("Weather updated");
      sl_metro_widget(departures, weather);
      Serial.println("sl_metro_widget called after weather update");
    }

    // Serial.println( lastTick - lastBrightnessTick );
    // Dim screen if there is no activity
    if (millis() - lastBrightnessTick > 60000 && wasDimmed == 0) {
        Serial.println( "Dim screen" );
        ledcAnalogWrite(LEDC_CHANNEL_0, 0);
        wasDimmed = 1;
        Serial.println("Screen dimmed");
    }

    delay(5);
}



void sl_metro_widget(std::string* depatures, std::string* weather_data)
{
    Serial.print("Free heap before processing: ");
    Serial.println(ESP.getFreeHeap());
    font_large = &full_font_5;
    font_normal = &full_font_5;

    int32_t tab_h;
    tab_h = 45;

    // init styles
    // lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, lv_color_make(0, 0, 0));
    lv_style_set_text_font(&style_title, font_large);

    // lv_style_init(&style_container);
    lv_style_set_text_color(&style_container, lv_color_make(0, 0, 0));
    lv_style_set_text_font(&style_container, font_large);

    // lv_style_init(&style_icon);
    lv_style_set_border_width(&style_icon, 0);
    lv_style_set_text_font(&style_icon, font_large);

    lv_obj_t * row1 = lv_obj_create(lv_scr_act());
    lv_obj_set_size(row1, 320, 240);
    lv_obj_center(row1);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW_WRAP);

    lv_obj_t * obj;
    lv_obj_t * label;

    // ROW 1
    obj = lv_obj_create(row1);
    lv_obj_set_size(obj, 60, LV_SIZE_CONTENT);
    label = lv_label_create(obj);
    lv_label_set_text(label, "T14");
    lv_obj_add_style(label, &style_title, 0);
    lv_obj_set_style_bg_color(obj, lv_color_make(0, 255, 255), 0);
    lv_obj_center(label);

    obj = lv_obj_create(row1);
    lv_obj_set_size(obj, 220, LV_SIZE_CONTENT);
    lv_obj_add_style(obj, &style_container, 0);
    label = lv_label_create(obj);
    lv_label_set_text(label, depatures[0].c_str());
    lv_obj_center(label);

    // ROW 2
    obj = lv_obj_create(row1);
    lv_obj_set_size(obj, 60, LV_SIZE_CONTENT);
    label = lv_label_create(obj);
    lv_label_set_text(label, "T14");
    lv_obj_add_style(label, &style_title, 0);
    lv_obj_set_style_bg_color(obj, lv_color_make(0, 255, 255), 0);
    lv_obj_center(label);

    Serial.println("Done draw 2.1 row ");

    obj = lv_obj_create(row1);
    lv_obj_set_size(obj, 220, LV_SIZE_CONTENT);
    lv_obj_add_style(obj, &style_container, 0);
    Serial.println("Done draw 2.1 obj ");
    Serial.print("Get free heap before creating label: ");
    Serial.println(ESP.getFreeHeap());
    label = lv_label_create(obj);
    Serial.println("Done label");
    lv_label_set_text(label, depatures[1].c_str());
    Serial.println("Set text");
    lv_obj_center(label);

    Serial.println("Done draw 2 row ");

    // ROW 3
    obj = lv_obj_create(row1);
    lv_obj_set_size(obj, 60, LV_SIZE_CONTENT);
    label = lv_label_create(obj);
    lv_label_set_text(label, "T14");
    lv_obj_add_style(label, &style_title, 0);
    lv_obj_set_style_bg_color(obj, lv_color_make(0, 255, 255), 0);
    lv_obj_center(label);

    obj = lv_obj_create(row1);
    lv_obj_set_size(obj, 220, LV_SIZE_CONTENT);
    lv_obj_add_style(obj, &style_container, 0);
    label = lv_label_create(obj);
    lv_label_set_text(label, depatures[2].c_str());
    lv_obj_center(label);

    // Row 4 Weather
    
    std::string temp_str = TEMP_SYMBOL;
    temp_str += " " + weather_data[1];

    std::string wind_str = WIND_SYMBOL;
    wind_str += " " + weather_data[2];

    obj = lv_obj_create(row1);
    lv_obj_set_size(obj, 140, LV_SIZE_CONTENT);
    lv_obj_add_style(obj, &style_icon, 0);
    label = lv_label_create(obj);
    lv_label_set_text(label, temp_str.c_str());

    obj = lv_obj_create(row1);
    lv_obj_set_size(obj, 140, LV_SIZE_CONTENT);
    lv_obj_add_style(obj, &style_icon, 0);
    label = lv_label_create(obj);
    lv_label_set_text(label, wind_str.c_str());

    Serial.println("Done draw weather row ");

    Serial.print("Free heap after processing: ");
    Serial.println(ESP.getFreeHeap());
    Serial.println("sl_metro_widget: End");
    
    
}

// static void tabview_delete_event_cb(lv_event_t * e)
// {
//     Serial.println("Delete event ");
//     lv_event_code_t code = lv_event_get_code(e);

//     if(code == LV_EVENT_DELETE) {
//         lv_style_reset(&style_text_muted);
//         lv_style_reset(&style_title);
//         lv_style_reset(&style_icon);
//         lv_style_reset(&style_bullet);
//     }
// }

