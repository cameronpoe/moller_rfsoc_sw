// Shared lines
const int clk  = 0;   // RX pin  - shared clock
const int data = 1;   // TX pin  - shared data

// Individual latch lines, one per DSA
const int LE[4] = {2, 3, 4, 5};   // DSA 1,2,3,4

// 0 dB = all bits zero. Order: [16,8,4,2,1,0.5] dB
int attenSet[6] = {0,0,0,0,0,0};
int del = 5;

// Startup flashes LEDs in new way compared to 2019/2020 Arduino code
void startupSignature() {
  // 8 quick on-offs
  for (int i = 0; i < 8; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(80);
    digitalWrite(LED_BUILTIN, LOW);
    delay(80);
  }

  delay(300);  // small gap between the two patterns

  // 4 slow on-offs
  for (int i = 0; i < 4; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(400);
    digitalWrite(LED_BUILTIN, LOW);
    delay(400);
  }

  // off for one second
  digitalWrite(LED_BUILTIN, LOW);
  delay(1000);

  // then stay on
  digitalWrite(LED_BUILTIN, HIGH);
}

void setup() {
  pinMode(clk, OUTPUT);
  pinMode(data, OUTPUT);
  for (int i = 0; i < 4; i++) pinMode(LE[i], OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  startupSignature();

  // idle state
  digitalWrite(clk, LOW);
  digitalWrite(data, LOW);
  for (int i = 0; i < 4; i++) digitalWrite(LE[i], LOW);

  // program each of the four attenuators
  for (int dsa = 0; dsa < 4; dsa++) {

    // shift in 6 bits, MSB first
    for (int b = 0; b < 6; b++) {
      digitalWrite(data, attenSet[b] ? HIGH : LOW);
      digitalWrite(clk, HIGH);
      delay(del);
      digitalWrite(clk, LOW);
      delay(del);
    }

    // pulse this DSA's latch to commit the setting
    delay(del);
    digitalWrite(LE[dsa], HIGH);
    delay(del);
    digitalWrite(LE[dsa], LOW);
    delay(del);
  }

  // leave LED on to indicate setup completed
  digitalWrite(LED_BUILTIN, HIGH);
}

void loop() {
  // nothing - LED stays on, attenuators are set
}