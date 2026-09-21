#include "io.h"

const uint8_t ADC_PINS[4]   = { PIN_ADC_CH1, PIN_ADC_CH2, PIN_ADC_CH3, PIN_ADC_CH4 };
const uint8_t DI_PINS[4]    = { PIN_DI_1, PIN_DI_2, PIN_DI_3, PIN_DI_4 };
const uint8_t RELAY_PINS[4] = { PIN_RELAY_1, PIN_RELAY_2, PIN_RELAY_3, PIN_RELAY_4 };

volatile bool diEvent[4]        = { false, false, false, false };
volatile bool btn1Event         = false;
volatile bool btn2Event         = false;
volatile bool btnBuiltinEvent   = false;

static uint8_t  relayShadow[4]  = { 0, 0, 0, 0 };
static uint32_t pulseUntil[4]   = { 0, 0, 0, 0 };   // millis(); 0 = not pulsing

// Single shared ISR - mirrors the original isr_Pins() logic.
static void IRAM_ATTR isrPins() {
  for (uint8_t i = 0; i < 4; i++) {
    if (digitalRead(DI_PINS[i]) == LOW) diEvent[i] = true;
  }
  if (digitalRead(PIN_BUTTON_1) == LOW) btn1Event = true;
  if (digitalRead(PIN_BUTTON_2) == LOW) btn2Event = true;
  if (digitalRead(PIN_BUTTON_BUILTIN) == HIGH) btnBuiltinEvent = true;
}

static void driveRelay(uint8_t idx, uint8_t on) {
  relayShadow[idx] = on ? 1 : 0;
  digitalWrite(RELAY_PINS[idx], relayShadow[idx] ? HIGH : LOW);
}

void ioInit(uint8_t enableMask, uint8_t safeMask) {
  pinMode(PIN_LED, OUTPUT);

  for (uint8_t i = 0; i < 4; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    driveRelay(i, (safeMask >> i) & 0x01);
    pinMode(ADC_PINS[i], ANALOG);
    pinMode(DI_PINS[i], INPUT_PULLUP);
  }
  (void)enableMask;  // enable is enforced in ioSetRelay/ioPulseRelay

  pinMode(PIN_BUTTON_BUILTIN, INPUT_PULLUP);
  pinMode(PIN_BUTTON_1, INPUT_PULLUP);
  pinMode(PIN_BUTTON_2, INPUT_PULLUP);

  for (uint8_t i = 0; i < 4; i++)
    attachInterrupt(digitalPinToInterrupt(DI_PINS[i]), isrPins, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_1), isrPins, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_2), isrPins, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_BUILTIN), isrPins, CHANGE);

  // External power rail on (VEXT LOW). heltec_setup() also does this via
  // heltec_ve(true); kept here so the rail is up before the OLED splash.
  pinMode(PIN_PWR_MGM, OUTPUT);
  digitalWrite(PIN_PWR_MGM, LOW);
}

// Oversampling: el ADC del ESP32-S3 salta varias cuentas muestra a muestra
// aun con una entrada perfectamente estable (confirmado en banco: multimetro
// fijo en 1.7988V/0.8879V, pero analogRead() de una sola muestra saltaba
// bastante; luego confirmado con osciloscopio -- la senal en si trae ruido).
// Se combinan N lecturas consecutivas por su **RMS** (valor eficaz,
// sqrt(mean(x^2))), no por promedio simple: a pedido del usuario, para
// quedarse con la medida "integral" de la senal ruidosa en vez de la media
// aritmetica. Nota: con ruido simetrico esto sesga el resultado levemente
// hacia arriba respecto al promedio real (RMS >= media siempre), pero para
// la amplitud de ruido observada aqui (decenas de cuentas sobre una base de
// cientos) el sesgo es de menos de 1 cuenta -- despreciable frente al ruido
// que se esta filtrando. Reduce el ruido en ~sqrt(N) antes de que le llegue
// al EMA de channels.cpp (doble filtraje: oversampling aqui + EMA a periodo
// fijo alla).
static const uint8_t ADC_OVERSAMPLE = 64;   // margen de tiempo disponible: el usuario se
                                             // conforma con un ciclo de lectura de 150ms

uint16_t ioReadAnalog(uint8_t ch) {
  if (ch > 3) return 0;
  uint64_t sumSq = 0;
  for (uint8_t i = 0; i < ADC_OVERSAMPLE; i++) {
    uint32_t s = analogRead(ADC_PINS[ch]);
    sumSq += (uint64_t)s * s;
  }
  return (uint16_t)lroundf(sqrtf((float)sumSq / (float)ADC_OVERSAMPLE));
}

uint8_t ioReadDigital(uint8_t ch) {
  if (ch > 3) return 0;
  return digitalRead(DI_PINS[ch]) ? 1 : 0;
}

uint8_t ioGetRelay(uint8_t idx) {
  if (idx > 3) return 0;
  return relayShadow[idx];
}

bool ioSetRelay(uint8_t idx, uint8_t on, uint8_t enableMask) {
  if (idx > 3) return false;
  if (!((enableMask >> idx) & 0x01)) return false;
  pulseUntil[idx] = 0;
  driveRelay(idx, on);
  return true;
}

bool ioPulseRelay(uint8_t idx, uint16_t ms, uint8_t enableMask, uint8_t safeMask) {
  if (idx > 3) return false;
  if (!((enableMask >> idx) & 0x01)) return false;
  uint8_t active = ((safeMask >> idx) & 0x01) ? 0 : 1;  // pulse opposite to safe level
  driveRelay(idx, active);
  pulseUntil[idx] = millis() + ms;
  if (pulseUntil[idx] == 0) pulseUntil[idx] = 1;         // avoid the "not pulsing" sentinel
  return true;
}

void ioServicePulses(uint8_t safeMask) {
  uint32_t now = millis();
  for (uint8_t i = 0; i < 4; i++) {
    if (pulseUntil[i] != 0 && (int32_t)(now - pulseUntil[i]) >= 0) {
      driveRelay(i, (safeMask >> i) & 0x01);
      pulseUntil[i] = 0;
    }
  }
}
