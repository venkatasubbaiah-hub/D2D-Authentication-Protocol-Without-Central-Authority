#include "main.h"
#include "cmox_crypto.h"
#include "cmox_hmac.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
extern UART_HandleTypeDef huart1;

#define UART_TIMEOUT 10000

uint8_t secretKey[32] = {
    0x10,0x21,0x32,0x43,
    0x54,0x65,0x76,0x87,
    0x98,0xA9,0xBA,0xCB,
    0xDC,0xED,0xFE,0x0F,
    0x11,0x22,0x33,0x44,
    0x55,0x66,0x77,0x88,
    0x99,0xAA,0xBB,0xCC,
    0xDD,0xEE,0xFF,0x00
};

char rx[160];

void sendLine(const char *message)
{
    HAL_UART_Transmit(
        &huart1,
        (uint8_t *)message,
        strlen(message),
        5000
    );
}

int readLine(char *buffer, uint16_t capacity)
{
    uint16_t index = 0;
    uint8_t byte;
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < UART_TIMEOUT &&
           index < capacity - 1)
    {
        if (HAL_UART_Receive(
                &huart1, &byte, 1, 100) == HAL_OK)
        {
            if (byte == '\n')
            {
                buffer[index] = '\0';
                return index > 0;
            }

            if (byte != '\r' &&
                byte >= 32 &&
                byte <= 126)
            {
                buffer[index++] = (char)byte;
            }
        }
    }

    buffer[index] = '\0';
    return 0;
}

int hexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;

    return -1;
}

int hexToBytes(
    const char *hex,
    uint8_t *output,
    uint16_t length)
{
    if (strlen(hex) != length * 2)
        return 0;

    for (uint16_t i = 0; i < length; i++)
    {
        int high = hexValue(hex[i * 2]);
        int low = hexValue(hex[i * 2 + 1]);

        if (high < 0 || low < 0)
            return 0;

        output[i] = (high << 4) | low;
    }

    return 1;
}

void bytesToHex(
    const uint8_t *data,
    uint16_t length,
    char *output)
{
    const char hex[] = "0123456789ABCDEF";

    for (uint16_t i = 0; i < length; i++)
    {
        output[i * 2] = hex[data[i] >> 4];
        output[i * 2 + 1] = hex[data[i] & 0x0F];
    }

    output[length * 2] = '\0';
}

int calculateHMAC(
    const char *prefix,
    const uint8_t *nonce,
    uint8_t output[32])
{
    uint8_t message[22];
    uint32_t messageLength = strlen(prefix) + 16;
    size_t macLength = 0;

    memcpy(message, prefix, strlen(prefix));
    memcpy(
        message + strlen(prefix),
        nonce,
        16
    );

    cmox_status_t status = cmox_hmac_compute(
        CMOX_HMAC_SHA256_ALGO,
        secretKey,
        sizeof(secretKey),
        message,
        messageLength,
        output,
        32,
        &macLength
    );

    return (status == CMOX_SUCCESS && macLength == 32);
}

int constantTimeCompare(
    const uint8_t *a,
    const uint8_t *b,
    uint16_t length)
{
    uint8_t difference = 0;

    for (uint16_t i = 0; i < length; i++)
        difference |= a[i] ^ b[i];

    return difference == 0;
}
void runAuthentication(void)
{
    uint8_t nonce[16];
    uint8_t receivedMAC[32];
    uint8_t calculatedMAC[32];

    char challengeHex[33];
    char responseHex[65];
    char message[100];

    sendLine("STM32_READY\n");

    // Receive HI.
    if (!readLine(rx, sizeof(rx)) ||
        strcmp(rx, "HI") != 0)
    {
        sendLine("AUTH_FAILED:HANDSHAKE\n");
        return;
    }

    sendLine("HELLO\n");

    // Receive ESP32 challenge.
    if (!readLine(rx, sizeof(rx)) ||
        strncmp(rx, "CHALLENGE:", 10) != 0)
    {
        sendLine("AUTH_FAILED:CHALLENGE\n");
        return;
    }

    if (!hexToBytes(rx + 10, nonce, 16))
    {
        sendLine("AUTH_FAILED:NONCE_FORMAT\n");
        return;
    }

    // Verify ESP32 has the correct key by calculating
    // the expected ESP32 response later.
    // First prove STM32 possession of the key.
    if (!calculateHMAC(
            "STM32|",
            nonce,
            calculatedMAC))
    {
        sendLine("AUTH_FAILED:HMAC\n");
        return;
    }

    bytesToHex(
        calculatedMAC,
        32,
        responseHex
    );

    snprintf(
        message,
        sizeof(message),
        "RESP_STM32:%s\n",
        responseHex
    );

    sendLine(message);

    // Receive ESP32 HMAC.
    if (!readLine(rx, sizeof(rx)) ||
        strncmp(rx, "RESP_ESP32:", 11) != 0)
    {
        sendLine("AUTH_FAILED:ESP32_RESPONSE\n");
        return;
    }

    if (!hexToBytes(
            rx + 11,
            receivedMAC,
            32))
    {
        sendLine("AUTH_FAILED:ESP32_FORMAT\n");
        return;
    }

    if (!calculateHMAC(
            "ESP32|",
            nonce,
            calculatedMAC))
    {
        sendLine("AUTH_FAILED:HMAC\n");
        return;
    }

    if (!constantTimeCompare(
            receivedMAC,
            calculatedMAC,
            32))
    {
        sendLine("AUTH_FAILED:ESP32_VERIFICATION\n");
        return;
    }

    sendLine("AUTH_SUCCESS\n");

    // Wait for ESP32 completion confirmation.
    if (!readLine(rx, sizeof(rx)) ||
        strcmp(rx, "MUTUAL_AUTH_SUCCESS") != 0)
    {
        sendLine("AUTH_FAILED:FINAL_CONFIRMATION\n");
        return;
    }

    sendLine("MUTUAL_AUTHENTICATION_SUCCESS\n");
}
int main(void)
{
    HAL_Init();

    SystemClock_Config();

    MX_GPIO_Init();

    MX_USART1_UART_Init();

    // Initialize CMOX library.
    if (cmox_initialize(NULL) != CMOX_SUCCESS)
    {
        Error_Handler();
    }

    HAL_Delay(1000);

    runAuthentication();

    while (1)
    {
        HAL_Delay(1000);
    }
}
