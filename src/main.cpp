#include <Arduino.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

// --- HARDWARE PINOUT ---
#define TFT_LED_PIN     14  // Backlight PWM

// Inputs
#define PIN_JOY_X       34  // Joystick X (ADC1)
#define PIN_JOY_Y       33  // Joystick Y (ADC1)
#define PIN_POT_BRIGHT  32  // Potentiometer Contrast / Blackout (ADC1)
#define PIN_SKILL_BTN   25  // ปุ่มสกิล / ตกลง (INPUT_PULLUP)

// Outputs (ไฟสถานะ HP 3 สี)
#define PIN_LED_GRN     16  // LED เขียว (HP >= 3)
#define PIN_LED_YEL     17  // LED เหลือง (HP >= 2)
#define PIN_LED_RED      5  // LED แดง (HP >= 1)
#define PIN_BUZZER      26  // Passive Buzzer

// Resolution & Entities (โหมดแนวตั้ง Portrait 320x480)
#define SCREEN_W        320
#define SCREEN_H        480
#define PLAYER_SIZE     10
#define ENEMY_SIZE       8
#define MAX_ENEMIES     10
#define MAX_GEMS         8
#define BOMB_RADIUS     55

enum GameState { STATE_TITLE, STATE_PLAYING, STATE_LEVELUP, STATE_GAMEOVER };
GameState currentState = STATE_TITLE;

struct Enemy {
  float x, y;
  float oldX, oldY;
  float speed;
  bool active;
};

struct Gem {
  float x, y;
  float oldX, oldY;
  bool active;
};

Enemy enemies[MAX_ENEMIES];
Gem gems[MAX_GEMS];

float playerX = 160;
float playerY = 240;
float oldPlayerX = 160;
float oldPlayerY = 240;

int playerHP = 3;
int playerLevel = 1;
int playerXP = 0;
int xpToNextLevel = 4;
int killCount = 0;
unsigned long gameStartTime = 0;
unsigned long survivalTimeSec = 0;
unsigned long bestSurvivalTime = 0;

// Upgradable Player Stats
unsigned long lastAutoAttackTime = 0;
unsigned long autoAttackInterval = 1200; 
int whipRadius = 35;                     
bool whipVisualActive = false;
unsigned long whipVisualTimer = 0;
int whipCenterX = 0;
int whipCenterY = 0;
int activeWhipRadius = 0;

// Bomb Skill
unsigned long lastBombTime = 0;
const unsigned long BOMB_COOLDOWN = 4000;
bool bombVisualActive = false;
unsigned long bombVisualTimer = 0;
int bombCenterX = 0;
int bombCenterY = 0;

// Level-Up Menu
int menuSelection = 0; // 0 = ATK SPEED, 1 = RANGE
bool hpBonusAwarded = false;
unsigned long joyMenuCooldown = 0;

// Audio Timers
unsigned long buzzerOffTime = 0;
bool isBuzzerSounding = false;
unsigned long stateEnterTime = 0;

// UI & Title Flags
bool titleScreenDrawn = false;
bool gameOverScreenDrawn = false;
unsigned long lastBlinkTime = 0;
bool blinkState = false;

// Backlight Dimmer
const int pwmFreq = 5000;
const int pwmResolution = 8;

//level up menu
bool levelUpScreenDrawn = false;
int lastMenuSelection = -1;

void setBacklight(uint8_t brightness) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcWrite(TFT_LED_PIN, brightness);
#else
  ledcWrite(0, brightness);
#endif
}

void initBacklight() {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach(TFT_LED_PIN, pwmFreq, pwmResolution);
#else
  ledcSetup(0, pwmFreq, pwmResolution);
  ledcAttachPin(TFT_LED_PIN, 0);
#endif
  setBacklight(255);
}

void updateHealthLEDs(int hp) {
  digitalWrite(PIN_LED_GRN, hp >= 3 ? HIGH : LOW);
  digitalWrite(PIN_LED_YEL, hp >= 2 ? HIGH : LOW);
  digitalWrite(PIN_LED_RED, hp >= 1 ? HIGH : LOW);
}

// Sound Functions
void playSfx(int freq, int durationMs) {
  tone(PIN_BUZZER, freq);
  buzzerOffTime = millis() + durationMs;
  isBuzzerSounding = true;
}

void playGemBeep()     { playSfx(2400, 45); }
void playHitSound()    { playSfx(160, 120); }
void playKillSound()   { playSfx(850, 35); }
void playSelectSound() { playSfx(1400, 40); }
void playLevelFanfare(){ playSfx(3000, 180); }

void updateBuzzer() {
  if (isBuzzerSounding && millis() >= buzzerOffTime) {
    noTone(PIN_BUZZER);
    digitalWrite(PIN_BUZZER, LOW);
    isBuzzerSounding = false;
  }
}

void spawnEnemy(int idx) {
  if (random(0, 2) == 0) {
    enemies[idx].x = (random(0, 2) == 0) ? 6 : (SCREEN_W - 14);
    enemies[idx].y = random(24, SCREEN_H - 14);
  } else {
    enemies[idx].x = random(6, SCREEN_W - 14);
    enemies[idx].y = (random(0, 2) == 0) ? 24 : (SCREEN_H - 14);
  }
  enemies[idx].oldX = enemies[idx].x;
  enemies[idx].oldY = enemies[idx].y;

  float baseSpeed = 0.7 + (survivalTimeSec * 0.008);
  if (baseSpeed > 1.8) baseSpeed = 1.8;
  enemies[idx].speed = baseSpeed;
  enemies[idx].active = true;
}

void spawnGem(float x, float y) {
  for (int i = 0; i < MAX_GEMS; i++) {
    if (!gems[i].active) {
      gems[i].x = x;
      gems[i].y = y;
      gems[i].oldX = x;
      gems[i].oldY = y;
      gems[i].active = true;
      break;
    }
  }
}

void resetGame() {
  playerX = 160; playerY = 240;
  oldPlayerX = 160; oldPlayerY = 240;
  playerHP = 3;
  playerLevel = 1;
  playerXP = 0;
  xpToNextLevel = 4;
  killCount = 0;
  autoAttackInterval = 1200;
  whipRadius = 35;
  whipVisualActive = false;
  bombVisualActive = false;

  gameStartTime = millis();
  survivalTimeSec = 0;
  stateEnterTime = millis();

  for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].active = false;
  for (int i = 0; i < MAX_GEMS; i++) gems[i].active = false;
  for (int i = 0; i < 4; i++) spawnEnemy(i);

  updateHealthLEDs(playerHP);
  tft.fillScreen(TFT_BLACK);
  tft.drawRect(0, 0, SCREEN_W, SCREEN_H, TFT_WHITE);
  currentState = STATE_PLAYING;
}

// --- STATE: TITLE SCREEN ---
void updateTitleScreen() {
  int ledPhase = (millis() / 200) % 3;
  digitalWrite(PIN_LED_GRN, ledPhase == 0);
  digitalWrite(PIN_LED_YEL, ledPhase == 1);
  digitalWrite(PIN_LED_RED, ledPhase == 2);

  if (!titleScreenDrawn) {
    tft.fillScreen(TFT_BLACK);
    tft.drawRect(4, 4, SCREEN_W - 8, SCREEN_H - 8, TFT_CYAN);
    tft.drawRect(8, 8, SCREEN_W - 16, SCREEN_H - 16, TFT_BLUE);

    tft.setTextFont(1);
    tft.setTextSize(3);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("ESP32", 110, 80);
    tft.drawString("SURVIVORS", 75, 120);

    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Retro Arena", 95, 180);

    if (bestSurvivalTime > 0) {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.setCursor(75, 240);
      tft.printf("Best: %lus", bestSurvivalTime);
    }
    titleScreenDrawn = true;
  }

  // ข้อความกระพริบ
  if (millis() - lastBlinkTime > 400) {
    lastBlinkTime = millis();
    blinkState = !blinkState;
    tft.setTextSize(2);
    if (blinkState) {
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.drawString("> PRESS TO START <", 50, 360);
    } else {
      tft.fillRect(50, 360, 230, 24, TFT_BLACK);
    }
  }

  if (millis() - stateEnterTime > 800) {
    if (digitalRead(PIN_SKILL_BTN) == LOW) {
      delay(200);
      titleScreenDrawn = false;
      resetGame();
    }
  }
}

// --- STATE: LEVEL UP SCREEN ---
// ฟังก์ชันวาดเฉพาะแถวตัวเลือก 2 แถว
void drawLevelUpOptions() {
  tft.setTextFont(1);
  tft.setTextSize(2);

  // --- แถวที่ 1 (บน): ATK SPEED ---
  tft.setCursor(35, 190);
  if (menuSelection == 0) {
    tft.setTextColor(TFT_GREEN, TFT_NAVY);
    tft.print("> ");
  } else {
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.print("  ");
  }
  tft.printf("ATK SPD (%lums) ", autoAttackInterval);

  // --- แถวที่ 2 (ล่าง): RANGE ---
  tft.setCursor(35, 240);
  if (menuSelection == 1) {
    tft.setTextColor(TFT_GREEN, TFT_NAVY);
    tft.print("> ");
  } else {
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.print("  ");
  }
  tft.printf("RANGE   (%dpx)  ", whipRadius);
}

// --- STATE: LEVEL UP (วาดเฉพาะตอนเปลี่ยนตัวเลือก) ---
void updateLevelUpScreen() {
  int rawX = analogRead(PIN_JOY_X); // แกนแนวตั้ง (ขึ้น-ลง)

  // 1. ตรวจจับการโยกเปลี่ยนเมนู
  if (millis() - joyMenuCooldown > 250) {
    if (rawX < 1400) { 
      // ดันขึ้น -> เลือกตัวบน
      if (menuSelection != 0) {
        menuSelection = 0;
        playSelectSound();
        joyMenuCooldown = millis();
      }
    } else if (rawX > 2600) { 
      // ดึงลง -> เลือกตัวล่าง
      if (menuSelection != 1) {
        menuSelection = 1;
        playSelectSound();
        joyMenuCooldown = millis();
      }
    }
  }

  // 2. กดยืนยันการเลือก
  if (digitalRead(PIN_SKILL_BTN) == LOW && (millis() - stateEnterTime > 400)) {
    if (menuSelection == 0) {
      if (autoAttackInterval > 350) autoAttackInterval -= 150;
    } else {
      if (whipRadius < 70) whipRadius += 6;
    }
    playLevelFanfare();
    delay(200);
    tft.fillScreen(TFT_BLACK);
    tft.drawRect(0, 0, SCREEN_W, SCREEN_H, TFT_WHITE);
    currentState = STATE_PLAYING;
    return;
  }

  // 3. วาดโครงสร้างกล่องเมนูและข้อความหลักเพียง "ครั้งเดียว" ตอนเข้าหน้า
  if (!levelUpScreenDrawn) {
    tft.fillRect(20, 80, 280, 280, TFT_NAVY);
    tft.drawRect(20, 80, 280, 280, TFT_YELLOW);

    tft.setTextFont(1);
    tft.setTextColor(TFT_YELLOW, TFT_NAVY);
    tft.setTextSize(3);
    tft.setCursor(45, 100);
    tft.printf("LEVEL UP! [Lv%d]", playerLevel);

    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextSize(2);
    if (hpBonusAwarded) {
      tft.setTextColor(TFT_GREEN, TFT_NAVY);
      tft.setCursor(75, 140);
      tft.println("+1 HP BONUS!");
    } else {
      tft.setCursor(65, 140);
      tft.println("CHOOSE UPGRADE:");
    }

    tft.setTextColor(TFT_CYAN, TFT_NAVY);
    tft.setTextSize(1);
    tft.setCursor(55, 320);
    tft.println("[JOY:Up/Down | BTN:Confirm]");

    levelUpScreenDrawn = true;
    drawLevelUpOptions(); // วาดตัวเลือกเริ่มต้น
    lastMenuSelection = menuSelection;
  }

  // 4. วาดเฉพาะเมื่อค่า menuSelection เปลี่ยนแปลงเท่านั้น
  if (lastMenuSelection != menuSelection) {
    drawLevelUpOptions();
    lastMenuSelection = menuSelection;
  }
}
// --- STATE: PLAYING SCREEN ---
void updatePlayingScreen() {
  survivalTimeSec = (millis() - gameStartTime) / 1000;

  oldPlayerX = playerX;
  oldPlayerY = playerY;

  // 1. ควบคุมตัวละคร (Invert X & Y)
  int rawX = analogRead(PIN_JOY_X);
  int rawY = analogRead(PIN_JOY_Y);

  // --- แนวนอน (แก้สลับ ซ้าย-ขวา ให้ตรงทิศ) ---
  // ถ้าใช้ rawY คุมแนวนอน:
  if (rawY > 2600 && playerX > 4) playerX -= 2.8;                          // ไปทางซ้าย (ลดค่า X)
  if (rawY < 1400 && playerX < SCREEN_W - PLAYER_SIZE - 4) playerX += 2.8;  // ไปทางขวา (เพิ่มค่า X)

  // --- แนวตั้ง (ขึ้น-ลง) ---
  if (rawX > 2600 && playerY < SCREEN_H - PLAYER_SIZE - 4) playerY += 2.8;  // ลง
  if (rawX < 1400 && playerY > 26) playerY -= 2.8;                          // ขึ้น

  // 2. จัดการวงโจมตีแส้
  if (whipVisualActive && (millis() - whipVisualTimer > 120)) {
    whipVisualActive = false;
    tft.drawCircle(whipCenterX, whipCenterY, activeWhipRadius, TFT_BLACK);
  }

  if (millis() - lastAutoAttackTime >= autoAttackInterval) {
    lastAutoAttackTime = millis();
    whipVisualActive = true;
    whipVisualTimer = millis();
    whipCenterX = (int)playerX + (PLAYER_SIZE / 2);
    whipCenterY = (int)playerY + (PLAYER_SIZE / 2);
    activeWhipRadius = whipRadius;

    tft.drawCircle(whipCenterX, whipCenterY, activeWhipRadius, TFT_YELLOW);

    for (int i = 0; i < MAX_ENEMIES; i++) {
      if (enemies[i].active) {
        float d = sqrt(pow(enemies[i].x - playerX, 2) + pow(enemies[i].y - playerY, 2));
        if (d <= whipRadius) {
          enemies[i].active = false;
          tft.fillRect((int)enemies[i].x, (int)enemies[i].y, ENEMY_SIZE, ENEMY_SIZE, TFT_BLACK);
          killCount++;
          spawnGem(enemies[i].x, enemies[i].y);
          playKillSound();
        }
      }
    }
  }

  // 3. จัดการวงระเบิดฉุกเฉิน
  if (bombVisualActive && (millis() - bombVisualTimer > 250)) {
    bombVisualActive = false;
    tft.drawCircle(bombCenterX, bombCenterY, BOMB_RADIUS, TFT_BLACK);
    tft.drawCircle(bombCenterX, bombCenterY, BOMB_RADIUS - 10, TFT_BLACK);
  }

  if (digitalRead(PIN_SKILL_BTN) == LOW && (millis() - lastBombTime >= BOMB_COOLDOWN)) {
    lastBombTime = millis();
    bombVisualActive = true;
    bombVisualTimer = millis();
    bombCenterX = (int)playerX + (PLAYER_SIZE / 2);
    bombCenterY = (int)playerY + (PLAYER_SIZE / 2);

    tft.drawCircle(bombCenterX, bombCenterY, BOMB_RADIUS, TFT_ORANGE);
    tft.drawCircle(bombCenterX, bombCenterY, BOMB_RADIUS - 10, TFT_RED);

    bool killedAny = false;
    for (int i = 0; i < MAX_ENEMIES; i++) {
      if (enemies[i].active) {
        float d = sqrt(pow(enemies[i].x - playerX, 2) + pow(enemies[i].y - playerY, 2));
        if (d <= BOMB_RADIUS) {
          enemies[i].active = false;
          tft.fillRect((int)enemies[i].x, (int)enemies[i].y, ENEMY_SIZE, ENEMY_SIZE, TFT_BLACK);
          killCount++;
          spawnGem(enemies[i].x, enemies[i].y);
          killedAny = true;
        }
      }
    }
    if (killedAny) playKillSound();
  }

  // 4. มอนสเตอร์เคลื่อนที่
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) {
      if (random(0, 30) == 0) spawnEnemy(i);
      continue;
    }

    enemies[i].oldX = enemies[i].x;
    enemies[i].oldY = enemies[i].y;

    if (enemies[i].x < playerX) enemies[i].x += enemies[i].speed;
    else enemies[i].x -= enemies[i].speed;

    if (enemies[i].y < playerY) enemies[i].y += enemies[i].speed;
    else enemies[i].y -= enemies[i].speed;

    float distToPlayer = sqrt(pow(enemies[i].x - playerX, 2) + pow(enemies[i].y - playerY, 2));
    if (distToPlayer < 8.0) {
      enemies[i].active = false;
      tft.fillRect((int)enemies[i].oldX, (int)enemies[i].oldY, ENEMY_SIZE, ENEMY_SIZE, TFT_BLACK);
      playerHP--;
      updateHealthLEDs(playerHP);
      playHitSound();

      if (playerHP <= 0) {
        if (survivalTimeSec > bestSurvivalTime) {
          bestSurvivalTime = survivalTimeSec;
        }
        currentState = STATE_GAMEOVER;
        stateEnterTime = millis();
        gameOverScreenDrawn = false;
        tft.fillScreen(TFT_BLACK);
        return;
      }
    }
  }

  // 5. เก็บเพชร Gem
  for (int i = 0; i < MAX_GEMS; i++) {
    if (gems[i].active) {
      float d = sqrt(pow(gems[i].x - playerX, 2) + pow(gems[i].y - playerY, 2));
      if (d < 12.0) {
        gems[i].active = false;
        tft.fillRect((int)gems[i].x - 3, (int)gems[i].y - 3, 8, 8, TFT_BLACK);
        playerXP++;
        playGemBeep();

        if (playerXP >= xpToNextLevel) {
          playerLevel++;
          playerXP = 0;
          xpToNextLevel += 3;

          if (playerLevel % 5 == 0) {
            hpBonusAwarded = true;
            if (playerHP < 3) {
              playerHP++;
              updateHealthLEDs(playerHP);
            }
          } else {
            hpBonusAwarded = false;
          }

          playLevelFanfare();
          stateEnterTime = millis();
          levelUpScreenDrawn = false;
          lastMenuSelection = -1;
          currentState = STATE_LEVELUP;
          return;
        }
      }
    }
  }

  // 6. ลบและวาดออบเจกต์ (Partial Redraw)
  if ((int)oldPlayerX != (int)playerX || (int)oldPlayerY != (int)playerY) {
    tft.fillRect((int)oldPlayerX, (int)oldPlayerY, PLAYER_SIZE, PLAYER_SIZE, TFT_BLACK);
  }
  tft.fillRect((int)playerX, (int)playerY, PLAYER_SIZE, PLAYER_SIZE, TFT_CYAN);

  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].active) {
      if ((int)enemies[i].oldX != (int)enemies[i].x || (int)enemies[i].oldY != (int)enemies[i].y) {
        tft.fillRect((int)enemies[i].oldX, (int)enemies[i].oldY, ENEMY_SIZE, ENEMY_SIZE, TFT_BLACK);
      }
      tft.fillRect((int)enemies[i].x, (int)enemies[i].y, ENEMY_SIZE, ENEMY_SIZE, TFT_RED);
    }
  }

  for (int i = 0; i < MAX_GEMS; i++) {
    if (gems[i].active) {
      tft.fillRect((int)gems[i].x, (int)gems[i].y - 2, 2, 6, TFT_GREEN);
      tft.fillRect((int)gems[i].x - 2, (int)gems[i].y, 6, 2, TFT_GREEN);
    }
  }

  // หลอด XP และ HUD ด้านบน
  int xpWidth = map(playerXP, 0, xpToNextLevel, 0, SCREEN_W - 8);
  tft.fillRect(4, 4, xpWidth, 4, TFT_GREEN);
  tft.fillRect(4 + xpWidth, 4, (SCREEN_W - 8) - xpWidth, 4, TFT_DARKGREY);

  static unsigned long lastHudTime = 0;
  if (millis() - lastHudTime > 300) {
    lastHudTime = millis();
    tft.fillRect(10, 12, 220, 16, TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 12);
    tft.printf("L%d %02lu:%02lu", playerLevel, survivalTimeSec / 60, survivalTimeSec % 60);

    tft.fillRect(SCREEN_W - 70, 12, 60, 16, TFT_BLACK);
    if (millis() - lastBombTime >= BOMB_COOLDOWN) {
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.setCursor(SCREEN_W - 65, 12);
      tft.print("[BOMB]");
    }
  }
}

// --- STATE: GAME OVER SCREEN ---
void updateGameOverScreen() {
  bool flash = (millis() / 250) % 2;
  digitalWrite(PIN_LED_GRN, flash);
  digitalWrite(PIN_LED_YEL, flash);
  digitalWrite(PIN_LED_RED, flash);

  if (!gameOverScreenDrawn) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextFont(1);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(5);
    tft.drawString("YOU DIED", 40, 100);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(3);
    tft.setCursor(50, 190);
    tft.printf("Survived: %lus", survivalTimeSec);
    tft.setCursor(50, 240);
    tft.printf("Kills: %d  Lv:%d", killCount, playerLevel);
    gameOverScreenDrawn = true;
  }

  if (millis() - lastBlinkTime > 400) {
    lastBlinkTime = millis();
    blinkState = !blinkState;
    tft.setTextSize(2);
    if (blinkState) {
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("> PRESS TO RETRY <", 50, 360);
    } else {
      tft.fillRect(50, 360, 230, 24, TFT_BLACK);
    }
  }

  if (millis() - stateEnterTime > 1000) {
    if (digitalRead(PIN_SKILL_BTN) == LOW) {
      delay(200);
      gameOverScreenDrawn = false;
      resetGame();
    }
  }
}

// --- SETUP & LOOP ---
void setup() {
  Serial.begin(115200);

  pinMode(PIN_SKILL_BTN, INPUT_PULLUP);
  pinMode(PIN_JOY_X, INPUT);
  pinMode(PIN_JOY_Y, INPUT);
  pinMode(PIN_POT_BRIGHT, INPUT);

  pinMode(PIN_LED_GRN, OUTPUT);
  pinMode(PIN_LED_YEL, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  initBacklight();

  tft.init();
  tft.setRotation(2); // ปรับเป็นแนวตั้ง Portrait (320 x 480)
  tft.fillScreen(TFT_BLACK);

  stateEnterTime = millis();
  titleScreenDrawn = false;
  currentState = STATE_TITLE;
}

void loop() {
  updateBuzzer();

  int potVal = analogRead(PIN_POT_BRIGHT);
  if (potVal < 150) {
    setBacklight(0);
  } else {
    uint8_t duty = map(potVal, 150, 4095, 20, 255);
    setBacklight(duty);
  }

  switch (currentState) {
    case STATE_TITLE:
      updateTitleScreen();
      break;
    case STATE_PLAYING:
      updatePlayingScreen();
      break;
    case STATE_LEVELUP:
      updateLevelUpScreen();
      break;
    case STATE_GAMEOVER:
      updateGameOverScreen();
      break;
  }

  delay(12);
}