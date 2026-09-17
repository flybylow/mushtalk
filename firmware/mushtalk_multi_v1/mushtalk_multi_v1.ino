/*
 * mushtalk_multi_v2 — multi-WAV player with sound on/off (serial + button).
 *
 * Board  : ESP32S3 Dev Module   PSRAM: OPI PSRAM   USB CDC On Boot: Enabled
 * Amp    : MAX98357A   BCLK=GPIO5   LRC=GPIO6   DIN=GPIO7
 *
 * Mute button: momentary switch between MUTE_BTN and GND (internal pull-up).
 * Default MUTE_BTN=0 is the onboard BOOT button — no extra wiring.
 * For a panel on/off button, wire it to GPIO4 and set MUTE_BTN to 4.
 *
 * Serial Monitor (115200), type then Enter:
 *   number 0..N / name  -> play that sample
 *   on                  -> enable sound
 *   off                 -> disable sound (mute)
 *   m                   -> toggle sound on/off
 *   l                   -> list samples + state
 *   stop                -> auto-cycle off (conductor sends this on connect)
 *   c                   -> toggle auto-cycle
 */

#include <ESP_I2S.h>
#include <LittleFS.h>

#define I2S_BCLK 5
#define I2S_LRC  6
#define I2S_DOUT 7
#define MUTE_BTN 0          // BOOT on the Dev Module; use 4 for an external button
#define WAV_RATE 44100
#define DEBOUNCE_MS 50
#define CHUNK 512

I2SClass I2S;

const char *SAMPLES[] = {
  "/drone_low.wav", "/pad_mid.wav", "/bell_hit.wav",
  "/spike_blip.wav", "/tick.wav", "/sample.wav",
};
const int N = sizeof(SAMPLES) / sizeof(SAMPLES[0]);

bool soundEnabled = true;
bool autoCycle    = false;   // wait for Serial / conductor; type 'c' to cycle
bool inPlay       = false;
int  cycleIdx     = 0;

bool btnLast = HIGH;
unsigned long btnAt = 0;

bool startI2S() {
  I2S.setPins(I2S_BCLK, I2S_LRC, I2S_DOUT);
  return I2S.begin(I2S_MODE_STD, WAV_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                   I2S_SLOT_MODE_MONO, I2S_STD_SLOT_BOTH);
}

void setSound(bool on) {
  if (on == soundEnabled) return;
  soundEnabled = on;
  if (on) {
    if (!startI2S()) Serial.println("I2S begin FAILED");
  } else {
    I2S.end();                 // stop BCLK → MAX98357A shuts down, silent now
  }
  Serial.printf("sound: %s\n", on ? "ON" : "OFF");
}

void handleButton() {
  bool now = digitalRead(MUTE_BTN);
  unsigned long t = millis();
  if (now == btnLast || (t - btnAt) < DEBOUNCE_MS) return;
  btnAt = t;
  btnLast = now;
  if (now == LOW) setSound(!soundEnabled);   // press = toggle
}

void listSamples() {
  Serial.println("samples:");
  for (int i = 0; i < N; i++) Serial.printf("  %d  %s\n", i, SAMPLES[i]);
  Serial.printf("sound: %s | auto-cycle: %s\n",
                soundEnabled ? "ON" : "OFF", autoCycle ? "on" : "off");
}

// PCM payload starts at the 'data' chunk (not always byte 44) and has a size.
// Extra RIFF chunks after that (e.g. C2PA) must not be sent to the amp.
bool wavPcmRange(const uint8_t *buf, size_t len, size_t *off, size_t *nbytes) {
  if (len < 44) return false;
  size_t i = 12;
  while (i + 8 <= len) {
    uint32_t id = (uint32_t)buf[i] | ((uint32_t)buf[i+1]<<8)
                | ((uint32_t)buf[i+2]<<16) | ((uint32_t)buf[i+3]<<24);
    uint32_t sz = (uint32_t)buf[i+4] | ((uint32_t)buf[i+5]<<8)
                | ((uint32_t)buf[i+6]<<16) | ((uint32_t)buf[i+7]<<24);
    i += 8;
    if (id == 0x61746164) {                      // 'data'
      if (i + sz > len) sz = len - i;
      *off = i;
      *nbytes = sz;
      return true;
    }
    i += sz;
  }
  return false;
}

void play(int i) {
  if (i < 0 || i >= N) { Serial.printf("no sample %d\n", i); return; }
  if (!soundEnabled || inPlay) return;
  inPlay = true;

  File f = LittleFS.open(SAMPLES[i], "r");
  if (!f) { Serial.printf("cannot open %s\n", SAMPLES[i]); inPlay = false; return; }

  size_t len = f.size();
  uint8_t *buf = (uint8_t *) ps_malloc(len);
  if (!buf) buf = (uint8_t *) malloc(len);
  if (!buf) { Serial.println("out of memory"); f.close(); inPlay = false; return; }

  size_t got = f.read(buf, len);
  f.close();

  size_t off = 0, nbytes = 0;
  if (!wavPcmRange(buf, got, &off, &nbytes)) {
    Serial.println("not a wav"); free(buf); inPlay = false; return;
  }

  Serial.printf("[%d] playing %s (%u pcm bytes)\n", i, SAMPLES[i], (unsigned) nbytes);

  size_t end = off + nbytes;
  while (off < end && soundEnabled) {
    handleButton();
    handleSerial();
    if (!soundEnabled) break;
    size_t n = CHUNK;
    if (off + n > end) n = end - off;
    I2S.write(buf + off, n);
    off += n;
  }
  // Keep BCLK alive with a short zero tail so the MAX98357A does not pop on underrun.
  if (soundEnabled) {
    uint8_t z[CHUNK] = {0};
    for (int k = 0; k < 4; k++) I2S.write(z, sizeof z);
  }
  free(buf);
  inPlay = false;
}

int resolve(const String &s) {
  String t = s; t.trim();
  if (t.length() == 0) return -1;
  bool numeric = true;
  for (unsigned k = 0; k < t.length(); k++) if (!isDigit(t[k])) { numeric = false; break; }
  if (numeric) return t.toInt();
  for (int i = 0; i < N; i++) {
    String name = String(SAMPLES[i]).substring(1);
    String base = name.substring(0, name.length() - 4);
    if (t.equalsIgnoreCase(name) || t.equalsIgnoreCase(base)) return i;
  }
  return -2;
}

void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  if (line.equalsIgnoreCase("on"))  { setSound(true);  return; }
  if (line.equalsIgnoreCase("off")) { setSound(false); return; }
  if (line.equalsIgnoreCase("m"))   { setSound(!soundEnabled); return; }
  if (line.equalsIgnoreCase("l"))   { listSamples(); return; }
  if (line.equalsIgnoreCase("stop")) {
    autoCycle = false;
    Serial.println("auto-cycle: off");
    return;
  }
  if (line.equalsIgnoreCase("c"))   {
    autoCycle = !autoCycle;
    Serial.printf("auto-cycle: %s\n", autoCycle ? "on" : "off");
    return;
  }

  int i = resolve(line);
  if (i == -2) { Serial.printf("unknown: %s  (type 'l' to list)\n", line.c_str()); return; }
  if (!soundEnabled) { Serial.println("sound is OFF (type 'on' or press the button)"); return; }
  autoCycle = false;
  Serial.println("(auto-cycle off)");
  play(i);
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n--- mushtalk_multi_v2 ---");

  pinMode(MUTE_BTN, INPUT_PULLUP);
  btnLast = digitalRead(MUTE_BTN);

  if (!LittleFS.begin(true)) { Serial.println("LittleFS mount FAILED"); return; }
  Serial.println("LittleFS mounted");

  if (!startI2S()) { Serial.println("I2S begin FAILED"); return; }
  Serial.println("I2S ready");
  Serial.println("mute: press BOOT (or the GPIO button) to toggle sound\n");
  listSamples();
  Serial.println();
}

void loop() {
  handleButton();
  handleSerial();

  if (autoCycle && soundEnabled) {
    play(cycleIdx);
    cycleIdx = (cycleIdx + 1) % N;
    for (int t = 0; t < 1500 && autoCycle && soundEnabled; t += 20) {
      handleButton();
      handleSerial();
      delay(20);
    }
  } else {
    delay(20);
  }
}
