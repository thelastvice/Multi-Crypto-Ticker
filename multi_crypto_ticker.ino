#include "ESP8266WiFi.h"
#include "WiFiClientSecure.h"
#include "ArduinoJson.h"
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <stdio.h>


// Define the number of devices we have in the chain and the hardware interface
// NOTE: These pin numbers will probably not work with your hardware and may
// need to be adapted
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 8

#define CLK_PIN   D5 // or SCK
#define DATA_PIN  D7 // or MOSI
#define CS_PIN    D6 // or SS

// SPI hardware interface
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// We always wait a bit between updates of the display
#define  DELAYTIME  25  // in milliseconds

// Number of coins
#define TOTAL_COINS 18

const char* ssid = "put_your_wifi_ssid";
const char* password = "put_your_wifi_password";

const char* coinbaseHost = "api.pro.coinbase.com";
const char* coinbaseFingerprint = "9C B0 72 05 A4 F9 D7 4E 5A A4 06 5E DD 1F 1C 27 5D C2 F1 48";

const char* binanceHost = "api.binance.com";
const char* binanceFingerprint = "41 82 D2 BA 64 E3 36 F1 3C 5E 49 05 2A A0 AA CB D0 F7 2B B7";


struct Coin {
  String url;
  String ticker; 
  bool isCoinbaseCoin;
};

struct Coin cryptoCoins[TOTAL_COINS]; 

void scrollText(char *p) {
    uint8_t charWidth;
    uint8_t cBuf[8];  // this should be ok for all built-in fonts
    mx.clear();
    
    while (*p != '\0')
    {
        charWidth = mx.getChar(*p++, sizeof(cBuf) / sizeof(cBuf[0]), cBuf);
        
        for (uint8_t i=0; i<=charWidth; i++)  // allow space between characters
        {
            mx.transform(MD_MAX72XX::TSL);
            if (i < charWidth)
                mx.setColumn(0, cBuf[i]);
            delay(DELAYTIME);
        }
    }
}

void connectToWIFI() {
    Serial.println();
    Serial.print("connecting to ");
    Serial.println(ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

}


JsonObject& getJsonObject(String url, bool isCoinbaseCoin) {
    const size_t capacity = (isCoinbaseCoin) ? JSON_OBJECT_SIZE(7) + 252 : JSON_OBJECT_SIZE(2) + 74;
    DynamicJsonBuffer jsonBuffer(capacity);
    
    // Use WiFiClientSecure class to create TLS connection
    WiFiClientSecure client;
    client.setTimeout(10000);
    
    const char* host = (isCoinbaseCoin) ? coinbaseHost : binanceHost;
    const char* fingerprint = (isCoinbaseCoin) ? coinbaseFingerprint : binanceFingerprint;
    Serial.println(host);
    Serial.printf("Using fingerprint '%s'\n", fingerprint);
    client.setFingerprint(fingerprint);
    
    if (!client.connect(host, 443)) {
        Serial.println("connection failed");
        scrollText("Connection failed!");
        // No further work should be done if the connection failed
        return jsonBuffer.parseObject(client);
    }
    Serial.println(F("Connected!"));
    
    // Send HTTP Request
    String httpEnding = (isCoinbaseCoin) ? " HTTP/1.1\r\n" : " HTTP/1.0\r\n";
    client.print(String("GET ") + url + httpEnding +
                 "Host: " + host + "\r\n" +
                 "User-Agent: BuildFailureDetectorESP8266\r\n" +
                 "Connection: close\r\n\r\n");
    Serial.println("request sent");
    
    // Check HTTP Status
    char status[32] = {0};
    client.readBytesUntil('\r', status, sizeof(status));
    if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
        Serial.print(F("Unexpected response: "));
        Serial.println(status);
        return jsonBuffer.parseObject(client);
    }
    
    // Skip HTTP headers
    char endOfHeaders[] = "\r\n\r\n";
    if (!client.find(endOfHeaders)) {
        Serial.println(F("Invalid response"));
        //scrollText("Invalid Response");
    }
    
    // Parse JSON object
    JsonObject& root = jsonBuffer.parseObject(client);
    if (!root.success()) {
        Serial.println(F("Parsing failed!"));
        //scrollText("JSON Parse Failed!");
    }
    
    // Disconnect
    client.stop();
    jsonBuffer.clear();
    return root;
}

float bitcoinPrice = 0.0;

float convertToUSD(float cryptoPrice) {
    if (bitcoinPrice == 0.0) { getBitcoinPrice(); }
    return bitcoinPrice * cryptoPrice;
}

void getBitcoinPrice() {
    JsonObject& root = getJsonObject("/api/v1/ticker/price?symbol=BTCUSDC", false);
    Serial.print("The Bitcoin price is:");
    Serial.println(root["price"].as<float>());
    bitcoinPrice = root["price"].as<float>();
}

void getCoinPrice(String url, String cryptoName, bool isCoinbaseCoin) {
    JsonObject& root = getJsonObject(url, isCoinbaseCoin);
    Serial.println("==========");
    Serial.println(F("Response:"));
    Serial.print("Symbol: ");
    Serial.println(root["symbol"].as<char*>());
    Serial.print("Price: ");
    Serial.println(root["price"].as<char*>());
    
    float cryptoPrice = root["price"].as<float>();
    cryptoPrice = (isCoinbaseCoin) ? cryptoPrice : convertToUSD(cryptoPrice);
    Serial.println(cryptoPrice);
    Serial.println("==========");
    String output = cryptoName + " $" + String(cryptoPrice);
    Serial.println(output);
    
    // Update the bitcoinPrice if the User requests the Bitcoin Price
    bitcoinPrice = (cryptoName == "BTC") ? cryptoPrice : bitcoinPrice;
    
    char *cstr = new char[output.length() + 1];
    strcpy(cstr, output.c_str());
    scrollText(cstr);
    delete [] cstr;
}

void configureCoins() { 
  Coin BTC = { .url = "/products/BTC-USD/ticker", .ticker = "BTC", .isCoinbaseCoin = true }; 
  Coin ETH = { .url = "/products/ETH-USD/ticker", .ticker = "ETH", .isCoinbaseCoin = true };
  Coin XRP = { .url = "/products/XRP-USD/ticker", .ticker = "XRP", .isCoinbaseCoin = true };
  Coin BCH = { .url = "/products/BCH-USD/ticker", .ticker = "BCH", .isCoinbaseCoin = true };
  Coin LTC = { .url = "/products/LTC-USD/ticker", .ticker = "LTC", .isCoinbaseCoin = true };

  
  // Lets add in some Binanace Coins
  Coin BNB = { .url = "/api/v1/ticker/price?symbol=BNBBTC", .ticker = "BNB", .isCoinbaseCoin = false };
  Coin EOS = { .url = "/api/v1/ticker/price?symbol=EOSBTC", .ticker = "EOS", .isCoinbaseCoin = false }; 
  Coin XMR = { .url = "/api/v1/ticker/price?symbol=XMRBTC", .ticker = "XMR", .isCoinbaseCoin = false };
  Coin XLM = { .url = "/api/v1/ticker/price?symbol=XLMBTC", .ticker = "XLM", .isCoinbaseCoin = false };
  Coin ADA = { .url = "/api/v1/ticker/price?symbol=ADABTC", .ticker = "ADA", .isCoinbaseCoin = false };
  Coin DASH = { .url = "/api/v1/ticker/price?symbol=DASHBTC", .ticker = "DASH", .isCoinbaseCoin = false };
  Coin ETC = { .url = "/api/v1/ticker/price?symbol=ETCBTC", .ticker = "ETC", .isCoinbaseCoin = false };
  Coin XEM = { .url = "/api/v1/ticker/price?symbol=XEMBTC", .ticker = "XEM", .isCoinbaseCoin = false };
  Coin ZEC = { .url = "/api/v1/ticker/price?symbol=ZECBTC", .ticker = "ZEC", .isCoinbaseCoin = false }; 
  Coin QTUM = { .url = "/api/v1/ticker/price?symbol=QTUMBTC", .ticker = "QTUM", .isCoinbaseCoin = false};
  Coin ZRX = { .url = "/api/v1/ticker/price?symbol=ZRXBTC", .ticker = "ZXR", .isCoinbaseCoin = false};
  Coin KMD = { .url = "/api/v1/ticker/price?symbol=KMDBTC", .ticker = "KMD", .isCoinbaseCoin = false};
  Coin GNT = { .url = "/api/v1/ticker/price?symbol=GNTBTC", .ticker = "GNT", .isCoinbaseCoin = false};



  cryptoCoins[0] =  BTC;
  cryptoCoins[1] =  ETH;
  cryptoCoins[2] =  XRP;
  cryptoCoins[3] =  BCH;
  cryptoCoins[4] =  LTC;
  cryptoCoins[5] =  BNB;
  cryptoCoins[6] =  EOS;
  cryptoCoins[7] =  XMR;
  cryptoCoins[8] =  XLM;
  cryptoCoins[9] =  ADA;
  cryptoCoins[10] =  DASH;
  cryptoCoins[11] =  ETC;
  cryptoCoins[12] =  XEM;
  cryptoCoins[13] =  ZEC;
  cryptoCoins[14] =  QTUM;
  cryptoCoins[15] =  ZRX;
  cryptoCoins[16] =  KMD;
  cryptoCoins[17] =  GNT;

}

void getAllCoinPrices() {
  Coin currentCoin;
  for(int index=0; index < TOTAL_COINS; index++) {
    currentCoin = cryptoCoins[index];
    getCoinPrice(currentCoin.url, currentCoin.ticker, currentCoin.isCoinbaseCoin);
  }
}

void setup() {
    mx.begin();
    Serial.begin(115200);
    connectToWIFI();
    configureCoins();
}

void loop() {
    getAllCoinPrices();
}
