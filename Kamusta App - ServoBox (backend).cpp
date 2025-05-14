#include <Wire.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <MySQL_Connection.h>
#include <MySQL_Cursor.h>
#include <time.h>
#include <map>
#include <vector>
// Servo declarations
Servo servo_10;
Servo servo_9;
Servo servo_8;
Servo servo_7;
Servo servo_6;
Servo servo_5;
Servo servo_4;



const int buttonPin = 27;  // Manually close all servos
const int resetButtonPin = 25;  // Reset system
int buttonState = 0;
// Wi-Fi credentials
const char *ssid = "bagongwifi";
const char *password = "Onetwothree4";

// MySQL credentials
const char *mysql_host = "153.92.15.3";
const char *mysql_user = "u693536525_user";
const char *mysql_pass = "&y2G6?tH";
const char *mysql_db = "u693536525_kamusta";
bool servoStatus[7] = {false, false, false, false, false, false, false}; 
bool buttonPressed = false;
std::map<int, bool> servoStates;
std::vector<int> processedReminders;

std::vector<String> reminderIDs;
WiFiClient client;
MySQL_Connection conn((Client *)&client);

// Manila timezone UTC offset
const long GMT_OFFSET_SEC = 8 * 3600;  // Manila is UTC+8
const int DAYLIGHT_OFFSET_SEC = 0;

enum State {
    STARTUP,
    IDLE,
    DISPENSING1,
    DISPENSING2,
    DISPENSING3,
    DISPENSING4,
    DISPENSING5,
    DISPENSING6,
    DISPENSING7,
    FINAL_IDLE
};

State currentState = STARTUP;
unsigned long previousMillis = 0;
const unsigned long startupDuration = 2000;

void setup() {
    Serial.begin(115200);

    // Set up Wi-Fi connection
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi!");

    // Configure NTP to automatically sync time
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "2.ph.pool.ntp.org", "3.asia.pool.ntp.org", "0.asia.pool.ntp.org");

    // Connect to MySQL
    if (conn.connect(mysql_host, 3306, (char*)mysql_user, (char*)mysql_pass, (char*)mysql_db)) {
        Serial.println("Connected to database!");
    } else {
        Serial.println("Connection failed.");
    }

    // Attach servos
    servo_10.attach(15);
    servo_9.attach(2);
    servo_8.attach(16);
    servo_7.attach(17);
    servo_6.attach(18);
    servo_5.attach(19);
    servo_4.attach(22);

    
    pinMode(buttonPin, INPUT_PULLUP);  // Button for closing all servos
    pinMode(resetButtonPin, INPUT_PULLUP);  // Button for resetting system
    previousMillis = millis();
}

void loop() {
    unsigned long currentMillis = millis();

    // Check if reset button is pressed
    int resetButtonState = digitalRead(resetButtonPin);
    if (resetButtonState == LOW) {
        resetSystem();
        return;
    }

    // Check if the close all servos button is pressed
   
  buttonState = digitalRead(buttonPin); // Read the button state

    
    if (buttonState == LOW) {
        closeAllServos();  // Close all servos

      
        updateReminderStatus();
    }

    delay(200);
    // Fetch all reminders for the current date
    fetchAllReminders();


    
    
}
// Function that updates the reminder status for all opened servos

void updateReminderStatus() {
     if (processedReminders.empty()) {
        Serial.println("No reminders to update.");
        return;
    }

    MySQL_Cursor *cursor = new MySQL_Cursor(&conn);

    // Loop through all processed reminder IDs (those that caused the servos to open)
    for (int i = 0; i < processedReminders.size(); i++) {
        int r_id = processedReminders[i];  // Get the reminder ID that was triggered by the servo

        // Query to update the status of specific 'waiting' reminders
        String query = "UPDATE reminders SET r_status = 'completed' WHERE r_id = " + String(r_id) + " AND r_status = 'waiting'";

        // Execute the query
        bool success = cursor->execute(query.c_str());

        if (success) {
            Serial.println("Updated reminder ID " + String(r_id) + " status to 'completed'");
        } else {
            Serial.println("Failed to update reminder ID " + String(r_id));
        }
    }

    delete cursor;
}

void resetSystem() {
    Serial.println("System reset. Restarting...");
    closeAllServos();  // Close all servos before restarting
    delay(100);
    ESP.restart();  // Reboot the ESP32
}

void fetchAllReminders() {
    // Ensure MySQL connection is active
    if (!conn.connected()) {
        Serial.println("Not connected to MySQL. Reconnecting...");
        if (!conn.connect(mysql_host, 3306, (char*)mysql_user, (char*)mysql_pass, (char*)mysql_db)) {
            Serial.println("Reconnection failed.");
            return;
        }
        Serial.println("Reconnected to MySQL.");
    }

    MySQL_Cursor cursor(&conn);

    // Query to get the latest date from the reminders table
    if (!cursor.execute("SELECT MAX(r_date) FROM reminders")) {
        Serial.println("Failed to fetch the latest date.");
        return;
    }

    column_names *cols = cursor.get_columns();
    row_values *row = cursor.get_next_row();

    if (row) {
        String latestDate = row->values[0];
        Serial.println("Latest Date: " + latestDate);

        // Query to fetch all open reminders for the latest date
        String query = "SELECT r_time, r_medslot, r_id FROM reminders WHERE r_date = '" + latestDate + "' AND r_boxstatus = 'open' ORDER BY r_time ASC";
        if (!cursor.execute(query.c_str())) {
            Serial.println("Failed to fetch reminders.");
            return;
        }

        cols = cursor.get_columns();

        // Array to track reminders being processed
        std::vector<String> reminderIDs;

        // Open all servos for reminders
        while ((row = cursor.get_next_row())) {
            String r_time = row->values[0];
            int r_medslot = atoi(row->values[1]);
            String r_id = row->values[2];  // Reminder ID

            Serial.println("Processing reminder:");
            Serial.println("Time: " + r_time);
            Serial.println("Medication Slot: " + String(r_medslot));
            Serial.println("Reminder ID: " + r_id);

            // Open the servo for the current reminder
            Serial.println("Opening servo for reminder ID: " + r_id);
            switch (r_medslot) {
                case 1: openServo(servo_10, 0); break;
                case 2: openServo(servo_9, 1); break;
                case 3: openServo(servo_8, 2); break;
                case 4: openServo(servo_7, 3); break;
                case 5: openServoCustom(servo_6, 4); break;
                case 6: openServoCustom(servo_5, 5); break;
                case 7: openServoCustom(servo_4, 6); break;
                default: 
                    Serial.println("Invalid medication slot: " + String(r_medslot));
                    continue; // Skip invalid slots
            }

            // Add the reminder ID to the list for status updates
            reminderIDs.push_back(r_id);
        }

        // Wait for 60 seconds while keeping the MySQL connection alive
        if (!reminderIDs.empty()) {
            Serial.println("All servos are open. Waiting for 60 seconds...");
            unsigned long startTime = millis();
            while (millis() - startTime < 60000) {
                delay(100);  // Allow periodic reconnection checks
                if (!conn.connected()) {
                    Serial.println("MySQL connection lost during delay. Reconnecting...");
                    if (!conn.connect(mysql_host, 3306, (char*)mysql_user, (char*)mysql_pass, (char*)mysql_db)) {
                        Serial.println("Reconnection failed during delay.");
                        return;
                    }
                    Serial.println("Reconnected during delay.");
                }
            }

            // Close all servos after the delay
            closeAllServos();
            Serial.println("All servos closed.");
        }

        // Ensure MySQL connection is active before updating reminders
        if (!conn.connected()) {
            Serial.println("MySQL connection lost before updating reminders. Reconnecting...");
            if (!conn.connect(mysql_host, 3306, (char*)mysql_user, (char*)mysql_pass, (char*)mysql_db)) {
                Serial.println("Reconnection failed before updating reminders.");
                return;
            }
            Serial.println("Reconnected before updating reminders.");
        }

        MySQL_Cursor updateCursor(&conn);

        // Update all reminder statuses to 'closed'
        for (const String &r_id : reminderIDs) {
            String updateStatusQuery = "UPDATE reminders SET r_boxstatus = 'closed' WHERE r_id = '" + r_id + "'";
            if (!updateCursor.execute(updateStatusQuery.c_str())) {
                Serial.println("Failed to update reminder status to 'closed' for ID: " + r_id);
            } else {
                Serial.println("Reminder status updated to 'closed' for ID: " + r_id);
            }
        }

 

    

        // Fetch remaining "waiting" reminders
       if (!conn.connected()) {
    Serial.println("Not connected to MySQL. Reconnecting...");
    if (!conn.connect(mysql_host, 3306, (char*)mysql_user, (char*)mysql_pass, (char*)mysql_db)) {
        Serial.println("Reconnection failed.");
        return;
    }
    Serial.println("Reconnected to MySQL.");
}



 query = "SELECT r_time, r_medslot, r_id FROM reminders WHERE r_date = '" + latestDate + "' AND r_status = 'waiting' ORDER BY r_time ASC";

    // Create a cursor for the MySQL query result
    MySQL_Cursor remainingCursor(&conn);

    if (remainingCursor.execute(query.c_str())) {
        column_names *cols = remainingCursor.get_columns();
        row_values *row;

        while ((row = remainingCursor.get_next_row())) {
            String r_time = row->values[0];  // Reminder time (HH:MM:SS)
            int r_medslot = atoi(row->values[1]);  // Medication slot (1-based)
            int r_id = atoi(row->values[2]);  // Reminder ID

            struct tm timeinfo;
            if (getLocalTime(&timeinfo)) {
                char currentTimeStr[9];
                strftime(currentTimeStr, sizeof(currentTimeStr), "%H:%M:%S", &timeinfo);
                String currentTime = String(currentTimeStr);

                // Trim times
                r_time.trim();
                currentTime.trim();

                // Compare HH:MM only
                String r_timeHM = r_time.substring(0, 5);
                String currentTimeHM = currentTime.substring(0, 5);

                if (r_timeHM == currentTimeHM && !servoStates[r_medslot - 1]) {
                    if (std::find(processedReminders.begin(), processedReminders.end(), r_id) == processedReminders.end()) {
                        Serial.println("Opening servo for reminder ID: " + String(r_id));

                        // Open the servo for the corresponding medication slot
                        switch (r_medslot) {
                            case 1: openServo(servo_10, 0); break;
                            case 2: openServo(servo_9, 1); break;
                            case 3: openServo(servo_8, 2); break;
                            case 4: openServo(servo_7, 3); break;
                            case 5: openServoCustom(servo_6, 4); break;
                            case 6: openServoCustom(servo_5, 5); break;
                            case 7: openServoCustom(servo_4, 6); break;
                            default: 
                                Serial.println("Invalid medication slot: " + String(r_medslot));
                                continue;
                        }

                        // Mark servo as open
                        servoStates[r_medslot - 1] = true;

                        // Add reminder to processed list
                        processedReminders.push_back(r_id);

    
                    
    
                }
            }
        }
    }
}
    }
}

    






void openServo(Servo &servo, int servoIndex) {
    for (int pos = 180; pos >= 0; pos--) {
        servo.write(pos);
        delay(15);
    }
    servoStatus[servoIndex] = true;  // Mark this servo as open
}

// Function to open a custom servo and update the status
void openServoCustom(Servo &servo, int servoIndex) {
    for (int pos = 0; pos <= 180; pos++) {
        servo.write(pos);
        delay(15);
    }
    servoStatus[servoIndex] = true;  // Mark this servo as open
}

// Function to close a servo and update the status
void closeServo(Servo &servo, int servoIndex) {
    for (int pos = 0; pos <= 180; pos++) {
        servo.write(pos);
        delay(15);
    }
    servoStatus[servoIndex] = false;  // Mark this servo as closed
}

// Function to close a custom servo and update the status
void closeServoCustom(Servo &servo, int servoIndex) {
    for (int pos = 180; pos >= 0; pos--) {
        servo.write(pos);
        delay(15);
    }
    servoStatus[servoIndex] = false;  // Mark this servo as closed
}

// Function to close only the opened servos
void closeAllServos() {
    if (servoStatus[0]) { closeServo(servo_10, 0); }
    if (servoStatus[1]) { closeServo(servo_9, 1); }
    if (servoStatus[2]) { closeServo(servo_8, 2); }
    if (servoStatus[3]) { closeServo(servo_7, 3); }
    if (servoStatus[4]) { closeServoCustom(servo_6, 4); }
    if (servoStatus[5]) { closeServoCustom(servo_5, 5); }
    if (servoStatus[6]) { closeServoCustom(servo_4, 6); }
}


