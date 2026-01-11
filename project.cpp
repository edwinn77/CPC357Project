#include <WiFi.h>
#include <HTTPClient.h>

// --- WiFi Configuration ---
const char* ssid = "winnnn~weeee~"; // WiFi SSID
const char* password = "123"; // WiFi Password

// --- Cloud Configuration ---
const char* cloudEndpoint = "https://busdata-792085308632.us-central1.run.app"; // Cloud Function Endpoint

// --- Pin Definitions ---
const int irEntrance = 42;   // IR Sensor 1
const int irExit = 4;      // IR Sensor 2
const int btnStation = 39;  // Button 1: Switch Station
const int btnRoute = 38;    // Button 2: Switch Route

// --- Array Definitions ---
const char* routes[] = {"Route A", "Route B", "Route C"};
int routeIndex = 0;
const int totalRoutes = 3;

// 2D Array: Each row belongs to a specific route
const char* stations[3][5] = {
  {"USM Gate", "Subway", "Queensbay", "Sungai Nibong", ""},       // Route A (4 stops)
  {"Penang Hill", "Air Itam", "Kek Lok Si", "", ""},       // Route B (3 stops)
  {"Gurney Drive", "Komtar", "Jetty", "Batu Feringghi", ""} // Route C (4 stops)
};

// Track how many valid stops are in each route
int stationCounts[] = {4, 3, 4}; 
int stationIndex = 0;

// --- Logic Variables ---
int passengerCount = 0;
bool entranceActive = false;
bool exitActive = false;
unsigned long lastPrintTime = 0;
const unsigned long printInterval = 5000; 

void setup() {
  Serial.begin(9600);
  
  // WiFi Connection
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  
  // Initialize Pins
  pinMode(irEntrance, INPUT_PULLUP);
  pinMode(irExit, INPUT_PULLUP);
  pinMode(btnStation, INPUT_PULLUP);
  pinMode(btnRoute, INPUT_PULLUP);
  
  Serial.println("System Ready: Route A | USM Main Gate");
}

void loop() {
  int entranceVal = digitalRead(irEntrance);
  int exitVal = digitalRead(irExit);

  // --- Passenger Entrance ---
  if (entranceVal == LOW && !entranceActive) {
    passengerCount++;
    entranceActive = true; 
    handleEvent("Passenger Entered");
  } else if (entranceVal == HIGH) { entranceActive = false; }

  // --- Passenger Exit ---
  if (exitVal == LOW && !exitActive) {
    if (passengerCount > 0) passengerCount--;
    exitActive = true;
    handleEvent("Passenger Exited");
  } else if (exitVal == HIGH) { exitActive = false; }

  // --- Button 1: Switch Station (Dynamic per Route) ---
  if (digitalRead(btnStation) == LOW) {
    // Use stationCounts[routeIndex] to stay within the current route's list
    stationIndex = (stationIndex + 1) % stationCounts[routeIndex];
    
    Serial.print("Next Stop for ");
    Serial.print(routes[routeIndex]);
    Serial.print(": ");
    Serial.println(stations[routeIndex][stationIndex]);
    
    // handleEvent("Station Update");
    delay(400); 
  }

  // --- Button 2: Switch Route (Safety Check: Passenger Count must be 0) ---
  if (digitalRead(btnRoute) == LOW) {
    if (passengerCount == 0) {
      routeIndex = (routeIndex + 1) % totalRoutes;
      stationIndex = 0; // Always reset to the first stop of the new route
      
      Serial.print("ROUTE CHANGED: ");
      Serial.println(routes[routeIndex]);
      Serial.print("Starting at: ");
      Serial.println(stations[routeIndex][stationIndex]);
      
      // handleEvent("Route Change");
    } else {
      Serial.println("SAFETY ALERT: Empty bus before changing routes!");
    }
    delay(400); 
  }

  // Status Print
  if (millis() - lastPrintTime >= printInterval) {
    Serial.printf("Route: %s | Station: %s | Count: %d\n", routes[routeIndex], stations[routeIndex][stationIndex], passengerCount);
    lastPrintTime = millis();
  }
  delay(50);
}

void handleEvent(String eventName) {
  sendToCloud(passengerCount, eventName, routes[routeIndex], stations[routeIndex][stationIndex]);
}

void sendToCloud(int count, String event, String r, String s) {
  if(WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(cloudEndpoint);
    http.addHeader("Content-Type", "application/json");
    
    // JSON Payload including Route and Location
    String json = "{\"passenger_count\":" + String(count) + 
                  ", \"event\":\"" + event + 
                  "\", \"route\":\"" + r + 
                  "\", \"location\":\"" + s + "\"}";

    int httpResponseCode = http.POST(json);
    http.end();
  }
}