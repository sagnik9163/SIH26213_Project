#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

// ==========================================
// 1. PIN DEFINITIONS & HARDWARE
// ==========================================
// I2C Pins for OLED
#define OLED_SDA 21
#define OLED_SCL 22

// D-Pad and Select Buttons
#define BTN_UP_PIN     18 
#define BTN_DOWN_PIN   23 
#define BTN_LEFT_PIN   26 
#define BTN_RIGHT_PIN  27 
#define BTN_SELECT_PIN 19 

// Passive Buzzer Pin
#define BUZZER_PIN     25

// Unconnected analog pin for random seed
#define RANDOM_SEED_PIN 34 

// Standard SSD1306 constructor for 128x64 displays
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

// ==========================================
// 2. BUTTON DEBOUNCING (Rate-Limiter)
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
// 3. AUDIO SYSTEM
// ==========================================
void beepTone(int freq, int duration) {
  tone(BUZZER_PIN, freq, duration);
}

void beepSuccess() {
  tone(BUZZER_PIN, 1200, 100); delay(100);
  tone(BUZZER_PIN, 1500, 100); delay(100);
  tone(BUZZER_PIN, 2000, 200); 
}

void beepGameOver() {
  tone(BUZZER_PIN, 200, 500);
}

// ==========================================
// 4. GAME VARIABLES & STATES
// ==========================================
enum GameState { STATE_START, STATE_PLAY, STATE_GAME_OVER };
GameState currentState = STATE_START;

bool needsRedraw = true; // Event-driven rendering flag

int mathScore = 0;
char mathQuestion[20];
int mathOptions[4];
int mathCorrectSlot = 0;
int mathCursor = 0; // 0=TopLeft, 1=TopRight, 2=BottomLeft, 3=BottomRight

// ==========================================
// 5. MATH GENERATOR LOGIC
// ==========================================
void generateMathProblem() {
  int op = random(0, 4); // 0:+, 1:-, 2:*, 3:/
  int num1, num2, ans;

  if (op == 0) { // Addition
    num1 = random(1, 10); num2 = random(1, 10);
    ans = num1 + num2;
    sprintf(mathQuestion, "%d + %d = ?", num1, num2);
  } 
  else if (op == 1) { // Subtraction (No negatives allowed)
    num1 = random(1, 10); num2 = random(1, 10);
    if (num1 < num2) { int t = num1; num1 = num2; num2 = t; } // Swap to ensure num1 is bigger
    ans = num1 - num2;
    sprintf(mathQuestion, "%d - %d = ?", num1, num2);
  } 
  else if (op == 2) { // Multiplication
    num1 = random(1, 10); num2 = random(1, 10);
    ans = num1 * num2;
    sprintf(mathQuestion, "%d x %d = ?", num1, num2);
  } 
  else { // Division (Forces perfect integers)
    num2 = random(1, 10); ans = random(1, 10);
    num1 = num2 * ans; 
    sprintf(mathQuestion, "%d / %d = ?", num1, num2);
  }

  // Pre-fill options with a dummy value to avoid duplicate checking bugs
  for (int i = 0; i < 4; i++) {
    mathOptions[i] = -999;
  }

  // Put the real answer in a random slot
  mathCorrectSlot = random(0, 4);
  mathOptions[mathCorrectSlot] = ans;

  // Generate tricky fake answers for the other 3 slots
  for (int i = 0; i < 4; i++) {
    if (i == mathCorrectSlot) continue; // Skip the real answer
    
    int fake;
    bool isUnique;
    do {
      isUnique = true;
      fake = ans + random(-4, 5); // Pick a number mathematically close to the real one
      if (fake < 0) fake = random(0, 20); // Keep fake answers positive
      
      // Ensure this fake doesn't match the real answer or any other fakes we generated
      for (int j = 0; j < 4; j++) {
        if (j != i && mathOptions[j] == fake) {
          isUnique = false;
        }
      }
    } while (!isUnique);
    
    mathOptions[i] = fake;
  }
}

// ==========================================
// 6. ARDUINO SETUP
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
// 7. MAIN GAME LOOP
// ==========================================
void loop() {
  
  switch (currentState) {

    // ------------------------------------------------
    // STATE: START SCREEN
    // ------------------------------------------------
    case STATE_START:
      if (needsRedraw) {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.drawStr(25, 25, "QUICK MATH");
        u8g2.drawStr(30, 45, "[Press Select]");
        u8g2.sendBuffer();
        needsRedraw = false;
      }

      if (isButtonPressed(btnSelect)) {
        mathScore = 0;
        mathCursor = 0;
        generateMathProblem();
        currentState = STATE_PLAY;
        needsRedraw = true;
      }
      break;

    // ------------------------------------------------
    // STATE: PLAY PHASE
    // ------------------------------------------------
    case STATE_PLAY:
      if (needsRedraw) {
        u8g2.clearBuffer();
        
        // 1. Draw Score
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.setCursor(0, 10); u8g2.print("Score: "); u8g2.print(mathScore);
        
        // 2. Draw Math Question (Auto-centered!)
        u8g2.setFont(u8g2_font_ncenB10_tr); 
        int textWidth = u8g2.getStrWidth(mathQuestion);
        u8g2.setCursor((128 - textWidth) / 2, 28); 
        u8g2.print(mathQuestion);

        // 3. Draw 2x2 Options Grid
        u8g2.setFont(u8g2_font_ncenB08_tr);
        for (int i = 0; i < 4; i++) {
          int boxX = (i % 2 == 0) ? 14 : 74; // Column 1 or Column 2
          int boxY = (i < 2) ? 34 : 49;      // Row 1 or Row 2
          
          // Draw bounding box if cursor is highlighting this option
          if (i == mathCursor) {
            u8g2.drawFrame(boxX, boxY, 40, 14); // 40px wide, 14px tall
          }
          
          // Draw the option text inside the box
          int optWidth = u8g2.getStrWidth(String(mathOptions[i]).c_str());
          u8g2.setCursor(boxX + (40 - optWidth) / 2, boxY + 11); // Center text in box
          u8g2.print(mathOptions[i]);
        }
        
        u8g2.sendBuffer();
        needsRedraw = false;
      }

      // 4. Handle 2x2 Grid Movement
      // mathCursor indices: 0(TL), 1(TR), 2(BL), 3(BR)
      if (isButtonPressed(btnUp) && mathCursor > 1) { 
        mathCursor -= 2; needsRedraw = true; 
      }
      if (isButtonPressed(btnDown) && mathCursor < 2) { 
        mathCursor += 2; needsRedraw = true; 
      }
      if (isButtonPressed(btnLeft) && (mathCursor % 2 != 0)) { 
        mathCursor -= 1; needsRedraw = true; 
      }
      if (isButtonPressed(btnRight) && (mathCursor % 2 == 0)) { 
        mathCursor += 1; needsRedraw = true; 
      }

      // 5. Handle Selection
      if (isButtonPressed(btnSelect)) {
        if (mathCursor == mathCorrectSlot) {
          // Correct!
          beepTone(1000, 100);
          mathScore++;
          generateMathProblem(); // Instantly create the next level
        } else {
          // Wrong!
          beepGameOver();
          currentState = STATE_GAME_OVER;
        }
        needsRedraw = true;
      }
      break;

    // ------------------------------------------------
    // STATE: GAME OVER
    // ------------------------------------------------
    case STATE_GAME_OVER:
      if (needsRedraw) {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.drawStr(30, 25, "GAME OVER!");
        
        u8g2.setCursor(40, 45); 
        u8g2.print("Score: "); 
        u8g2.print(mathScore);
        
        u8g2.sendBuffer();
        needsRedraw = false;
      }

      if (isButtonPressed(btnSelect)) {
        currentState = STATE_START;
        needsRedraw = true;
      }
      break;
  }
}