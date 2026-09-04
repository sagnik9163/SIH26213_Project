/*
  ESP32 OLED Table Clock + Telegram Reminder Bot (U8g2 version)
  ---------------------------------------------------------------
  - Shows live clock (NTP synced) on a 1.3" 128x64 I2C OLED (SH1106 driver)
  - Listens to a Telegram bot for commands:
      /remind HH:MM Your reminder text
      /list            -> lists all pending reminders
      /clear           -> clears all reminders
      /clear N         -> clears reminder number N (1-indexed, from /list)
  - When the clock hits a reminder's time, it flashes the reminder
    on the OLED and (optionally) buzzes.

  Libraries needed (Library Manager):
    - U8g2 (by oliver / olikraus)
    - UniversalTelegramBot (Brian Lough)
    - ArduinoJson (v6.x)

  NOTE ON DRIVER CHIP:
    Most 1.3" I2C OLEDs use the SH1106 driver even though they look
    identical to 0.96" SSD1306 boards. This sketch defaults to SH1106.
    If your display shows garbled/shifted output, your board is
    probably SSD1306-based instead — swap the constructor below for:
      U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

  Wiring:
    OLED VCC -> 3.3V   OLED GND -> GND
    OLED SDA -> GPIO21 OLED SCL -> GPIO22
    Buzzer (optional) -> GPIO 4 (through a transistor/resistor as needed)
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <time.h>

// ------------------- USER CONFIG -------------------
const char* WIFI_SSID     = "OnePlus Nord CE 3 Lite 5G";
const char* WIFI_PASSWORD = "w5cuke7u";

#define BOT_TOKEN   "8720491965:AAGHPhu5pmooJj2_cB4Lic2TfE4mNDHBb5Y"
#define CHAT_ID     "-1004468259020"   // your personal chat or channel id

const char* NTP_SERVER    = "pool.ntp.org";
const long  GMT_OFFSET_S  = 5 * 3600 + 1800;  // e.g. IST = UTC+5:30. Adjust for your timezone.
const int   DST_OFFSET_S  = 0;

const int BUZZER_PIN = 25;          // set to -1 if you don't have a buzzer
const unsigned long BOT_POLL_MS = 2000;   // how often to check Telegram (ms)
// -----------------------------------------------------

// Full-buffer mode (F) is easiest to work with; use _1_ (page buffer) if you
// hit RAM pressure once WiFi + TLS + this are all running together.
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

struct Reminder {
  int hour;
  int minute;
  String text;
  bool firedToday;   // prevents re-firing every second during the same minute
};

const int MAX_REMINDERS = 15;
Reminder reminders[MAX_REMINDERS];
int reminderCount = 0;

unsigned long lastBotPoll = 0;
unsigned long lastClockRefresh = 0;

// Active alert being shown on screen (if any)
String activeAlertText = "";
unsigned long alertShownAt = 0;
const unsigned long ALERT_DURATION_MS = 15000; // show alert for 15s

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  if (BUZZER_PIN >= 0) pinMode(BUZZER_PIN, OUTPUT);

  Wire.begin(21, 22);
  u8g2.begin();
  u8g2.setBusClock(400000);   // faster I2C; drop to 100000 if display glitches
  showMessage("Connecting WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());

  secured_client.setInsecure(); // simplest option; for production, pin Telegram's root cert

  configTime(GMT_OFFSET_S, DST_OFFSET_S, NTP_SERVER);
  showMessage("Syncing time...");
  struct tm timeinfo;
  int retries = 0;
  while (!getLocalTime(&timeinfo) && retries < 20) {
    delay(500);
    retries++;
  }

  bot.sendMessage(CHAT_ID, "ESP32 clock is online! Send /remind HH:MM your text", "");
  showMessage("Ready!");
  delay(1000);
}

// ---------- Main loop ----------
void loop() {
  unsigned long now = millis();

  // Poll Telegram periodically (non-blocking-ish)
  if (now - lastBotPoll > BOT_POLL_MS) {
    lastBotPoll = now;
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      handleTelegramMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
  }

  checkReminders();

  // Refresh display ~once a second
  if (now - lastClockRefresh > 1000) {
    lastClockRefresh = now;
    if (activeAlertText != "" && (now - alertShownAt < ALERT_DURATION_MS)) {
      drawAlertScreen();
    } else {
      activeAlertText = "";
      drawClockScreen();
    }
  }
}

// ---------- Telegram command handling ----------
void handleTelegramMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;
    text.trim();

    // Only respond to your configured chat/channel for safety
    if (chat_id != String(CHAT_ID)) continue;

    if (text.startsWith("/remind")) {
      // format: /remind HH:MM Some text here
      String rest = text.substring(7);
      rest.trim();
      int spaceIdx = rest.indexOf(' ');
      if (spaceIdx == -1) {
        bot.sendMessage(chat_id, "Usage: /remind HH:MM Your text", "");
        continue;
      }
      String timeStr = rest.substring(0, spaceIdx);
      String msgText = rest.substring(spaceIdx + 1);
      int colonIdx = timeStr.indexOf(':');
      if (colonIdx == -1) {
        bot.sendMessage(chat_id, "Time must be HH:MM, e.g. 14:30", "");
        continue;
      }
      int h = timeStr.substring(0, colonIdx).toInt();
      int m = timeStr.substring(colonIdx + 1).toInt();

      if (reminderCount >= MAX_REMINDERS) {
        bot.sendMessage(chat_id, "Reminder list full. Use /clear to free space.", "");
        continue;
      }
      reminders[reminderCount].hour = h;
      reminders[reminderCount].minute = m;
      reminders[reminderCount].text = msgText;
      reminders[reminderCount].firedToday = false;
      reminderCount++;

      bot.sendMessage(chat_id, "Reminder set for " + timeStr + ": " + msgText, "");
    }
    else if (text == "/list") {
      if (reminderCount == 0) {
        bot.sendMessage(chat_id, "No reminders set.", "");
      } else {
        String out = "Reminders:\n";
        for (int r = 0; r < reminderCount; r++) {
          out += String(r + 1) + ". " + pad2(reminders[r].hour) + ":" + pad2(reminders[r].minute)
                 + " - " + reminders[r].text + "\n";
        }
        bot.sendMessage(chat_id, out, "");
      }
    }
    else if (text == "/clear") {
      reminderCount = 0;
      bot.sendMessage(chat_id, "All reminders cleared.", "");
    }
    else if (text.startsWith("/clear ")) {
      int idx = text.substring(7).toInt() - 1;
      if (idx >= 0 && idx < reminderCount) {
        for (int r = idx; r < reminderCount - 1; r++) reminders[r] = reminders[r + 1];
        reminderCount--;
        bot.sendMessage(chat_id, "Reminder removed.", "");
      } else {
        bot.sendMessage(chat_id, "Invalid reminder number.", "");
      }
    }
    else if (text == "/start" || text == "/help") {
      bot.sendMessage(chat_id,
        "Commands:\n/remind HH:MM text - set a reminder\n/list - show reminders\n/clear - clear all\n/clear N - clear reminder N",
        "");
    }
  }
}

// ---------- Reminder checking ----------
void checkReminders() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  // Reset "firedToday" flags at midnight
  static int lastDay = -1;
  if (timeinfo.tm_mday != lastDay) {
    lastDay = timeinfo.tm_mday;
    for (int r = 0; r < reminderCount; r++) reminders[r].firedToday = false;
  }

  for (int r = 0; r < reminderCount; r++) {
    if (!reminders[r].firedToday &&
        reminders[r].hour == timeinfo.tm_hour &&
        reminders[r].minute == timeinfo.tm_min) {
      reminders[r].firedToday = true;
      activeAlertText = reminders[r].text;
      alertShownAt = millis();
      bot.sendMessage(CHAT_ID, "Reminder: " + reminders[r].text, "");
      if (BUZZER_PIN >= 0) buzz();
    }
  }
}

void buzz() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);
    digitalWrite(BUZZER_PIN, LOW);
    delay(150);
  }
}

// ---------- Display (U8g2) ----------
void drawClockScreen() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  char dateStr[16];
  sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
  char countStr[16];
  sprintf(countStr, "%d reminder(s)", reminderCount);

  u8g2.clearBuffer();

  // Top-left: reminder count
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, countStr);

  // Big centered time
  u8g2.setFont(u8g2_font_logisoso24_tf);   // large digits, good for a clock
  int w = u8g2.getStrWidth(timeStr);
  u8g2.drawStr((128 - w) / 2, 45, timeStr);

  // Date under it
  u8g2.setFont(u8g2_font_6x10_tf);
  w = u8g2.getStrWidth(dateStr);
  u8g2.drawStr((128 - w) / 2, 62, dateStr);

  u8g2.sendBuffer();
}

void drawAlertScreen() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(0, 12, "** REMINDER **");
  u8g2.drawHLine(0, 16, 128);

  // Wrap the reminder text across a couple of lines (simple word wrap)
  u8g2.setFont(u8g2_font_6x10_tf);
  drawWrappedText(activeAlertText, 0, 30, 128, 10);

  u8g2.sendBuffer();
}

// Very simple word-wrap helper for U8g2 (no built-in wrap support)
void drawWrappedText(String text, int x, int startY, int maxWidth, int lineHeight) {
  String line = "";
  int y = startY;
  int start = 0;
  while (start < (int)text.length()) {
    int spaceIdx = text.indexOf(' ', start);
    String word = (spaceIdx == -1) ? text.substring(start) : text.substring(start, spaceIdx);
    String testLine = line.length() ? (line + " " + word) : word;
    if (u8g2.getStrWidth(testLine.c_str()) > maxWidth && line.length()) {
      u8g2.drawStr(x, y, line.c_str());
      y += lineHeight;
      line = word;
    } else {
      line = testLine;
    }
    if (spaceIdx == -1) break;
    start = spaceIdx + 1;
  }
  if (line.length()) u8g2.drawStr(x, y, line.c_str());
}

void showMessage(const char* msg) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 35, msg);
  u8g2.sendBuffer();
}

String pad2(int val) {
  return (val < 10 ? "0" : "") + String(val);
}
