#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Crypto.h>
#include <SHA256.h>
#include <string.h>

// ========================================
// PIN CONFIGURATION
// ========================================

#define STM32_RX 32
#define STM32_TX 33

#define GREEN_LED 2
#define RED_LED 4

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDRESS 0x3C

#define UART_TIMEOUT 10000

// ========================================
// UART
// ========================================

HardwareSerial STM32(2);

// ========================================
// OLED
// ========================================

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);

// ========================================
// SHARED SECRET KEY
// Must match STM32 exactly
// ========================================

uint8_t secretKey[32] = {
  0x10, 0x21, 0x32, 0x43,
  0x54, 0x65, 0x76, 0x87,
  0x98, 0xA9, 0xBA, 0xCB,
  0xDC, 0xED, 0xFE, 0x0F,
  0x11, 0x22, 0x33, 0x44,
  0x55, 0x66, 0x77, 0x88,
  0x99, 0xAA, 0xBB, 0xCC,
  0xDD, 0xEE, 0xFF, 0x00
};

// ========================================
// GLOBAL VARIABLES
// ========================================

uint8_t nonce[16];
uint8_t mac[32];

SHA256 hash;

bool authenticated = false;
bool oledAvailable = false;

// ========================================
// OLED STATUS
// ========================================

void showStatus(const char *line1, const char *line2) {

  if (!oledAvailable) return;

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 10);
  display.println(line1);

  display.println();

  display.println(line2);

  display.display();
}


String readSTM32(uint32_t timeout = UART_TIMEOUT) {

  String message = "";

  uint32_t start = millis();

  while (millis() - start < timeout) {

    while (STM32.available()) {

      uint8_t c = STM32.read();

      // Print every received byte in hexadecimal.
      Serial.printf("UART BYTE: 0x%02X\n", c);

      if (c == '\r') {
        continue;
      }

      if (c == '\n') {

        message.trim();

        if (message.length() > 0) {

          Serial.println(
            "STM32 -> ESP32: [" + message + "]"
          );

          return message;
        }

        continue;
      }

      // Accept printable ASCII characters only.
      if (c >= 32 && c <= 126) {

        if (message.length() < 150) {
          message += (char)c;
        }
      }
    }

    delay(1);
  }

  Serial.println("UART RECEIVE TIMEOUT");

  return "";
}

// ========================================
// UART TRANSMIT
// ========================================

void sendSTM32(const String &message) {

  STM32.println(message);

  Serial.println(
    "ESP32 -> STM32: " + message
  );
}

// ========================================
// BYTE TO HEX
// ========================================

String toHex(const uint8_t *data, size_t length) {

  const char hex[] = "0123456789ABCDEF";

  String result = "";

  for (size_t i = 0; i < length; i++) {

    result += hex[data[i] >> 4];

    result += hex[data[i] & 0x0F];
  }

  return result;
}

// ========================================
// HEX TO BYTE
// ========================================

bool fromHex(
  const String &input,
  uint8_t *output,
  size_t length
) {

  if (input.length() != length * 2) {
    return false;
  }

  for (size_t i = 0; i < length; i++) {

    char a = input[i * 2];
    char b = input[i * 2 + 1];

    int high =
      (a >= '0' && a <= '9') ? a - '0' :
      (a >= 'A' && a <= 'F') ? a - 'A' + 10 :
      (a >= 'a' && a <= 'f') ? a - 'a' + 10 : -1;

    int low =
      (b >= '0' && b <= '9') ? b - '0' :
      (b >= 'A' && b <= 'F') ? b - 'A' + 10 :
      (b >= 'a' && b <= 'f') ? b - 'a' + 10 : -1;

    if (high < 0 || low < 0) {
      return false;
    }

    output[i] = (high << 4) | low;
  }

  return true;
}

// ========================================
// HMAC SHA256
// ========================================

void calculateHMAC(
  const char *prefix,
  uint8_t output[32]
) {

  hash.resetHMAC(
    secretKey,
    sizeof(secretKey)
  );

  hash.update(
    prefix,
    strlen(prefix)
  );

  hash.update(
    nonce,
    sizeof(nonce)
  );

  hash.finalizeHMAC(
    secretKey,
    sizeof(secretKey),
    output,
    32
  );
}

// ========================================
// CONSTANT TIME COMPARISON
// ========================================

bool verifyHMAC(
  const uint8_t *received,
  const uint8_t *expected,
  size_t length
) {

  uint8_t difference = 0;

  for (size_t i = 0; i < length; i++) {

    difference |= received[i] ^ expected[i];
  }

  return difference == 0;
}

// ========================================
// AUTHENTICATION
// ========================================

bool authentication() {

  authenticated = false;

  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, HIGH);

  showStatus(
    "SECURE D2D",
    "Waiting for STM32"
  );

  // STEP 1: WAIT FOR STM32_READY

  String response = readSTM32();

  if (response != "STM32_READY") {

    Serial.println("STM32 NOT DETECTED");

    return false;
  }

  Serial.println("STM32 DETECTED");

  // STEP 2: SEND HI

  sendSTM32("HI");

  response = readSTM32();

  if (response != "HELLO") {

    Serial.println("HANDSHAKE FAILED");

    return false;
  }

  Serial.println("BASIC HANDSHAKE SUCCESS");

  // STEP 3: GENERATE RANDOM NONCE

  for (int i = 0; i < 16; i++) {

    nonce[i] = (uint8_t)esp_random();
  }

  String nonceHex = toHex(nonce, 16);

  // STEP 4: SEND CHALLENGE

  sendSTM32(
    "CHALLENGE:" + nonceHex
  );

  showStatus(
    "CHALLENGE SENT",
    "Verifying STM32"
  );

  // STEP 5: RECEIVE STM32 HMAC

  response = readSTM32();

  if (!response.startsWith("RESP_STM32:")) {

    Serial.println("INVALID STM32 RESPONSE");

    return false;
  }

  uint8_t receivedMAC[32];

  if (!fromHex(
        response.substring(11),
        receivedMAC,
        32
      )) {

    Serial.println("INVALID HMAC FORMAT");

    return false;
  }

  // STEP 6: VERIFY STM32

  calculateHMAC(
    "STM32|",
    mac
  );

  if (!verifyHMAC(
        receivedMAC,
        mac,
        32
      )) {

    Serial.println("STM32 AUTHENTICATION FAILED");

    return false;
  }

  Serial.println("STM32 AUTHENTICATION VERIFIED");

  // STEP 7: ESP32 RESPONSE

  calculateHMAC(
    "ESP32|",
    mac
  );

  sendSTM32(
    "RESP_ESP32:" + toHex(mac, 32)
  );

  showStatus(
    "STM32 VERIFIED",
    "Verifying ESP32"
  );

  // STEP 8: WAIT FOR STM32 VERIFICATION

  response = readSTM32();

  if (response != "AUTH_SUCCESS") {

    Serial.println("ESP32 AUTHENTICATION FAILED");

    return false;
  }

  Serial.println("ESP32 AUTHENTICATION VERIFIED");

  // STEP 9: FINAL CONFIRMATION

  sendSTM32("MUTUAL_AUTH_SUCCESS");

  authenticated = true;

  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(RED_LED, LOW);

  showStatus(
    "AUTHENTICATION",
    "SUCCESS"
  );

  Serial.println("==============================");
  Serial.println("MUTUAL AUTHENTICATION SUCCESS");
  Serial.println("==============================");

  return true;
}

// ========================================
// SETUP
// ========================================

void setup() {

  Serial.begin(115200);

  STM32.begin(
    115200,
    SERIAL_8N1,
    STM32_RX,
    STM32_TX
  );

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, HIGH);

  Wire.begin(21, 22);

  oledAvailable = display.begin(
    SSD1306_SWITCHCAPVCC,
    OLED_ADDRESS
  );

  if (!oledAvailable) {
    Serial.println("OLED NOT DETECTED");
  }

  showStatus(
    "SECURE D2D",
    "Starting..."
  );

  delay(1500);

  Serial.println();
  Serial.println("==============================");
  Serial.println("ESP32 STARTED");
  Serial.println("==============================");

  if (authentication()) {

    Serial.println("AUTHENTICATION COMPLETED");

  } else {

    authenticated = false;

    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED, HIGH);

    showStatus(
      "AUTHENTICATION",
      "FAILED"
    );

    Serial.println("AUTHENTICATION FAILED");
  }
}

// ========================================
// LOOP
// ========================================

void loop() {

  delay(1000);
}