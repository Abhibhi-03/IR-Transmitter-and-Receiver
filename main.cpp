// main.cpp
// Implemented by Mohammed Aeraf Khan and Abhi Patel
// Infrared Communication Device

#include "mbed.h"
#include "mstd_iterator"
#include "LCDi2c.h"
#include "FATFileSystem.h"
#include "BlockDevice.h"
#include "Keypad.h"
#include <string>
#include <map>

// ===================== Headers =====================

PwmOut irLed(PTC8); // IR LED pin
InterruptIn irReceiver(PTC3); // IR receiver pin
DigitalIn button(PTC12);    // Button to begin begin recording message
LCDi2c lcd(PTC11, PTC10, LCD20x4); // Initializing LCD

DigitalIn printButton(SW2);     // For dumping the pulse widths in the terminal to plot the wave

Keypad kpad(PTA6, PTA4, PTA26, PTA27, PTA1, PTA9, PTA8, PTA7);

PwmOut statusReady(PTB19);
PwmOut statusBusy(PTB18);

// ===================== Variables and Constant Declaration =====================

// IR transmission parameters
const int BIT_ONE_DURATION_US = 1500;
const int BIT_ZERO_DURATION_US = 1000;
const int START_BIT_DURATION_US = 3000;
const int STOP_BIT_DURATION_US = 2000;
const int BIT_END_PAUSE_US = 500;

// Pulse Width Tolerance 
const int START_BIT_MIN_WIDTH_US = 2900;
const int START_BIT_MAX_WIDTH_US = 3100;
const int STOP_BIT_MIN_WIDTH_US = 1900;
const int STOP_BIT_MAX_WIDTH_US = 2100;
const int DATA_BIT_ONE_MIN_WIDTH_US = 1400;
const int DATA_BIT_ONE_MAX_WIDTH_US = 1600;
const int DATA_BIT_ZERO_MIN_WIDTH_US = 900;
const int DATA_BIT_ZERO_MAX_WIDTH_US = 1100;

Timer timer;

volatile int lastEdgeTime;
volatile bool datareceived = false;
volatile bool startBitDetected = false;

// Buffer for received message
char receivedMessage[1024];
int receivedMessageIndex = 0;

// Buffer and index for bits
bool bitBuffer[8];
int bitIndex = 0;

// for printing pulse width in the terminal (used to plot the wave)
#define PULSE_WIDTH_BUFFER_SIZE 1024
int pulseWidths[PULSE_WIDTH_BUFFER_SIZE];
int pulseWidthIndex = 0;

// Instantiate and mount the filesystem for SD card operations
FATFileSystem fs("fs");
BlockDevice *bd = BlockDevice::get_default_instance();


// ===================== Transmitter Functions =====================

void sendBit(bool bit) {
    irLed = 0.5f; // LED on
    wait_us(bit ? BIT_ONE_DURATION_US : BIT_ZERO_DURATION_US); // 6 ms for '1', 3 ms for '0'
    irLed = 0.0f; // LED off
    wait_us(BIT_END_PAUSE_US); // Wait 1 ms before sending the next bit
}

void sendByte(uint8_t byte) {
    for (int i = 0; i < 8; ++i) {
        sendBit(byte & (1 << i));
    }
}

void sendStartBit() {
    irLed = 0.5f; // LED on
    wait_us(START_BIT_DURATION_US); // 6 ms for '1', 3 ms for '0'
    irLed = 0.0f; // LED off
    wait_us(BIT_END_PAUSE_US); // Wait 1 ms before sending the next bit
}

void sendStopBit() {
    irLed = 0.5f; // LED on
    wait_us(STOP_BIT_DURATION_US); // 6 ms for '1', 3 ms for '0'
    irLed = 0.0f; // LED off
    wait_us(BIT_END_PAUSE_US); // Wait 1 ms before sending the next bit
}

void sendString(const std::string& str) {
    int count = 0; // Counter for characters sent
    sendStartBit();
    for (char ch : str) {
        uint8_t byte = static_cast<uint8_t>(ch); // Convert character to its ASCII value
        sendByte(byte); // Send the ASCII value of the character

        count++;

        if (count == 5) { // After sending 5 characters, wait before sending the next
            wait_us(50000); // Wait 50 ms before sending the next set of characters
            count = 0; // Reset counter for the next set
        }
    }
    sendStopBit();
}

// ===================== Receiver Functions =====================

void processBit(bool bit) {
    if (bitIndex < 8) {
        bitBuffer[bitIndex++] = bit;
    }

    if (bitIndex == 8) {
        // Convert bits to byte
        uint8_t byte = 0;
        for (int i = 0; i < 8; ++i) {
            if (bitBuffer[i]) {
                byte |= 1 << i;
            }
        }

        // Reset for the next byte
        bitIndex = 0;

        // Convert byte to character and store it
        if (receivedMessageIndex < 255) {
            receivedMessage[receivedMessageIndex++] = static_cast<char>(byte);
        }
    }
}

bool isStartBit(int pulseWidth) {
    return pulseWidth > START_BIT_MIN_WIDTH_US && pulseWidth < START_BIT_MAX_WIDTH_US;
}

bool isStopBit(int pulseWidth) {
    return pulseWidth > STOP_BIT_MIN_WIDTH_US && pulseWidth < STOP_BIT_MAX_WIDTH_US;
}

bool isDataBit(int pulseWidth) {
    return (pulseWidth > DATA_BIT_ZERO_MIN_WIDTH_US && pulseWidth < DATA_BIT_ZERO_MAX_WIDTH_US) ||
           (pulseWidth > DATA_BIT_ONE_MIN_WIDTH_US && pulseWidth < DATA_BIT_ONE_MAX_WIDTH_US);
}

bool determineBitValue(int pulseWidth) {
    return pulseWidth > DATA_BIT_ONE_MIN_WIDTH_US && pulseWidth < DATA_BIT_ONE_MAX_WIDTH_US;
}


void commonEdgeHandler(int currentEdgeTime) {
    if (lastEdgeTime != 0) {
        int pulseWidth = currentEdgeTime - lastEdgeTime;
        pulseWidths[pulseWidthIndex++] = pulseWidth;

        if (isStartBit(pulseWidth)) {
            startBitDetected = true;
            bitIndex = 0;
        } else if (startBitDetected && isDataBit(pulseWidth)) {
            bool bit = determineBitValue(pulseWidth);
            processBit(bit);
        } else if (startBitDetected && isStopBit(pulseWidth)) {
            startBitDetected = false;
            datareceived = true; // Indicate that the message was fully received.
        }
    }
    lastEdgeTime = currentEdgeTime;
}

void riseHandler() {
    commonEdgeHandler(timer.read_us());
}

void fallHandler() {
    commonEdgeHandler(timer.read_us());
}

// ===================== Keypad Matrix Functions =====================

std::string getPressedNumbers() {
    std::string numbers = "";
    char key;
    int released = 1;

    while(1) {
        key = kpad.ReadKey();

        if(key == '\0')
            released = 1;

        if((key != '\0') && (released == 1)) {
            if (key == '#') {
                return numbers;     // hash key as enter
            } else {
                numbers += key;
                released = 0;
            }
        }
    }
}

std::string convertToWords(const std::string& numbers) {
    std::map<char, std::string> num_to_char = {
        {'1', "1"},
        {'2', "2abc"},
        {'3', "3def"},
        {'4', "4ghi"},
        {'5', "5jkl"},
        {'6', "6mno"},
        {'7', "7pqrs"},
        {'8', "8tuv"},
        {'9', "9wxyz"},
        {'0', " 0"}
    };

    std::string word = "";
    char prev_num = '\0';
    int count = 0;

    for (char num : numbers) {
        if (num == '*') {
            word += "";
            prev_num = '\0';
        } else if (num == prev_num) {
            count = (count + 1) % num_to_char[num].size();
            word.pop_back(); // remove the previous character
            word += num_to_char[num][count];
        } else {
            prev_num = num;
            count = 0;
            word += num_to_char[num][count];
        }
    }
    return word;
}

// ===================== Status Indicator Functions =====================

void setStatusReady() {
    statusReady = 1;
    statusBusy = 0;
}

void setStatusBusy() {
    statusReady = 0;
    statusBusy = 1;
}

// ===================== Side Task Function =====================

void printdatatoTerminal() {
    for (int i = 0; i < pulseWidthIndex; i++) {
        printf("%d\n", pulseWidths[i]);
    }
}

// ===================== LCD display Function =====================

void displayReceivedData(const char* message) {
    lcd.cls();
    lcd.printf("%s", message);
}

// ===================== SD Card Function =====================

void saveToSDCard(const char* data) {
    if (data == NULL) {
        return;
    }
    // file was manually created in the SD Card
    FILE *file = fopen("/fs/received_data.txt", "a");
    if (file != NULL) {
        fprintf(file, "%s\n", data);
        fclose(file);
    } else {
    }
}

// ===================== Main Code Functions =====================

int main() {
    setStatusReady();
    irLed.period(1.0 / 38000.0);
    irLed = 0.0f;
    // const char* str = "Aeraf & Abhi\n";      // for testing

    if (bd == NULL) {
        return -1;
    }

    int err = fs.mount(bd);
    if (err != 0) {
        // printf("Mount failed, error: %d. Formatting...\n", err);
        err = fs.reformat(bd);
        if (err != 0) {
            // printf("Failed to format filesystem, error: %d\n", err);
            return -1;
        }
    }

    timer.start();
    irReceiver.rise(&riseHandler);
    irReceiver.fall(&fallHandler);

    lcd.cls();

    while (true) {
        if (button == 0) {
            setStatusBusy();
            string numbers = getPressedNumbers();
            wait_us(5000);
            string sentence = convertToWords(numbers);
            setStatusReady();
            // printf("Characters: %s\n", sentence.c_str());    // Debug print to check the caracters sent.
            sendString(sentence);
            wait_us(5000); // Delay to prevent immediate reenterring in the if block
        }

        // Optionally we check if the data being sent is the same as the data being received (indicating that the receiver of the same device
        // is picking up the signal which it is transmitting) We decided to print the signal that was being transmitted on the LCD for debugging purposes.

        if (datareceived) {
            datareceived = false; // Reset the flag after processing the data

            // Printing received data
            receivedMessage[receivedMessageIndex] = '\0'; // Null-terminate the string
            printf("Received data: %s\n", receivedMessage);
            saveToSDCard(receivedMessage); // Call function to save data
            displayReceivedData(receivedMessage); // Call fucntion to display received message on the LCD display

            // Reset for next message
            receivedMessageIndex = 0;

        }

        if (printButton == 0) {
            printdatatoTerminal();
            wait_us(500000); // Delay to prevent immediate reenterring in the if block
        }

        ThisThread::sleep_for(10ms);
    }

    fs.unmount(); // Unmount the file system (clean up)
}