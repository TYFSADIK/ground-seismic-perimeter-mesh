// Seismic node: ADXL355 (SPI) @ 1 kHz -> features -> classifier -> signed LoRa mesh packet (SX1262).
#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include "mesh.h"
#include "node_secrets.h"   // NODE_ID, MESH_KEY[] : generated per node, never committed

using namespace seismic;

SX1262 radio = new Module(8, 14, 12, 13);   // Heltec WiFi LoRa 32 V3: NSS, DIO1, RST, BUSY
// Heltec V3: the SX1262 owns SPI pins SCK 9 / MOSI 10 / MISO 11 / NSS 8. The ADXL355 shares that bus
// on its own chip-select (GPIO 5), so every access is wrapped in its own SPI transaction.
constexpr int ADXL_CS = 5;
const SPISettings kAdxlSpi(5000000, MSBFIRST, SPI_MODE0);
hw_timer_t* timer = nullptr;
volatile bool sample_due = false;
float window[kN];
int widx = 0;
uint16_t seq = 0;
FeatureExtractor fx;
Classifier clf;
Relay relay;
StoreForward<128> sf;
uint32_t last_ack_ms = 0;

void IRAM_ATTR on_timer() { sample_due = true; }

float adxl355_read_z_g() {
  // ZDATA3..1 at 0x0E, 20-bit two's complement, 256000 LSB/g at +-2 g
  SPI.beginTransaction(kAdxlSpi);
  digitalWrite(ADXL_CS, LOW);
  SPI.transfer((0x0E << 1) | 1);
  uint32_t b3 = SPI.transfer(0), b2 = SPI.transfer(0), b1 = SPI.transfer(0);
  digitalWrite(ADXL_CS, HIGH);
  SPI.endTransaction();
  int32_t raw = int32_t((b3 << 12) | (b2 << 4) | (b1 >> 4));
  if (raw & 0x80000) raw -= 1 << 20;
  return raw / 256000.0f;
}

void adxl355_init() {
  pinMode(ADXL_CS, OUTPUT); digitalWrite(ADXL_CS, HIGH);
  SPI.begin(9, 11, 10);   // SCK, MISO, MOSI (shared with the SX1262)
  auto wr = [](uint8_t reg, uint8_t v) {
    SPI.beginTransaction(kAdxlSpi);
    digitalWrite(ADXL_CS, LOW); SPI.transfer(reg << 1); SPI.transfer(v); digitalWrite(ADXL_CS, HIGH);
    SPI.endTransaction();
  };
  wr(0x28, 0x02);   // FILTER: ODR 1 kHz
  wr(0x2C, 0x01);   // RANGE +-2 g
  wr(0x2D, 0x00);   // POWER_CTL: measurement mode
}

bool send(Packet& p) {
  return radio.transmit(reinterpret_cast<uint8_t*>(&p), sizeof(p)) == RADIOLIB_ERR_NONE;
}

void setup() {
  Serial.begin(115200);
  adxl355_init();
  radio.begin(915.0, 125.0, 9, 7, 0x12, 14);   // SF9 / 125 kHz; stay within RSS-247
  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &on_timer);
  timerAlarm(timer, 1000, true, 0);            // 1 kHz
}

void loop() {
  if (sample_due) {
    sample_due = false;
    window[widx++] = adxl355_read_z_g();
    if (widx == kN) {
      widx = 0;
      Features f = fx.compute(window);
      Decision d = clf.classify(f);
      if (d.label != Label::Quiet && d.label != Label::Wind) {
        Packet p = make_detection(NODE_ID, ++seq, millis() / 1000, f, d);
        sign(p, MESH_KEY, sizeof(MESH_KEY));
        bool gateway_alive = millis() - last_ack_ms < 30000;
        if (!gateway_alive || !send(p)) sf.push(p);
      }
    }
  }
  // Receive: relay floods, track gateway acks, replay backlog when the gateway is back.
  uint8_t buf[64];
  if (radio.receive(buf, sizeof(Packet)) == RADIOLIB_ERR_NONE) {
    Packet p; memcpy(&p, buf, sizeof(p));
    if (p.type == uint8_t(PktType::Ack) && crc_ok(p)) last_ack_ms = millis();
    else if (relay.should_forward(p, NODE_ID)) send(p);
  }
  Packet back;
  if (millis() - last_ack_ms < 30000 && sf.size() && sf.pop(back)) send(back);
}
