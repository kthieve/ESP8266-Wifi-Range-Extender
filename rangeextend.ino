
/**
 * ESP8266 WiFi Range Extender - Memory Optimized Version
 * 
 * Essential Features Only:
 * - Connects to existing WiFi router and creates access point
 * - Uses NAPT for internet sharing between networks
 * - Simple web interface for monitoring
 * - Basic LED status indication
 * - 24-hour auto reboot for stability
 * - Optimized for NodeMCU's limited RAM
 * 
 * CONFIGURATION REQUIRED - UPDATE THESE VALUES:
 */

#if LWIP_FEATURES && !LWIP_IPV6

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <lwip/napt.h>

// ==========================================
// CONFIGURATION - UPDATE THESE VALUES!
// ==========================================

const char* MAIN_SSID = "";        // Your main router's WiFi name
const char* MAIN_PASS = "";    // Your main router's WiFi password
const char* AP_SSID = "WiFi_Extender";             // Name for your extended network
const char* AP_PASS = "123456789";               // Password for your extended network

// Network Configuration
const IPAddress AP_IP(192, 168, 101, 1);
const IPAddress AP_GATEWAY(192, 168, 101, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);
const IPAddress DNS_PRIMARY(192, 168, 100, 2);     // Your router IP

// Timing (in milliseconds)
#define RECONNECT_INTERVAL 15000   // Faster reconnection
#define STATUS_INTERVAL 30000      // More frequent status checks
#define REBOOT_INTERVAL (12UL * 60UL * 60UL * 1000UL)  // 12 hours (more frequent)
#define INTERNET_CHECK_INTERVAL 60000  // 1 minute
#define CONNECTION_WATCHDOG 10000   // 10 seconds watchdog

// ==========================================
// Global Variables (Minimized)
// ==========================================

ESP8266WebServer server(80);
bool stationOK = false;
bool naptOK = false;
bool internetOK = false;
unsigned long lastCheck = 0;
unsigned long lastWatchdog = 0;
unsigned long lastMemoryCheck = 0;
unsigned long bootTime = 0;
int reconnectCount = 0;

// ==========================================
// Core Functions
// ==========================================

bool connectToRouter() {
  // Don't change mode if AP is already running
  if (WiFi.getMode() != WIFI_AP_STA) {
    WiFi.mode(WIFI_AP_STA);
    delay(100);
  }
  
  WiFi.begin(MAIN_SSID, MAIN_PASS);
  
  Serial.print("Connecting to router");
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    stationOK = true;
    Serial.println(" OK!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    stationOK = false;
    Serial.println(" FAILED!");
    return false;
  }
}

bool startAccessPoint() {
  WiFi.mode(WIFI_AP_STA);
  
  if (!WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET)) {
    Serial.println("AP config failed");
    return false;
  }
  
  // Start AP with memory-optimized parameters: limit clients to reduce memory usage
  if (!WiFi.softAP(AP_SSID, AP_PASS, 6, false, 4)) {  // Reduced max clients to 4
    Serial.println("AP start failed");
    return false;
  }
  
  // Wait for AP to be ready
  delay(1000);  // Increased delay
  
  // Configure DNS for AP clients (Android compatibility)
  WiFi.softAPDhcpServer().setDns(IPAddress(8, 8, 8, 8));  // Google DNS
  
  Serial.print("AP started: ");
  Serial.println(AP_SSID);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("AP is visible and ready for connections");
  Serial.println("DNS configured for Android compatibility");
  return true;
}

bool enableNAPT() {
  if (!stationOK) return false;
  
  delay(500);  // Reduced delay
  
  // Use minimal NAPT configuration for memory efficiency
  if (ip_napt_init(300, 5) == ERR_OK && ip_napt_enable_no(SOFTAP_IF, 1) == ERR_OK) {
    naptOK = true;
    Serial.println("Internet sharing enabled (memory optimized)");
    return true;
  } else {
    naptOK = false;
    Serial.println("NAPT failed - trying minimal config...");
    delay(1000);
    // Retry with even smaller table
    if (ip_napt_init(200, 3) == ERR_OK && ip_napt_enable_no(SOFTAP_IF, 1) == ERR_OK) {
      naptOK = true;
      Serial.println("Internet sharing enabled (minimal config)");
      return true;
    }
    Serial.println("NAPT initialization failed completely");
    return false;
  }
}

bool testInternet() {
  if (!stationOK || !naptOK) {
    internetOK = false;
    return false;
  }
  
  WiFiClient client;
  client.setTimeout(5000);  // 5 second timeout
  
  // Try multiple servers for better reliability
  const char* testServers[] = {"8.8.8.8", "1.1.1.1", "google.com"};
  const int testPorts[] = {53, 53, 80};
  
  for (int i = 0; i < 3; i++) {
    if (client.connect(testServers[i], testPorts[i])) {
      client.stop();
      internetOK = true;
      Serial.printf("Internet test OK (via %s)\n", testServers[i]);
      return true;
    }
    delay(1000);
  }
  
  internetOK = false;
  Serial.println("Internet test failed on all servers");
  return false;
}

void connectionWatchdog() {
  // Aggressive connection monitoring
  static int failCount = 0;
  
  // Check WiFi status
  if (WiFi.status() != WL_CONNECTED) {
    failCount++;
    stationOK = false;
    naptOK = false;
    internetOK = false;
    
    if (failCount >= 3) {  // 3 consecutive failures
      Serial.printf("Connection watchdog: %d failures, forcing reconnect\n", failCount);
      WiFi.disconnect();
      delay(1000);
      connectToRouter();
      if (stationOK) enableNAPT();
      failCount = 0;
      reconnectCount++;
    }
  } else {
    failCount = 0;  // Reset on successful connection
    if (!stationOK) {
      stationOK = true;
      if (!naptOK) enableNAPT();
    }
  }
}

void checkMemory() {
  // Aggressive memory management
  int freeHeap = ESP.getFreeHeap();
  
  if (freeHeap < 8000) {  // Trigger at 8KB instead of 10KB
    Serial.printf("Low memory: %d bytes - aggressive cleanup\n", freeHeap);
    
    // Disable NAPT temporarily to free memory
    if (naptOK) {
      Serial.println("Disabling NAPT to free memory");
      naptOK = false;
      delay(1000);  // Let memory settle
    }
    
    // Force cleanup
    yield();
    delay(100);
    
    // Re-enable NAPT with minimal config
    if (stationOK && !naptOK && ESP.getFreeHeap() > 6000) {
      Serial.println("Re-enabling NAPT with minimal memory");
      enableNAPT();
    }
    
    // Emergency restart if still critical
    if (ESP.getFreeHeap() < 4000) {
      Serial.println("Critical memory - emergency restart");
      ESP.restart();
    }
  }
}

void checkAPStatus() {
  // Ensure AP stays active and STA connection is maintained
  if (WiFi.getMode() != WIFI_AP_STA) {
    Serial.println("AP mode lost - restarting AP");
    WiFi.mode(WIFI_AP_STA);
    delay(100);
    startAccessPoint();
    
    // Reconnect to router if needed
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Router connection lost - reconnecting");
      connectToRouter();
      if (stationOK) {
        enableNAPT();
      }
    }
  }
}

void updateLED() {
  static unsigned long lastBlink = 0;
  static bool ledState = false;
  unsigned long now = millis();
  
  if (stationOK && naptOK && internetOK) {
    // Solid on - working
    digitalWrite(LED_BUILTIN, LOW);
  } else if (stationOK && naptOK) {
    // Slow blink - no internet
    if (now - lastBlink > 1000) {
      ledState = !ledState;
      digitalWrite(LED_BUILTIN, ledState ? LOW : HIGH);
      lastBlink = now;
    }
  } else if (stationOK) {
    // Fast blink - connected but no sharing
    if (now - lastBlink > 300) {
      ledState = !ledState;
      digitalWrite(LED_BUILTIN, ledState ? LOW : HIGH);
      lastBlink = now;
    }
  } else {
    // Very fast blink - not connected
    if (now - lastBlink > 100) {
      ledState = !ledState;
      digitalWrite(LED_BUILTIN, ledState ? LOW : HIGH);
      lastBlink = now;
    }
  }
}

// ==========================================
// Web Interface (Simplified)
// ==========================================

void handleRoot() {
  // Build minimal HTML directly - no large template in memory
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><meta http-equiv='refresh' content='30'><title>WiFi Extender</title>";
  html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0}.info{background:white;padding:15px;margin:10px 0;border-radius:5px}";
  html += ".ok{background:#d4f4dd;color:#0f5132}.error{background:#f8d7da;color:#721c24}.warning{background:#fff3cd;color:#856404}</style></head><body>";
  html += "<h1>WiFi Extender</h1>";
  
  // Status
  if (stationOK && naptOK && internetOK) {
    html += "<div class='info ok'>✅ Internet Working</div>";
  } else if (stationOK && naptOK) {
    html += "<div class='info warning'>⚡ Testing Internet</div>";
  } else {
    html += "<div class='info error'>❌ No Connection</div>";
  }
  
  // Essential info only
  html += "<div class='info'><strong>Network:</strong> ";
  html += AP_SSID;
  html += "<br><strong>Password:</strong> ";
  html += AP_PASS;
  html += "<br><strong>Clients:</strong> ";
  html += String(WiFi.softAPgetStationNum());
  html += "<br><strong>Memory:</strong> ";
  html += String(ESP.getFreeHeap());
  html += " bytes<br><strong>Router:</strong> ";
  html += stationOK ? "Connected" : "Disconnected";
  html += "</div>";
  
  html += "<div class='info'><strong>LED:</strong><br>Solid=Working, Slow blink=No internet, Fast=Connecting</div>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ==========================================
// Main Functions
// ==========================================

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\nWiFi Range Extender - Memory Optimized");
  Serial.println("======================================");
  
  bootTime = millis();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  
  // Check configuration
  if (String(MAIN_SSID) == "YOUR_ROUTER_SSID") {
    Serial.println("⚠️ Update WiFi credentials in code!");
  }
  
  // Configure WiFi for stability
  WiFi.persistent(false);  // Don't save WiFi config to flash
  WiFi.setAutoReconnect(true);  // Enable auto-reconnect
  WiFi.setSleepMode(WIFI_NONE_SLEEP);  // Disable WiFi sleep for stability
  WiFi.setPhyMode(WIFI_PHY_MODE_11G);  // Use 802.11g for better compatibility
  
  // Start AP first
  Serial.println("Starting Access Point...");
  if (!startAccessPoint()) {
    Serial.println("CRITICAL: AP failed!");
    return;
  }
  
  // Verify AP is running
  Serial.printf("AP Status: %s\n", WiFi.softAPgetStationNum() >= 0 ? "RUNNING" : "FAILED");
  Serial.printf("AP MAC: %s\n", WiFi.softAPmacAddress().c_str());
  
  // Connect to router (this will set mode to AP_STA)
  Serial.println("Connecting to main router...");
  connectToRouter();
  
  // Enable internet sharing
  if (stationOK) {
    enableNAPT();
  } else {
    Serial.println("Running in AP-only mode (no internet sharing)");
  }
  
  // Start web server
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();
  
  Serial.println("Setup complete!");
  Serial.printf("Connect to: %s (password: %s)\n", AP_SSID, AP_PASS);
  Serial.printf("Web interface: http://%s/\n", AP_IP.toString().c_str());
  Serial.println("======================================");
}

void loop() {
  server.handleClient();
  updateLED();
  
  unsigned long now = millis();
  
  // Connection watchdog (every 10 seconds)
  if (now - lastWatchdog > CONNECTION_WATCHDOG) {
    connectionWatchdog();
    lastWatchdog = now;
  }
  
  // Memory check (every 10 seconds - more frequent)
  if (now - lastMemoryCheck > 10000) {
    checkMemory();
    lastMemoryCheck = now;
  }
  
  // Periodic tasks (every 30 seconds)
  if (now - lastCheck > STATUS_INTERVAL) {
    lastCheck = now;
    
    // Check AP status
    checkAPStatus();
    
    // Test internet connectivity
    if (stationOK && naptOK) {
      testInternet();
    }
    
    // Status update with reconnect count
    Serial.printf("Status: Router=%s NAPT=%s Internet=%s Clients=%d Memory=%d Reconnects=%d\n",
                  stationOK ? "OK" : "FAIL",
                  naptOK ? "OK" : "FAIL", 
                  internetOK ? "OK" : "FAIL",
                  WiFi.softAPgetStationNum(),
                  ESP.getFreeHeap(),
                  reconnectCount);
    
    // Emergency restart if too many reconnects
    if (reconnectCount > 10) {
      Serial.println("Too many reconnects - emergency restart");
      delay(1000);
      ESP.restart();
    }
  }
  
  // 12-hour reboot for stability
  if (now - bootTime > REBOOT_INTERVAL) {
    Serial.println("12-hour maintenance reboot...");
    delay(1000);
    ESP.restart();
  }
  
  yield();
}

#else
void setup() {
  Serial.begin(115200);
  Serial.println("ERROR: NAPT not supported");
  Serial.println("Update ESP8266 Arduino Core");
  Serial.println("Set LWIP Variant to v2 Higher Bandwidth");
}
void loop() { delay(1000); }
#endif
