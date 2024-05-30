#include <ESP8266WiFi.h>
#include <MySQL_Connection.h>
#include <MySQL_Cursor.h>
#include <Adafruit_Si7021.h>

float temperature = 13;
float humidity = 35;
float light = 20; // Replace with actual light sensor reading
float mappedPulse = 90; // Replace with actual pulse sensor reading
char CNP_pacient[] = "1234567890123";

// WiFi settings
const char* ssid = "Ta'veren";
const char* wifi_password = "Manetheren";
Adafruit_Si7021 sensor;

// MySQL settings
IPAddress server_addr(192, 168, 249, 84);  // IP of the MySQL server
char user[] = "root";              // MySQL user login username
char password_db[] = "password";   // MySQL user login password
char db_name[] = "db";             // MySQL database name

WiFiClient client;
MySQL_Connection conn((Client *)&client);

// Function prototypes
void deleteLastSensorData(const char* CNP_pacient);
void insertSensorData(const char* CNP_pacient, float pulse, float temp, float hum, float light);
bool insertAlerta_automata(const char* CNP_pacient, float temperature, float humidity, int mappedPulse);
bool reconnectToServer();
bool ensureConnected();

void setup() {
  Serial.begin(115200);
  delay(100);

  // Connect to WiFi
  WiFi.begin(ssid, wifi_password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  // Connect to MySQL server
  if (reconnectToServer()) {
    Serial.println("Connected to MySQL server");
  } else {
    Serial.println("Connection to MySQL server failed.");
  }
  if (!sensor.begin()) {
    Serial.println("Could not find Si7021 sensor!");
    while (1);
  }
}

void loop() {
  /*float temperature = sensor.readTemperature();
  float humidity = sensor.readHumidity();
  int light = analogRead(12);
  int mappedPulse = analogRead(A0);*/
  // Ensure the connection to MySQL server
  if (!ensureConnected()) {
    Serial.println("Reconnection to MySQL server failed.");
    delay(5000);
    return;
  }

  // Delete last sensor data and insert new data
  deleteLastSensorData(CNP_pacient);
  insertSensorData(CNP_pacient, mappedPulse, temperature, humidity, light);

  // Check alert configurations and generate alerts
  insertAlerta_automata(CNP_pacient, temperature, humidity, mappedPulse);
  delay(5000);
}

bool reconnectToServer() {
  for (int i = 0; i < 3; i++) {  // Try to reconnect up to 3 times
    if (conn.connect(server_addr, 3306, user, password_db, db_name)) {
      return true;
    }
    delay(1000);
  }
  return false;
}

bool ensureConnected() {
  if (!conn.connected()) {
    Serial.println("Reconnecting to MySQL server...");
    return reconnectToServer();
  }
  return true;
}

void deleteLastSensorData(const char* CNP_pacient) {
  if (!ensureConnected()) return;  // Ensure connection before operation

  char SELECT_SQL[128];
  snprintf(SELECT_SQL, sizeof(SELECT_SQL), "SELECT ID_senzor FROM Senzor_data WHERE CNP_pacient='%s' ORDER BY ID_senzor DESC LIMIT 1", CNP_pacient);

  MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);
  cur_mem->execute(SELECT_SQL);

  column_names *cols = cur_mem->get_columns();
  row_values *row = NULL;

  int last_id = -1;
  if ((row = cur_mem->get_next_row())) {
    last_id = atoi(row->values[0]);
  }
  delete cur_mem;

  if (last_id != -1) {
    char DELETE_SQL[128];
    snprintf(DELETE_SQL, sizeof(DELETE_SQL), "DELETE FROM Senzor_data WHERE ID_senzor=%d", last_id);

    MySQL_Cursor *curs_del = new MySQL_Cursor(&conn);
    curs_del->execute(DELETE_SQL);
    delete curs_del;
  }
}

void insertSensorData(const char* CNP_pacient, float pulse, float temp, float hum, float light) {
  if (!ensureConnected()) return;  // Ensure connection before operation

  int validitate_temp = (temp > 25 || temp < 16) ? 0 : 1;
  int validitate_puls = (pulse < 60 || pulse > 100) ? 0 : 1;
  int validitate_umiditate = (hum > 50 || hum < 30) ? 0 : 1;
  int validitate_lumina = 1;  // Adjust based on your criteria

  char INSERT_SQL[512];
  snprintf(INSERT_SQL, sizeof(INSERT_SQL), 
    "INSERT INTO Senzor_data (CNP_pacient, valoare_puls, validitate_puls, valoare_temp, validitate_temp, valoare_umiditate, validitate_umiditate, valoare_lumina, validitate_lumina) "
    "VALUES ('%s', %f, %d, %f, %d, %f, %d, %f, %d)",  
    CNP_pacient, pulse, validitate_puls, temp, validitate_temp, hum, validitate_umiditate, light, validitate_lumina);

  MySQL_Cursor *curs_mem = new MySQL_Cursor(&conn);
  curs_mem->execute(INSERT_SQL);
  delete curs_mem;
}

bool insertAlerta_automata(const char* CNP_pacient, float temperature, float humidity, int mappedPulse) {
  // Ensure connection to MySQL server
  if (!ensureConnected()) {
    Serial.println("Error: MySQL server not connected.");
    return false;
  }

  // Query to read configuration from Configurare_alerta table
  char SELECT_CONFIG[256];
  snprintf(SELECT_CONFIG, sizeof(SELECT_CONFIG),
    "SELECT tip_senzor, prag_min, prag_max FROM Configurare_Alerta");

  MySQL_Cursor config_cursor = MySQL_Cursor(&conn);
  if (!config_cursor.execute(SELECT_CONFIG)) {
    Serial.println("Error executing configuration query.");
    return false;
  }

  // Variables to store configuration values
  float temp_min = 0, temp_max = 0;
  float hum_min = 0, hum_max = 0;
  int pulse_min = 0, pulse_max = 0;

  // Read configuration values from the result set
  row_values* row;
  do {
    row = config_cursor.get_next_row();
    if (row != NULL) {
      char* tip_senzor = row->values[0];
      float prag_min = atof(row->values[1]);
      float prag_max = atof(row->values[2]);

      if (strcmp(tip_senzor, "Temperatura") == 0) {
        temp_min = prag_min;
        temp_max = prag_max;
      } else if (strcmp(tip_senzor, "Umiditate") == 0) {
        hum_min = prag_min;
        hum_max = prag_max;
      } else if (strcmp(tip_senzor, "Puls") == 0) {
        pulse_min = (int)prag_min;
        pulse_max = (int)prag_max;
      }
    }
  } while (row != NULL);

  // Define the SQL query for inserting data into Alerta_automata table
  char INSERT_ALERT[512];

  // Create a cursor to execute the query
  MySQL_Cursor alert_cursor = MySQL_Cursor(&conn);

  // Check temperature condition
  if (temperature < temp_min || temperature > temp_max) {
    snprintf(INSERT_ALERT, sizeof(INSERT_ALERT),
      "INSERT INTO Alerta_automata (CNP_pacient, tip_senzor, mesaj_automat) VALUES ('%s', 'Temperatura', 'Temperature out of range')",
      CNP_pacient);

    Serial.println("Executing temperature alert insertion query...");
    if (!alert_cursor.execute(INSERT_ALERT)) {
      Serial.println("Error inserting temperature alert into Alerta_automata table.");
      return false; // Return failure
    }
  }

  // Check humidity condition
  if (humidity < hum_min || humidity > hum_max) {
    snprintf(INSERT_ALERT, sizeof(INSERT_ALERT),
      "INSERT INTO Alerta_automata (CNP_pacient, tip_senzor, mesaj_automat) VALUES ('%s', 'Umiditate', 'Humidity out of range')",
      CNP_pacient);

    Serial.println("Executing humidity alert insertion query...");
    if (!alert_cursor.execute(INSERT_ALERT)) {
      Serial.println("Error inserting humidity alert into Alerta_automata table.");
      return false; // Return failure
    }
  }

  // Check pulse condition
  if (mappedPulse < pulse_min || mappedPulse > pulse_max) {
    snprintf(INSERT_ALERT, sizeof(INSERT_ALERT),
      "INSERT INTO Alerta_automata (CNP_pacient, tip_senzor, mesaj_automat) VALUES ('%s', 'Puls', 'Pulse out of range')",
      CNP_pacient);

    Serial.println("Executing pulse alert insertion query...");
    if (!alert_cursor.execute(INSERT_ALERT)) {
      Serial.println("Error inserting pulse alert into Alerta_automata table.");
      return false; // Return failure
    }
  }

  return true; // Return success
}







