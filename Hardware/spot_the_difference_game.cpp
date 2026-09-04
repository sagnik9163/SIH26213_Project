#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

// ==========================================
// 1. PIN DEFINITIONS & HARDWARE
// ==========================================
#define OLED_SDA 21
#define OLED_SCL 22

#define BTN_UP_PIN     18 
#define BTN_DOWN_PIN   23 
#define BTN_LEFT_PIN   26 
#define BTN_RIGHT_PIN  27 
#define BTN_SELECT_PIN 19 

#define BUZZER_PIN     25
#define RANDOM_SEED_PIN 34 

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

// ==========================================
// 2. PIXEL-ART ICONS (16x16 Bitmaps)
// ==========================================
// 1. Smiley Face
const unsigned char icon_smiley[] PROGMEM = {
  0xe0, 0x07, 0x18, 0x18, 0x04, 0x20, 0x02, 0x40, 0xc2, 0x43, 0x02, 0x40, 0x02, 0x40, 0x22, 0x44, 
  0x22, 0x44, 0x12, 0x48, 0x0a, 0x50, 0x04, 0x20, 0x18, 0x18, 0xe0, 0x07, 0x00, 0x00, 0x00, 0x00
};
// 2. Heart
const unsigned char icon_heart[] PROGMEM = {
  0x00, 0x00, 0x38, 0x1c, 0x7c, 0x3e, 0xfe, 0x7f, 0xfe, 0x7f, 0xfe, 0x7f, 0xfc, 0x3f, 0xf8, 0x1f, 
  0xf0, 0x0f, 0xe0, 0x07, 0xc0, 0x03, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
// 3. Star
const unsigned char icon_star[] PROGMEM = {
  0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0xc0, 0x03, 0xcc, 0x33, 0xfe, 0x7f, 0xfe, 0x7f, 0xfc, 0x3f, 
  0xf8, 0x1f, 0xf0, 0x0f, 0xe0, 0x07, 0xe0, 0x07, 0xc0, 0x03, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00
};
// 4. House
const unsigned char icon_house[] PROGMEM = {
  0x80, 0x01, 0xc0, 0x03, 0xe0, 0x07, 0xf0, 0x0f, 0xf8, 0x1f, 0xfc, 0x3f, 0xfe, 0x7f, 0xff, 0xff, 
  0x02, 0x40, 0x02, 0x40, 0x02, 0x40, 0xfa, 0x5f, 0xfa, 0x5f, 0xfa, 0x5f, 0xfe, 0x7f, 0x00, 0x00
};
// 5. Music Note
const unsigned char icon_music[] PROGMEM = {
  0x00, 0x03, 0x00, 0x05, 0x00, 0x09, 0x00, 0x11, 0x00, 0x21, 0x00, 0x41, 0x00, 0x41, 0x00, 0x41, 
  0x00, 0x41, 0x00, 0x41, 0x38, 0x41, 0x7c, 0x01, 0x7c, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00
};
// 6. Coffee Cup
const unsigned char icon_cup[] PROGMEM = {
  0x00, 0x00, 0xfc, 0x3f, 0x04, 0x20, 0x04, 0x20, 0x04, 0x22, 0x04, 0x22, 0x04, 0x22, 0x04, 0x22, 
  0x04, 0x21, 0x08, 0x11, 0x10, 0x08, 0x20, 0x04, 0xc0, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Create an array that holds all our icons so we can easily select them by number
const unsigned char* iconSet[] = {icon_smiley, icon_heart, icon_star, icon_house, icon_music, icon_cup};
const int iconSetSize = 6;

// ==========================================
// 3. BUTTON DEBOUNCING (Rate-Limiter)
// ==========================================
#define DEBOUNCE_DELAY 200 

struct Button {
  const uint8_t pin;
  bool state;
  bool lastState;
  unsigned long lastDebounceTime;
};

Button btnUp     = {BTN_UP_PIN, HIGH, HIGH, 0};
Button btnDown   = {BTN_DOWN_PIN, HIGH, HIGH, 0};
Button btnLeft   = {BTN_LEFT_PIN, HIGH, HIGH, 0};
Button btnRight  = {BTN_RIGHT_PIN, HIGH, HIGH, 0};
Button btnSelect = {BTN_SELECT_PIN, HIGH, HIGH, 0};

bool isButtonPressed(Button &b) {
  bool pressed = false;
  int reading = digitalRead(b.pin);
  if (reading == LOW && b.lastState == HIGH) {
    if ((millis() - b.lastDebounceTime) > DEBOUNCE_DELAY) {
      pressed = true;
      b.lastDebounceTime = millis(); 
    }
  }
  b.lastState = reading; 
  return pressed;
}

// ==========================================
// 4. AUDIO SYSTEM 
// ==========================================
void beepClick() { tone(BUZZER_PIN, 800, 50); }
void beepSuccess() {
  tone(BUZZER_PIN, 1200, 150); delay(150);
  tone(BUZZER_PIN, 1600, 250); 
}
void beepIncorrect() { tone(BUZZER_PIN, 400, 300); }

// ==========================================
// 5. GAME VARIABLES & STATES
// ==========================================
enum GameState { STATE_START, STATE_MEMORIZE, STATE_GUESS, STATE_FEEDBACK };
GameState currentState = STATE_START;
bool needsRedraw = true; 

int currentLevel = 1;
int patternLength = 3;
const int MAX_PATTERN_LENGTH = 6; // Fits perfectly on screen

// We now use integers instead of chars to track which icon is in which slot
int originalPattern[10];
int modifiedPattern[10];
int changedIndex = 0;
int cursorIndex = 0; 

// ==========================================
// 6. TRUE RANDOM PATTERN GENERATOR
// ==========================================
void generatePattern() {
  // 1. Generate a completely fresh sequence of icon numbers
  for (int i = 0; i < patternLength; i++) {
    originalPattern[i] = random(0, iconSetSize);
    modifiedPattern[i] = originalPattern[i]; // Copy it over
  }

  // 2. Pick ONE random spot to change
  changedIndex = random(0, patternLength);

  // 3. Change it to a guaranteed DIFFERENT icon
  int newIcon;
  do {
    newIcon = random(0, iconSetSize);
  } while (newIcon == originalPattern[changedIndex]);
  
  modifiedPattern[changedIndex] = newIcon;
}

// ==========================================
// 7. ARDUINO SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  pinMode(BTN_UP_PIN, INPUT_PULLUP);
  pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_LEFT_PIN, INPUT_PULLUP);
  pinMode(BTN_RIGHT_PIN, INPUT_PULLUP);
  pinMode(BTN_SELECT_PIN, INPUT_PULLUP);

  randomSeed(analogRead(RANDOM_SEED_PIN));
  u8g2.begin();
}

// ==========================================
// 8. MAIN GAME LOOP
// ==========================================
void loop() {
  
  switch (currentState) {

    // ------------------------------------------------
    // STATE 1: START SCREEN
    // ------------------------------------------------
    case STATE_START:
      if (needsRedraw) {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.drawStr(12, 25, "SPOT THE CHANGE");
        
        // Draw some decorative icons on the start screen
        u8g2.drawXBMP(30, 45, 16, 16, iconSet[0]); // Smiley
        u8g2.drawXBMP(82, 45, 16, 16, iconSet[1]); // Heart
        
        u8g2.sendBuffer();
        needsRedraw = false;
      }

      if (isButtonPressed(btnSelect)) {
        currentLevel = 1;
        patternLength = 3;
        generatePattern();
        currentState = STATE_MEMORIZE;
        needsRedraw = true;
      }
      break;

    // ------------------------------------------------
    // STATE 2: MEMORIZE (Patient takes their time)
    // ------------------------------------------------
    case STATE_MEMORIZE:
      if (needsRedraw) {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        
        u8g2.setCursor(0, 10); u8g2.print("Lvl: "); u8g2.print(currentLevel);
        u8g2.drawStr(45, 10, "Memorize");
        
        // --- DRAW THE BITMAP ICONS ---
        int spacing = 20;
        int totalWidth = (patternLength - 1) * spacing + 16;
        int startX = (128 - totalWidth) / 2; // Auto-centers perfectly
        
        for (int i = 0; i < patternLength; i++) {
          int iconX = startX + (i * spacing);
          // drawXBMP takes (X, Y, Width, Height, Array)
          u8g2.drawXBMP(iconX, 26, 16, 16, iconSet[originalPattern[i]]);
        }

        u8g2.drawStr(10, 60, "Press Select to Start");
        u8g2.sendBuffer();
        needsRedraw = false;
      }

      if (isButtonPressed(btnSelect)) {
        cursorIndex = 0; 
        currentState = STATE_GUESS;
        needsRedraw = true;
      }
      break;

    // ------------------------------------------------
    // STATE 3: GUESS (Find what changed)
    // ------------------------------------------------
    case STATE_GUESS:
      if (needsRedraw) {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        
        u8g2.setCursor(0, 10); u8g2.print("Lvl: "); u8g2.print(currentLevel);
        u8g2.drawStr(40, 10, "What changed?");
        
        int spacing = 20;
        int totalWidth = (patternLength - 1) * spacing + 16;
        int startX = (128 - totalWidth) / 2; 
        
        for (int i = 0; i < patternLength; i++) {
          int iconX = startX + (i * spacing);
          
          u8g2.drawXBMP(iconX, 26, 16, 16, iconSet[modifiedPattern[i]]);
          
          // Draw a thick cursor line underneath the selected icon
          if (i == cursorIndex) {
            u8g2.drawLine(iconX - 1, 46, iconX + 16, 46);
            u8g2.drawLine(iconX - 1, 47, iconX + 16, 47); 
          }
        }
        
        u8g2.sendBuffer();
        needsRedraw = false;
      }

      if (isButtonPressed(btnLeft) && cursorIndex > 0) { 
        cursorIndex--; needsRedraw = true; beepClick(); 
      }
      if (isButtonPressed(btnRight) && cursorIndex < (patternLength - 1)) { 
        cursorIndex++; needsRedraw = true; beepClick(); 
      }

      if (isButtonPressed(btnSelect)) {
        if (cursorIndex == changedIndex) {
          // CORRECT: Instantly go to next level!
          beepSuccess();
          currentLevel++;
          
          patternLength = 3 + (currentLevel / 2);
          if (patternLength > MAX_PATTERN_LENGTH) {
            patternLength = MAX_PATTERN_LENGTH;
          }
          
          generatePattern();
          currentState = STATE_MEMORIZE; 
          delay(400); // Visual pause so they hear the success sound before screen wipes
          
        } else {
          // INCORRECT: Go to feedback screen
          beepIncorrect();
          currentState = STATE_FEEDBACK;
        }
        needsRedraw = true;
      }
      break;

    // ------------------------------------------------
    // STATE 4: FEEDBACK (Only shown if incorrect)
    // ------------------------------------------------
    case STATE_FEEDBACK:
      if (needsRedraw) {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        
        u8g2.drawStr(38, 10, "Nice Try!");
        u8g2.drawStr(10, 25, "The change was here:");
        
        // Show exactly which icon was the right answer
        int spacing = 20;
        int totalWidth = (patternLength - 1) * spacing + 16;
        int startX = (128 - totalWidth) / 2;
        int iconX = startX + (changedIndex * spacing);
        
        u8g2.drawXBMP(iconX, 31, 16, 16, iconSet[modifiedPattern[changedIndex]]);
        u8g2.drawLine(iconX - 1, 51, iconX + 16, 51);
        
        u8g2.drawStr(25, 62, "[Press Select]");
        u8g2.sendBuffer();
        needsRedraw = false;
      }

      if (isButtonPressed(btnSelect)) {
        generatePattern();
        currentState = STATE_MEMORIZE;
        needsRedraw = true;
      }
      
      if (isButtonPressed(btnUp) && isButtonPressed(btnDown)) {
        currentState = STATE_START;
        needsRedraw = true;
      }
      break;
  }
}