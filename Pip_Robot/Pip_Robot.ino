/* PIP Final - Arduino UNO R3
   LCD: RS=12, E=11, D4=5, D5=4, D6=3, D7=2; RW=GND.
   HC-SR04: TRIG=7, ECHO=8. Servo signal=9. Buzzer=A3 via 1k resistor.
   Keep the existing separate servo supply and common ground.
   Do not connect that supply's 5V output to Arduino 5V.

   PLAY: After a 5-second cooldown, hold hand 12-22 cm away for 1.2 seconds.
   Head centers during the gesture. Move hand beyond 30 cm to rearm.
   Three targets: 20, 12, 25 cm. Hold within +/-3 cm for 0.6 seconds.
   Both holds tolerate gaps up to 350 ms between valid in-range readings.
   Each round allows 15 seconds; score is out of 3.
   SLEEP: In FRIEND mode, no detection within 60 cm for 15 seconds.
   WAKE: Hold hand within 40 cm in front of the centered sensor.
   Head is limited to the previously tested 80-100 degree range.
*/
#include <LiquidCrystal.h>
#include <Servo.h>

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);
Servo head;
const int TRIG = 7, ECHO = 8, BUZZER = A3;
const int SERVO_PIN = 9;
const int scanAngles[] = {80, 90, 100, 90};
const int targets[] = {20, 12, 25};
const unsigned long SLEEP_AFTER = 15000UL;
const unsigned long ROUND_TIME = 15000UL;
enum Mode { FRIEND, ASLEEP, GAME, FEEDBACK, RESULTS };
Mode mode = FRIEND;

byte openEye[8] = {14,31,27,27,31,31,14,0};
byte happyEye[8] = {0,0,14,27,17,0,0,0};
byte closedEye[8] = {0,0,0,31,0,0,0,0};
byte surprisedEye[8] = {14,27,17,17,17,27,14,0};

int angle = 90, scanIndex = 1;
int roundIndex = 0, score = 0, previousMood = -1;
int wakeHits = 0;
float distanceCm = -1;
bool gestureActive = false, targetHeld = false;
bool gameArmed = true;
unsigned long gestureSince = 0, holdSince = 0;
unsigned long lastSeen = 0, lastSample = 0, lastDraw = 0;
unsigned long lastMove = 0, lastScan = 0, lastSound = 0;
unsigned long roundStarted = 0, modeSince = 0, gameCooldown = 0;

void line(byte row, const char* text) {
  char padded[17];
  byte i = 0;
  while (i < 16 && text[i] != '\0') {
    padded[i] = text[i];
    i++;
  }
  while (i < 16) padded[i++] = ' ';
  padded[16] = '\0';
  lcd.setCursor(0, row);
  lcd.print(padded);
}

void eyes(byte expression, int offset) {
  // Update all cells without clearing the whole display.
  lcd.setCursor(0, 0);
  for (int col = 0; col < 16; col++) {
    if (col == 5 + offset || col == 10 + offset) lcd.write(expression);
    else lcd.print(' ');
  }
}

void moveHead(int requested) {
  requested = constrain(requested, 80, 100);
  if (requested != angle) {
    angle = requested;
    head.write(angle);
    lastMove = millis();
  }
}

float readDistance() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  unsigned long echo = pulseIn(ECHO, HIGH, 25000UL);
  float cm = echo * 0.0343f / 2.0f;
  if (echo == 0 || cm < 2 || cm > 400) return -1;
  return cm;
}

void chirp(bool happy) {
  tone(BUZZER, happy ? 1200 : 1800, 90);
  delay(110);
  tone(BUZZER, happy ? 1800 : 700, 120);
}

void beginRound() {
  mode = GAME;
  roundStarted = millis();
  targetHeld = false;
  lastDraw = 0;
}

void startGame() {
  moveHead(90);
  gestureActive = false;
  gameArmed = false;
  roundIndex = 0;
  score = 0;
  chirp(true);
  beginRound();
}

void finishRound(bool won) {
  if (won) score++;
  eyes(won ? 1 : 3, 0);
  line(1, won ? "Got it! :)" : "Time up! Next...");
  chirp(won);
  mode = FEEDBACK;
  modeSince = millis();
}

void returnToFriend() {
  mode = FRIEND;
  lastSeen = millis();
  lastScan = millis();
  gameCooldown = millis();
  gestureActive = false;
  previousMood = -1;
}

void handleSample(unsigned long now) {
  static unsigned long lastGoodGesture = 0;
  static unsigned long lastGoodTarget = 0;

  bool nearby = distanceCm >= 2 && distanceCm < 60;

  if (mode == ASLEEP) {
    if (distanceCm >= 2 && distanceCm < 40) {
      wakeHits++;
    } else {
      wakeHits = 0;
    }

    if (wakeHits >= 3) {
      wakeHits = 0;
      returnToFriend();
      chirp(true);
    }
    return;
  }

  if (mode == GAME) {
    bool onTarget =
        distanceCm >= targets[roundIndex] - 3 &&
        distanceCm <= targets[roundIndex] + 3;

    // Reset only after readings stay off-target
    // for more than 350 milliseconds.
    if (targetHeld && now - lastGoodTarget > 350UL) {
      targetHeld = false;
    }

    if (onTarget) {
      if (!targetHeld) {
        targetHeld = true;
        holdSince = now;
      }

      lastGoodTarget = now;

      if (now - holdSince >= 600UL) {
        finishRound(true);
      }
    }
    return;
  }

  if (mode != FRIEND) return;
  if (nearby) lastSeen = now;

  // Move your hand away to allow another game.
  if (distanceCm > 30) {
    gameArmed = true;
  }

  // Wider, easier game-start range.
  bool startPose =
      gameArmed &&
      now - gameCooldown >= 5000UL &&
      distanceCm >= 12 && distanceCm <= 22;

  // Brief glitches no longer cancel the gesture.
  if (gestureActive &&
      now - lastGoodGesture > 350UL) {
    gestureActive = false;
  }

  if (startPose) {
    if (!gestureActive) {
      gestureActive = true;
      gestureSince = now;
      moveHead(90);
    }

    lastGoodGesture = now;

    if (now - gestureSince >= 1200UL) {
      startGame();
    }
  }

  if (mode != FRIEND) return;

  int mood = 0;

  if (nearby && distanceCm < 30) {
    mood = distanceCm < 10 ? 2 : 1;
  }

  if (!gestureActive &&
      mood != previousMood &&
      now - lastSound >= 2000UL) {
    if (mood != 0) {
      chirp(mood == 1);
    }

    previousMood = mood;
    lastSound = millis();
  }
}

void drawScreen(unsigned long now) {
  char text[24];
  if (mode == FEEDBACK) return;
  if (mode == RESULTS) {
    eyes(1, 0);
    snprintf(text, sizeof(text), "Score: %d/3!", score);
    line(1, text);
  } else if (mode == ASLEEP) {
    eyes(2, 0);
    line(1, "Zzz... wave here");
  } else if (mode == GAME) {
    snprintf(text, sizeof(text), "R%d: Hold at %dcm", roundIndex + 1, targets[roundIndex]);
    line(0, text);
    if (targetHeld) {
      line(1, "On target! Hold!");
    } else {
      unsigned long elapsed = now - roundStarted;
      int seconds = elapsed >= ROUND_TIME ? 0 : (ROUND_TIME - elapsed + 999) / 1000;
      if (distanceCm < 0) snprintf(text, sizeof(text), "You: --   %2ds", seconds);
      else snprintf(text, sizeof(text), "You:%3dcm %2ds", (int)(distanceCm + 0.5f), seconds);
      line(1, text);
    }
  } else {
    byte expression = 0;
    if (distanceCm >= 2 && distanceCm < 10) expression = 3;
    else if (distanceCm >= 10 && distanceCm < 30) expression = 1;
    if (now % 4000UL < 180) expression = 2;
    eyes(expression, angle < 90 ? -1 : (angle > 90 ? 1 : 0));
    if (gestureActive) line(1, "Hold to play...");
    else if ((now / 4000UL) % 2 == 1) line(1, "Game:hold12-22cm");
    else if (distanceCm >= 2 && distanceCm < 10) line(1, "Personal space!");
    else if (distanceCm >= 10 && distanceCm < 30) line(1, "Hi friend! :)");
    else line(1, "Come say hi!");
  }
}

void setup() {
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  pinMode(BUZZER, OUTPUT);
  digitalWrite(TRIG, LOW);
  delay(200);
  lcd.begin(16, 2);
  lcd.createChar(0, openEye);
  lcd.createChar(1, happyEye);
  lcd.createChar(2, closedEye);
  lcd.createChar(3, surprisedEye);
  head.attach(SERVO_PIN);
  head.write(90);
  eyes(2, 0);
  line(1, "PIP waking up...");
  delay(1000);
  chirp(true);
  lastSeen = lastMove = lastScan = millis();
  gameCooldown = millis();
}

void loop() {
  unsigned long now = millis();

  if (mode == GAME && now - roundStarted >= ROUND_TIME) finishRound(false);

  now = millis();
  if (mode == FEEDBACK && now - modeSince >= 1200UL) {
    roundIndex++;
    if (roundIndex < 3) beginRound();
    else {
      mode = RESULTS;
      modeSince = now;
      tone(BUZZER, 2200, 250);
    }
  } else if (mode == RESULTS && now - modeSince >= 4000UL) returnToFriend();

  now = millis();
  if (mode == FRIEND) {
    if (now - lastSeen >= SLEEP_AFTER) {
      mode = ASLEEP;
      gestureActive = false;
      wakeHits = 0;
      moveHead(90);
      noTone(BUZZER);
    } else if (!gestureActive && now - lastScan >= 900UL) {
      scanIndex = (scanIndex + 1) % 4;
      moveHead(scanAngles[scanIndex]);
      lastScan = now;
    }
  }

  now = millis();
  // Avoid taking readings while the head is turning.
  if ((mode == FRIEND || mode == ASLEEP || mode == GAME) &&
      now - lastSample >= 100UL && now - lastMove >= 250UL) {
    distanceCm = readDistance();
    lastSample = millis();
    handleSample(lastSample);
  }

  now = millis();
  if (now - lastDraw >= 100UL) {
    drawScreen(now);
    lastDraw = now;
  }
}
