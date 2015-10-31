#include <SPI.h>

// connector -> arduino
// 1 -> Vcc
// 2 -> Clk 13
// 3 -> Load 10
// 4 -> Out 12
// 5 -> Gnd

const int debounceThreshold = 20;
const int byteCount = 14;
const int keyCount = 8 * byteCount;

int volumePin = A5;
byte volume = 127;
int shiftLoadPin = 10;
boolean reported[keyCount];
boolean detected[keyCount];
unsigned long detectTime[keyCount];
boolean unplugged = false;

const byte pitches[keyCount] = {
    36, 38, 40, 42, 43, 41, 39, 37,
    45, 47, 44, 0, 0, 0, 48, 46, 
    41, 43, 45, 47, 48, 46, 44, 42,
    49, 51, 53, 55, 56, 54, 52, 50,
    57, 59, 61, 0, 0, 62, 60, 58,
    0, 63, 65, 67, 68, 66, 64, 0,
    69, 71, 73, 75, 76, 74, 72, 70,
    77, 79, 82, 81, 84, 83, 80, 78,
    41, 43, 45, 47, 48, 46, 44, 42,
    49, 51, 53, 55, 56, 54, 52, 50,
    57, 59, 61, 0, 0, 62, 60, 58,
    0, 63, 65, 67, 68, 66, 64, 0,
    69, 71, 73, 75, 76, 74, 72, 70,
    77, 79, 81, 83, 84, 82, 80, 78};

const byte channels[keyCount] = {
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 0, 0, 0, 3, 3, 
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 0, 0, 1, 1, 1,
    0, 1, 1, 1, 1, 1, 1, 0,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 0, 0, 2, 2, 2,
    0, 2, 2, 2, 2, 2, 2, 0,
    2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2};

void setup()
{
    Serial.begin(115200);
    pinMode(shiftLoadPin, OUTPUT);

    SPI.begin();
    SPI.setBitOrder(LSBFIRST);
    SPI.setClockDivider(SPI_CLOCK_DIV2);
    SPI.setDataMode(SPI_MODE3);

    for (int i = 0; i < keyCount; i++)
    {
        reported[i] = true;
        detected[i] = true;
        detectTime[i] = 0;
    }
}

void loop()
{
    byte prevVolume = volume;
    volume = (double)analogRead(volumePin) * 127.0 / 1023.0;
    if (volume != prevVolume)
        outputMidi(0xB0, 4, 0x07, volume);

    latchInputs();
    unsigned long now = millis();
    for (int i = 0; i < byteCount; i++)
    {
        byte keyByte = SPI.transfer(0);
        for (int note = i*8; note < (i+1)*8; note++)
        {
            boolean keyState = keyByte & 0x01;
            keyByte = keyByte >> 1;

            unsigned long stableTime = now - detectTime[note];

            if (keyState != reported[note] &&
                stableTime >= debounceThreshold)
            {
                reported[note] = keyState;
                if (keyState)
                    outputMidi(0x80, channels[note], pitches[note], 0);
                else
                    outputMidi(0x90, channels[note], pitches[note], 0x7f);
            }

            if (keyState != detected[note])
            {
                detected[note] = keyState;
                detectTime[note] = now;
            }
        }
    }
}

void outputMidi(byte command, byte channel, byte data2, byte data3)
{
    if (!channel)
        return;

    byte data1 = command | (channel - 1);

    Serial.write(data1);
    Serial.write(data2);
    Serial.write(data3);
}

void latchInputs()
{
    digitalWrite(shiftLoadPin, LOW);
    digitalWrite(shiftLoadPin, HIGH);
}
