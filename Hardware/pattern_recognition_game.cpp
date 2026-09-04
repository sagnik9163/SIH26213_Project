#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

// ==========================================
// 1. PIN DEFINITIONS
// ==========================================
// I2C Pins for OLED
#define OLED_SDA 21
#define OLED_SCL 22

// D-Pad and Select Buttons (Mapped to your specific Wokwi layout)
#define BTN_UP_PIN     32 // btn1
#define BTN_DOWN_PIN   27 // btn3
#define BTN_LEFT_PIN   26 // btn4
#define BTN_RIGHT_PIN  25 // btn2
#define BTN_SELECT_PIN 33 // btn5 (Center)

// Passive Buzzer Pin
#define BUZZER_PIN     12

// Unconnected analog pin for random seed
#define RANDOM_SEED_PIN 34 

// ==========================================
// 2. DISPLAY SETUP
// ==========================================
// Standard SSD1306 constructor for 128x64 displays
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

// ==========================================
// 3. GAME VARIABLES & STATES
// ==========================================
enum GameState {
  STATE_START,
  STATE_WATCH_PHASE,
  STATE_PLAY_PHASE,
  STATE_GAME_OVER
};

GameState currentState = STATE_START;

#define MAX_LEVEL 100
uint8_t sequence[MAX_LEVEL];
int currentLevel = 1;
int playIndex = 0;   // Tracks player's progress in the current sequence
int cursorIndex = 4; // Starts at center tile (0-8 grid)

// Event-driven rendering flag to prevent screen lag
bool needsRedraw = true; 

// ==========================================
// ==========================================
// 4. BUTTON DEBOUNCING LOGIC
// ==========================================
// Increased to 200ms to act as a rate-limiter for human fingers
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

// "Rate-Limited" Button Check: Fires instantly, then locks out for 200ms
bool isButtonPressed(Button &b) {
  bool pressed = false;
  int reading = digitalRead(b.pin);
  
  // Detect the exact moment the button gets pushed down
  if (reading == LOW && b.lastState == HIGH) {
    
    // Only register the press if the lockout period has expired
    if ((millis() - b.lastDebounceTime) > DEBOUNCE_DELAY) {
      pressed = true;
      b.lastDebounceTime = millis(); // Reset the lockout timer
    }
  }
  
  b.lastState = reading; // Always track the physical state of the metal
  return pressed;
}

// ==========================================
// 5. AUDIO HELPER FUNCTIONS
// ==========================================
void beepTile() {
  tone(BUZZER_PIN, 1000, 100); // 1000Hz for 100ms
}

void beepSuccess() {
  tone(BUZZER_PIN, 1200, 100); delay(100);
  tone(BUZZER_PIN, 1500, 100); delay(100);
  tone(BUZZER_PIN, 2000, 200); 
}

void beepGameOver() {
  tone(BUZZER_PIN, 200, 500);  // Low buzz, 200Hz for 500ms
}

// ==========================================
// 6. GRAPHICS HELPERS (128x64 Version)
// ==========================================
void drawGrid(int highlightedTile, bool showCursor) {
  const int tileSize = 14;
  const int gap = 2;
  const int startX = 41;
  const int startY = 16;

  for (int i = 0; i < 9; i++) {
    int col = i % 3;
    int row = i / 3;
    int x = startX + col * (tileSize + gap);
    int y = startY + row * (tileSize + gap);

    // Draw the tile itself
    if (i == highlightedTile) {
      u8g2.drawBox(x, y, tileSize, tileSize);
    } else {
      u8g2.drawFrame(x, y, tileSize, tileSize);
    }

    // Draw the player cursor
    if (showCursor && i == cursorIndex) {
      u8g2.drawFrame(x - 2, y - 2, tileSize + 4, tileSize + 4);
    }
  }
}

// ==========================================
// 7. ARDUINO SETUP
// ==========================================
void setup() {
  Serial.begin(115200);

  // Initialize Input Pullup Buttons
  pinMode(BTN_UP_PIN, INPUT_PULLUP);
  pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_LEFT_PIN, INPUT_PULLUP);
  pinMode(BTN_RIGHT_PIN, INPUT_PULLUP);
  pinMode(BTN_SELECT_PIN, INPUT_PULLUP);

  randomSeed(analogRead(RANDOM_SEED_PIN));
  
  u8g2.begin();
}

// ==========================================
// 8. MAIN LOOP (STATE MACHINE)
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
        u8g2.drawStr(25, 25, "PATTERN RECALL");
        u8g2.drawStr(30, 45, "[Press Select]");
        u8g2.sendBuffer();
        needsRedraw = false;
      }

      if (isButtonPressed(btnSelect)) {
        currentLevel = 1;
        sequence[0] = random(0, 9);
        cursorIndex = 4;
        currentState = STATE_WATCH_PHASE;
        needsRedraw = true;
        delay(500); 
      }
      break;

    // ------------------------------------------------
    // STATE: WATCH PHASE
    // ------------------------------------------------
    case STATE_WATCH_PHASE:
      for (int i = 0; i < currentLevel; i++) {
        // Show highlighted tile
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.drawStr(0, 10, "Watch...");
        u8g2.setCursor(95, 10); u8g2.print("Lvl:"); u8g2.print(currentLevel);
        
        drawGrid(sequence[i], false);
        u8g2.sendBuffer();
        
        beepTile();
        delay(400);

        // Turn off highlight for the gap between tiles
        u8g2.clearBuffer();
        u8g2.drawStr(0, 10, "Watch...");
        u8g2.setCursor(95, 10); u8g2.print("Lvl:"); u8g2.print(currentLevel);
        
        drawGrid(-1, false); 
        u8g2.sendBuffer();
        
        delay(200); 
      }
      
      playIndex = 0; 
      currentState = STATE_PLAY_PHASE;
      needsRedraw = true; // Queue a redraw for the play screen
      break;

    // ------------------------------------------------
    // STATE: PLAY PHASE
    // ------------------------------------------------
    case STATE_PLAY_PHASE:
      // Only render if a button was pressed to move the cursor
      if (needsRedraw) {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.drawStr(0, 10, "Your Turn!");
        u8g2.setCursor(95, 10); u8g2.print("Lvl:"); u8g2.print(currentLevel);
        drawGrid(-1, true); 
        u8g2.sendBuffer();
        needsRedraw = false;
      }

      // Handle Input - D-Pad
      if (isButtonPressed(btnUp) && cursorIndex > 2) {
        cursorIndex -= 3;
        needsRedraw = true; // Tell the screen to update
      }
      if (isButtonPressed(btnDown) && cursorIndex < 6) {
        cursorIndex += 3;
        needsRedraw = true;
      }
      if (isButtonPressed(btnLeft) && (cursorIndex % 3) > 0) {
        cursorIndex -= 1;
        needsRedraw = true;
      }
      if (isButtonPressed(btnRight) && (cursorIndex % 3) < 2) {
        cursorIndex += 1;
        needsRedraw = true;
      }

      // Handle Input - Selection
      if (isButtonPressed(btnSelect)) {
        // Visual confirmation
        u8g2.clearBuffer();
        u8g2.drawStr(0, 10, "Your Turn!");
        u8g2.setCursor(95, 10); u8g2.print("Lvl:"); u8g2.print(currentLevel);
        drawGrid(cursorIndex, true); 
        u8g2.sendBuffer();
        
        beepTile();
        delay(200); 

        // Game Logic Evaluation
        if (cursorIndex == sequence[playIndex]) {
          playIndex++;
          
          if (playIndex == currentLevel) {
            beepSuccess();
            delay(300);
            
            if (currentLevel < MAX_LEVEL) {
              sequence[currentLevel] = random(0, 9); 
              currentLevel++;
            }
            currentState = STATE_WATCH_PHASE;
            delay(500); 
          }
        } else {
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
        
        u8g2.setCursor(20, 45);
        u8g2.print("Reached Lvl: ");
        u8g2.print(currentLevel);
        
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
