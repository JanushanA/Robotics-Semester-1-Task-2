#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_NeoPixel.h>

// PIN DEFINITIONS

#define SS_PIN      10
#define RST_PIN      9
#define LED_PIN      3
#define LED_COUNT    8
#define BUZZER_PIN   2
#define TCS_S0       4
#define TCS_S1       5
#define TCS_S2       6
#define TCS_S3       7
#define TCS_OUT      8
#define BUTTON_PIN   A0


// GAME SETTINGS

const int  MAX_PLAYERS     = 4;
const byte RFID_DATA_BLOCK = 4;
const float UNKNOWN_THRESHOLD = 500.0;  // Bigger = easier to match colours


// GAME TYPES

enum TileType {
  RFID_TILE,
  COLOUR_TILE,
  MEMORY_TILE,
  TIMED_RFID_TILE,
  TIMED_COLOUR_TILE,
  TIMED_MEMORY_TILE,
  BONUS_TILE
};

// Colours the game currently uses.
enum GameColour {
  BLACK_COLOUR   = 0,
  GREEN_COLOUR   = 1,
  YELLOW_COLOUR  = 2,
  PURPLE_COLOUR  = 3,
  UNKNOWN_COLOUR = 99
};

// saved colour sample 
struct KnownColor {
  GameColour    id;
  const char*   name;
  unsigned long r, g, b;   // Raw pulseIn readings
  uint32_t      neoColor;  // NeoPixel output colour
};

// colour calibration palette.
const KnownColor PALETTE[] = {
  { BLACK_COLOUR,  "BLACK",  4100, 3333, 3038, 0x333333 },
  { GREEN_COLOUR,  "GREEN",   1148,  697,  915, 0x00FF00 },
  { YELLOW_COLOUR, "YELLOW",  453,  407,  864, 0xFFB400 },
  { PURPLE_COLOUR, "PURPLE",  831, 1716,  990, 0x6600CC }
};
const int PALETTE_SIZE = 4;


// LCD WRAPPER CLASS
// Handles LCD output and mirrors LCD text to Serial with [LCD].

class DisplayManager {
private:
  LiquidCrystal_I2C lcd;
  String lineBuffer[2];
  int currentRow;

  // Prints one buffered LCD line to Serial.
  void flushLine(int row) {
    if (lineBuffer[row].length() > 0) {
      Serial.print(F("[LCD] "));
      Serial.println(lineBuffer[row]);
      lineBuffer[row] = "";
    }
  }

public:
  DisplayManager() : lcd(0x27, 16, 2), currentRow(0) {}

  // Starts the LCD screen.
  void begin() {
    lcd.init();
    lcd.backlight();
  }

  // Clears LCD and flushes old LCD text to Serial.
  void clear() {
    flushLine(0);
    flushLine(1);
    currentRow = 0;
    lcd.clear();
  }

  // Moves the LCD cursor.
  void setCursor(uint8_t col, uint8_t row) {
    if (row == 1 && currentRow == 0) flushLine(0);
    currentRow = row;
    lcd.setCursor(col, row);
  }

  // Prints text to LCD and saves it for Serial mirror.
  void print(const char* s) {
    lineBuffer[currentRow] += s;
    lcd.print(s);
  }

  // Prints String text to LCD and Serial buffer.
  void print(const String &s) {
    lineBuffer[currentRow] += s;
    lcd.print(s);
  }

  // Prints numbers to LCD and Serial buffer.
  void print(int n) {
    lineBuffer[currentRow] += String(n);
    lcd.print(n);
  }

  // Prints one character to LCD and Serial buffer.
  void print(char c) {
    lineBuffer[currentRow] += c;
    lcd.print(c);
  }
};


// LED WRAPPER CLASS
// Controls all NeoPixel effects.

class LedManager {
private:
  Adafruit_NeoPixel strip;

public:
  LedManager() : strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800) {}

  // Starts the NeoPixel strip.
  void begin() {
    strip.begin();
    strip.show();
  }

  // Sets every pixel to one colour, but does not show until show() is called.
  void setAll(uint32_t color) {
    for (int i = 0; i < LED_COUNT; i++) {
      strip.setPixelColor(i, color);
    }
  }

  // Sends the prepared pixel colours to the LEDs.
  void show() {
    strip.show();
  }

  // Turns all pixels off.
  void clear() {
    strip.clear();
    strip.show();
  }

  // Flashes the whole strip a few times.
  void flash(uint32_t color, int times, int delayMs) {
    for (int i = 0; i < times; i++) {
      setAll(color);
      show();
      delay(delayMs);
      clear();
      delay(delayMs);
    }
  }

  // Shows one game colour using the palette's NeoPixel colour.
  void showGameColour(GameColour colour, int holdMs) {
    clear();
    for (int i = 0; i < PALETTE_SIZE; i++) {
      if (PALETTE[i].id == colour) {
        setAll(PALETTE[i].neoColor);
        break;
      }
    }
    show();
    delay(holdMs);
    clear();
  }

  // Plays a rainbow startup animation.
  void rainbow(int waitMs) {
    for (long hue = 0; hue < 3L * 65536; hue += 256) {
      for (int i = 0; i < strip.numPixels(); i++) {
        int pixelHue = hue + (i * 65536L / strip.numPixels());
        strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixelHue)));
      }
      strip.show();
      delay(waitMs);
    }
  }

  // Makes an RGB colour value for simple effects.
  uint32_t color(byte r, byte g, byte b) {
    return strip.Color(r, g, b);
  }
};


// BUZZER CLASS
// Handles all game sounds.

class BuzzerManager {
public:
  // Sets up the buzzer pin.
  void begin() {
    pinMode(BUZZER_PIN, OUTPUT);
  }

  // Plays a happy correct-answer sound.
  void success() {
    tone(BUZZER_PIN, 1000, 120); delay(150);
    tone(BUZZER_PIN, 1400, 150); delay(180);
    noTone(BUZZER_PIN);
  }

  // Plays a low wrong-answer sound.
  void fail() {
    tone(BUZZER_PIN, 250, 250); delay(300);
    noTone(BUZZER_PIN);
  }

  // Plays a rising bonus sound.
  void bonus() {
    tone(BUZZER_PIN,  900, 100); delay(120);
    tone(BUZZER_PIN, 1200, 100); delay(120);
    tone(BUZZER_PIN, 1500, 150); delay(180);
    noTone(BUZZER_PIN);
  }

  // Plays a timeout warning sound.
  void timeout() {
    tone(BUZZER_PIN, 400, 150); delay(180);
    tone(BUZZER_PIN, 300, 220); delay(250);
    noTone(BUZZER_PIN);
  }
};



// BUTTON CLASS
// Button is wired from A0 to GND, so INPUT_PULLUP is used.
// Not pressed = HIGH, pressed = LOW.

class ButtonManager {
public:
  // Starts the button pin using Arduino's internal pull-up resistor.
  void begin() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
  }

  // Returns true when the button is currently pressed.
  bool isPressed() {
    return digitalRead(BUTTON_PIN) == LOW;
  }

  // Waits for one clean button press, but gives up after timeoutMs.
  bool waitForPress(unsigned long timeoutMs) {
    unsigned long start = millis();

    while (millis() - start < timeoutMs) {
      if (isPressed()) {
        delay(30); // Simple debounce delay.
        if (isPressed()) {
          waitForRelease();
          return true;
        }
      }
      delay(10);
    }

    return false;
  }

  // Waits until the button is released so one press does not count twice.
  void waitForRelease() {
    while (isPressed()) {
      delay(10);
    }
    delay(30); // Simple debounce after release.
  }
};


// RFID CLASS
// Reads player ID and player name from RFID cards.

class RfidReader {
private:
  MFRC522 rfid;
  MFRC522::MIFARE_Key key;

  // Authenticates the selected RFID data block.
  bool authenticateBlock(byte block) {
    MFRC522::StatusCode status = rfid.PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_A,
      block,
      &key,
      &(rfid.uid)
    );

    if (status != MFRC522::STATUS_OK) {
      Serial.print("Auth failed: ");
      Serial.println(rfid.GetStatusCodeName(status));
      return false;
    }
    return true;
  }

  // Stops communication with the current card.
  void haltCard() {
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

public:
  RfidReader() : rfid(SS_PIN, RST_PIN) {}

  // Starts SPI and the RC522 reader.
  void begin() {
    SPI.begin();
    rfid.PCD_Init();
    for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;
  }

  // Reads player data from block 4 of a card.
  bool readPlayer(int &playerID, String &playerName) {
    if (!rfid.PICC_IsNewCardPresent()) return false;
    if (!rfid.PICC_ReadCardSerial())   return false;
    if (!authenticateBlock(RFID_DATA_BLOCK)) {
      haltCard();
      return false;
    }

    byte buffer[18];
    byte size = sizeof(buffer);

    MFRC522::StatusCode status = rfid.MIFARE_Read(RFID_DATA_BLOCK, buffer, &size);
    if (status != MFRC522::STATUS_OK) {
      Serial.print("Read failed: ");
      Serial.println(rfid.GetStatusCodeName(status));
      haltCard();
      return false;
    }

    playerID = buffer[0];
    playerName = "";
    for (byte i = 1; i < 16 && buffer[i] != 0; i++) {
      playerName += (char)buffer[i];
    }

    haltCard();
    return true;
  }
};


// COLOUR SENSOR CLASS
// Reads TCS3200 raw RGB pulses and detects nearest game colour.

class ColourSensor {
private:
  // Reads one TCS3200 colour channel.
  unsigned long readChannel(bool s2, bool s3) {
    digitalWrite(TCS_S2, s2);
    digitalWrite(TCS_S3, s3);
    delay(10);
    return pulseIn(TCS_OUT, LOW, 500000UL);
  }

  // Calculates distance between current reading and saved colour.
  float distanceTo(unsigned long r, unsigned long g, unsigned long b, const KnownColor &known) {
    return sqrt(
      pow((float)r - (float)known.r, 2) +
      pow((float)g - (float)known.g, 2) +
      pow((float)b - (float)known.b, 2)
    );
  }

public:
  // Sets up TCS3200 pins and frequency scaling.
  void begin() {
    pinMode(TCS_S0,  OUTPUT);
    pinMode(TCS_S1,  OUTPUT);
    pinMode(TCS_S2,  OUTPUT);
    pinMode(TCS_S3,  OUTPUT);
    pinMode(TCS_OUT, INPUT);

    digitalWrite(TCS_S0, HIGH); // 20% scaling
    digitalWrite(TCS_S1, LOW);  // 20% scaling
  }

  // Detects the closest saved card colour.
  GameColour detect() {
    unsigned long r = readChannel(LOW,  LOW);   // Red filter
    unsigned long g = readChannel(HIGH, HIGH);  // Green filter
    unsigned long b = readChannel(LOW,  HIGH);  // Blue filter

    Serial.print("RAW(");
    Serial.print(r); Serial.print(", ");
    Serial.print(g); Serial.print(", ");
    Serial.print(b); Serial.println(")");

    float bestDist = UNKNOWN_THRESHOLD;
    GameColour best = UNKNOWN_COLOUR;

    for (int i = 0; i < PALETTE_SIZE; i++) {
      float d = distanceTo(r, g, b, PALETTE[i]);
      if (d < bestDist) {
        bestDist = d;
        best = PALETTE[i].id;
      }
    }

    return best;
  }

  // Converts a game colour enum into readable text.
  String nameOf(GameColour c) {
    switch (c) {
      case BLACK_COLOUR:  return "BLACK";
      case GREEN_COLOUR:  return "GREEN";
      case YELLOW_COLOUR: return "YELLOW";
      case PURPLE_COLOUR: return "PURPLE";
      default:            return "UNKNOWN";
    }
  }
};


// MAIN GAME CLASS
// whole game flow.

class CircuitQuestGame {
private:
  DisplayManager display;
  LedManager leds;
  BuzzerManager buzzer;
  ButtonManager button;
  RfidReader rfid;
  ColourSensor colourSensor;

  int scores[MAX_PLAYERS];
  bool skipNextTurn[MAX_PLAYERS];
  int currentPlayer;

  // Moves to the next player.
  void nextPlayer() {
    currentPlayer = (currentPlayer + 1) % MAX_PLAYERS;
  }

  // Changes a player's score without allowing negative scores.
  void changeScore(int playerIndex, int amount) {
    scores[playerIndex] = max(0, scores[playerIndex] + amount);
  }

  // Shows the welcome screen.
  void showWelcome() {
    display.clear();
    display.print("Circuit Quest");
    display.setCursor(0, 1);
    display.print("Starting...");
    leds.rainbow(20);
    delay(1000);
  }

  // Shows whose turn it is.
  void showCurrentPlayer() {
    display.clear();
    display.print("Player ");
    display.print(currentPlayer + 1);
    display.setCursor(0, 1);
    display.print("Take your turn");
  }

  // Shows all player scores.
  void showScores() {
    display.clear();
    display.print("P1:");
    display.print(scores[0]);
    display.print(" P2:");
    display.print(scores[1]);
    display.setCursor(0, 1);
    display.print("P3:");
    display.print(scores[2]);
    display.print(" P4:");
    display.print(scores[3]);
  }

  // Shows a spinning/random event animation on the LCD.
  void lcdEventAnimation(const String outcomes[], int count, int finalIndex) {
    display.clear();
    display.print("Generating...");
    delay(500);

    for (int i = 0; i < 10; i++) {
      int idx = random(0, count);
      display.clear();
      display.print("Player ");
      display.print(currentPlayer + 1);
      display.setCursor(0, 1);
      display.print(outcomes[idx]);
      delay(180);
    }

    display.clear();
    display.print("Player ");
    display.print(currentPlayer + 1);
    display.setCursor(0, 1);
    display.print(outcomes[finalIndex]);
    delay(1000);
  }

  // Applies a random RFID reward or punishment.
  void applyRandomRFIDOutcome(int playerIndex) {
    const int outcomeCount = 6;
    String outcomes[outcomeCount] = {
      "+10 points",
      "-5 points",
      "Extra turn!",
      "Skip next",
      "+15 points",
      "Double bonus"
    };

    int finalIndex = random(0, outcomeCount);
    lcdEventAnimation(outcomes, outcomeCount, finalIndex);

    switch (finalIndex) {
      case 0:
        changeScore(playerIndex, 10);
        buzzer.success();
        leds.flash(leds.color(0, 255, 0), 2, 150);
        break;

      case 1:
        changeScore(playerIndex, -5);
        buzzer.fail();
        leds.flash(leds.color(255, 0, 0), 2, 150);
        break;

      case 2:
        display.clear();
        display.print("Extra turn!");
        display.setCursor(0, 1);
        display.print("Play again");
        buzzer.success();
        leds.flash(leds.color(0, 0, 255), 3, 120);
        delay(1500);
        currentPlayer = (currentPlayer - 1 + MAX_PLAYERS) % MAX_PLAYERS;
        break;

      case 3:
        skipNextTurn[playerIndex] = true;
        display.clear();
        display.print("Skip next turn");
        buzzer.fail();
        leds.flash(leds.color(255, 150, 0), 3, 120);
        delay(1500);
        break;

      case 4:
        changeScore(playerIndex, 15);
        display.clear();
        display.print("+15 points!");
        buzzer.success();
        leds.flash(leds.color(0, 255, 150), 3, 120);
        delay(1500);
        break;

      case 5:
        changeScore(playerIndex, 20);
        display.clear();
        display.print("Double bonus!");
        display.setCursor(0, 1);
        display.print("+20 points!");
        buzzer.bonus();
        leds.rainbow(20);
        delay(1500);
        break;
    }
  }

  // Waits for the button, then scans the colour once.
  bool waitForColourInput(GameColour expectedColour, unsigned long timeoutMs, bool showLiveStatus) {
    unsigned long start = millis();

    while (millis() - start < timeoutMs) {
      unsigned long elapsed = millis() - start;
      unsigned long remaining = timeoutMs - elapsed;

      if (showLiveStatus) {
        display.setCursor(0, 1);
        display.print("Press button    ");
      }

      // Only scan the colour sensor after the player presses the button.
      if (button.waitForPress(remaining)) {
        GameColour detected = colourSensor.detect();

        Serial.print("Button scan -> ");
        Serial.println(colourSensor.nameOf(detected));

        display.clear();
        display.print("Detected:");
        display.setCursor(0, 1);
        display.print(colourSensor.nameOf(detected));
        delay(600);

        return detected == expectedColour;
      }
    }

    return false;
  }

  // Runs one RFID event tile.
  void runRFIDTile(bool timedMode) {
    unsigned long timeoutMs = timedMode ? 8000 : 16000;

    display.clear();
    display.print("RFID Event Tile");
    display.setCursor(0, 1);
    display.print(timedMode ? "Scan in 8 sec" : "Scan your card");

    unsigned long start = millis();
    bool scanned = false;

    while (millis() - start < timeoutMs) {
      int cardPlayerID = -1;
      String cardName = "";

      if (rfid.readPlayer(cardPlayerID, cardName)) {
        scanned = true;

        if (cardPlayerID == currentPlayer + 1) {
          display.clear();
          display.print(cardName);
          display.setCursor(0, 1);
          display.print("Card accepted");
          delay(900);
          applyRandomRFIDOutcome(currentPlayer);
        } else {
          display.clear();
          display.print("Wrong player");
          display.setCursor(0, 1);
          display.print("card scanned");
          buzzer.fail();
          leds.flash(leds.color(255, 0, 0), 3, 150);
          changeScore(currentPlayer, -5);
        }
        break;
      }
    }

    if (!scanned) {
      display.clear();
      display.print("RFID timeout");
      display.setCursor(0, 1);
      display.print("-5 points");
      buzzer.timeout();
      leds.flash(leds.color(255, 0, 0), 3, 150);
      changeScore(currentPlayer, -5);
    }
  }

  // Runs one colour card tile.
  void runColourTile(bool timedMode) {
    GameColour target = (GameColour)random(0, 4);
    unsigned long timeoutMs = timedMode ? 4000 : 8000;

    display.clear();
    display.print("Show colour:");
    display.setCursor(0, 1);
    display.print(colourSensor.nameOf(target));
    leds.showGameColour(target, 800);

    display.clear();
    display.print("Show ");
    display.print(colourSensor.nameOf(target));
    display.setCursor(0, 1);
    display.print(timedMode ? "Press: 4 sec" : "Press button");

    bool matched = waitForColourInput(target, timeoutMs, false);

    if (matched) {
      display.clear();
      display.print("Correct!");
      display.setCursor(0, 1);
      display.print("+10 points");
      buzzer.success();
      leds.showGameColour(target, 600);
      leds.flash(leds.color(0, 255, 0), 2, 150);
      changeScore(currentPlayer, 10);
    } else {
      display.clear();
      display.print("Wrong/timeout");
      display.setCursor(0, 1);
      display.print("-5 points");
      buzzer.fail();
      leds.flash(leds.color(255, 0, 0), 3, 150);
      changeScore(currentPlayer, -5);
    }
  }

  // Runs the memory pattern challenge.
  bool runMemoryChallenge(int patternLength, unsigned long totalTimeoutMs) {
    GameColour pattern[10];

    for (int i = 0; i < patternLength; i++) {
      pattern[i] = (GameColour)random(0, 4);
    }

    display.clear();
    display.print("Watch closely!");
    display.setCursor(0, 1);
    display.print("Remember it...");
    delay(700);

    for (int i = 0; i < patternLength; i++) {
      leds.showGameColour(pattern[i], 500);
      leds.clear();
      delay(250);
    }

    display.clear();
    display.print("Your turn!");
    display.setCursor(0, 1);
    display.print("Use colour cards");
    delay(800);

    unsigned long challengeStart = millis();

    for (int i = 0; i < patternLength; i++) {
      unsigned long elapsed = millis() - challengeStart;
      if (elapsed >= totalTimeoutMs) return false;

      display.clear();
      display.print("Step ");
      display.print(i + 1);
      display.print(" of ");
      display.print(patternLength);
      display.setCursor(0, 1);
      display.print("Press button...");

      if (!waitForColourInput(pattern[i], totalTimeoutMs - elapsed, false)) return false;

      leds.showGameColour(pattern[i], 300);
      buzzer.success();
      delay(200);
    }

    return true;
  }

  // Runs one memory tile.
  void runMemoryTile(bool timedMode) {
    int patternLength = timedMode ? 3 : 4;
    unsigned long timeoutMs = timedMode ? 7000 : 14000;

    display.clear();
    display.print("Memory Tile");
    display.setCursor(0, 1);
    display.print(timedMode ? "Fast mode!" : "Watch pattern");
    delay(1000);

    bool passed = runMemoryChallenge(patternLength, timeoutMs);

    if (passed) {
      display.clear();
      display.print("Pattern right!");
      display.setCursor(0, 1);
      display.print("+12 points");
      buzzer.success();
      leds.flash(leds.color(0, 255, 0), 3, 120);
      changeScore(currentPlayer, 12);
    } else {
      display.clear();
      display.print("Pattern wrong");
      display.setCursor(0, 1);
      display.print("-5 points");
      buzzer.fail();
      leds.flash(leds.color(255, 0, 0), 3, 150);
      changeScore(currentPlayer, -5);
    }
  }

  // Runs one bonus tile.
  void runBonusTile() {
    display.clear();
    display.print("Bonus Tile!");
    display.setCursor(0, 1);
    display.print("+5 points");
    buzzer.bonus();
    leds.rainbow(20);
    changeScore(currentPlayer, 5);
  }

public:
  CircuitQuestGame() : currentPlayer(0) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
      scores[i] = 0;
      skipNextTurn[i] = false;
    }
  }

  // Starts every hardware part and shows welcome screen.
  void begin() {
    Serial.begin(9600);
    rfid.begin();
    display.begin();
    leds.begin();
    buzzer.begin();
    button.begin();
    colourSensor.begin();
    randomSeed(analogRead(A0));
    showWelcome();
  }

  // Runs one complete player's turn.
  void update() {
    if (skipNextTurn[currentPlayer]) {
      display.clear();
      display.print("Player ");
      display.print(currentPlayer + 1);
      display.setCursor(0, 1);
      display.print("Turn skipped!");
      buzzer.fail();
      delay(1800);
      skipNextTurn[currentPlayer] = false;
      nextPlayer();
      return;
    }

    showCurrentPlayer();
    delay(1200);

    TileType tile = (TileType)random(0, 7);

    switch (tile) {
      case RFID_TILE:         runRFIDTile(false);   break;
      case COLOUR_TILE:       runColourTile(false); break;
      case MEMORY_TILE:       runMemoryTile(false); break;
      case TIMED_RFID_TILE:   runRFIDTile(true);    break;
      case TIMED_COLOUR_TILE: runColourTile(true);  break;
      case TIMED_MEMORY_TILE: runMemoryTile(true);  break;
      case BONUS_TILE:        runBonusTile();       break;
    }

    showScores();
    delay(2500);
    nextPlayer();
  }
};



CircuitQuestGame game;

void setup() {
  game.begin();
}

//loop game
void loop() {
  game.update();
}
