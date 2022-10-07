#include "arduino.h"
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h> //Biblioteca para as publicações via mqtt
#include "MQTT_Client.h"  //Arquivo com as funções de mqtt
#include <threewire.h>  
#include <RtcDS1302.h>
#include "MAX30105.h"
#include "heartRate.h"
//------------------------------------Definições de rede-----------------------------
#define WIFISSID "trator"                                    // Coloque seu SSID de WiFi aqui
#define PASSWORD "Pauloh01"                                  // Coloque seu password de WiFi aqui
#define TOKEN "BBFF-rX9QpMCCnyo6oNUW0xErfP0e7ixvjJ"          // Coloque seu TOKEN do Ubidots aqui
#define variable_label_spo2 "Saturação de Oxigenio"          // Label referente a variável de temperatura criada no ubidots
#define VARIABLE_LABEL_HEAT_RATE "Frequencia Cardiaca"       // Label referente a variável de umidade criada no ubidots
#define DEVICE_ID "BBFF-fc2c3213408ddcd65448b2fc1996251f155" // ID do dispositivo (Device id, também chamado de client name)
#define SERVER "things.ubidots.com"                          // Servidor do Ubidots (broker)
#define PORT 1883                                            // Porta padrão

// Tópico aonde serão feitos os publish, "esp32-dht" é o DEVICE_LABEL
#define TOPIC "/v1.6/devices/CPAP_RESPIRE"
int CLK = 5, RST = 12, DAT = 14;

WiFiClient ubidots;           // Objeto WiFiClient usado para a conexão wifi
PubSubClient client(ubidots); // Objeto PubSubClient usado para publish–subscribe
WiFiServer sv(80);
MAX30105 particleSensor;
ThreeWire myWire(DAT,CLK,RST); // IO, SCLK, CE
RtcDS1302<ThreeWire> Rtc(myWire);
QueueHandle_t fila;

struct
{
  double bpm;
  double espo2;
} typedef mqtt_dados_t;

#define TIMETOBOOT 3000 // wait for this time(msec) to output SpO2
#define SCALE 93.0      // 88.0      //adjust to display heart beat and SpO2 in the same scale
#define SAMPLING 5      // if you want to see heart beat more precisely , set SAMPLING to 1
#define FINGER_ON 30000 // if red signal is lower than this , it indicates your finger is not on the sensor
#define MINIMUM_SPO2 10.0

const byte RATE_SIZE = 4; // Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE];    // Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; // Time at which the last beat occurred
float beatsPerMinute;
double beatAvg;
/* ------------------------------- RTC 1302 -------------------------------------------
CLK/SCLK --> D5 - IO27
DAT/IO   --> D4 - IO12
RST/CE   --> D2 - IO14
VCC      --> 3,3v 
GND      --> GND*/

//------------------------Funções--------------------------------------------------
void reconnect();
bool mqttInit();
bool sendValues(double bpm, double espo2);
void mqtt_task(void *pvt);
void printDateTime(const RtcDateTime& dt);

//---------------------------------SETUP------------------------------------------------
void setup()
{
  Serial.begin(9600);
  //--------------------------------- RTC 1302 ------------------------------------------------------------
    Serial.print(" compiled: ");
    Serial.println(__DATE__);
    Serial.println(__TIME__);
    Rtc.Begin();
    RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
    printDateTime(compiled);
    Serial.println();
    if (!Rtc.IsDateTimeValid()) 
    {
        Serial.println("RTC perdeu a confiança no DateTime!");
        Rtc.SetDateTime(compiled);
    }

    if (Rtc.GetIsWriteProtected())
    {
        Serial.println("RTC foi protegido contra gravação, permitindo a gravação agora");
        Rtc.SetIsWriteProtected(false);
    }

    if (!Rtc.GetIsRunning())
    {
        Serial.println("O RTC não estava funcionando ativamente, começando agora");
        Rtc.SetIsRunning(true);
    }

    RtcDateTime now = Rtc.GetDateTime();
    if (now < compiled) 
    {
        Serial.println("O RTC é mais antigo que o tempo de compilação! (Atualizando DateTime)");
        Rtc.SetDateTime(compiled);
    }
    else if (now > compiled) 
    {
        Serial.println("RTC is newer than compile time. (this is expected)");
    }
    else if (now == compiled) 
    {
        Serial.println("RTC is the same as compile time! (not expected but all is fine)");
    }
    
  //-------------------------------------------------------------------------------------------------------
  Serial.println("Initializing...");

  //---------------------------------------- Configurando AP------------------------------------------------

  //--------------------------------------------------------------------------------------------------------

  fila = xQueueCreate(10, sizeof(mqtt_dados_t));

  // Initialize sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST))
  {
    Serial.println("MAX30102 was not found. Please check wiring/power. ");
    while (1)
      ;
  }
  // Serial.println("Place your index finger on the sensor with steady pressure.");

  byte ledBrightness = 0x0A; // Options: 0=Off to 255=50mA
  byte sampleAverage = 8;    // Options: 1, 2, 4, 8, 16, 32
  byte ledMode = 2;          // Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
  int sampleRate = 400;      // Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 69;       // Options: 69, 118, 215, 411
  int adcRange = 2048;       // Options: 2048, 4096, 8192, 16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); // Configure sensor with these settings

  xTaskCreatePinnedToCore(mqtt_task, "MQTT_TASK", 2048, NULL, 1, NULL, 1);
}
//------------------------------------------------------------------------------------
int tempo_envio = 0;
int y = 30;
uint32_t ir, red;
float red_forGraph = 0.0;
float ir_forGraph = 0.0;
double avered = 0;
double aveir = 0;
double sumirrms = 0;
double sumredrms = 0;
int i = 0;
int Num = 100; // calculate SpO2 by this sampling interval
double fred, fir;
double SpO2 = 0;
double ESpO2 = 95.0; // initial value of estimated SpO2
double FSpO2 = 0.7;  // filter factor for estimated SpO2
double frate = 0.95; // low pass filter for IR/red LED value to eliminate AC component

void loop()
{
  RtcDateTime now = Rtc.GetDateTime();
  printDateTime(now);
  Serial.println();
  long irValue = particleSensor.getIR();

  // Checa batimento
  if (checkForBeat(irValue) == true)
  {

    long delta = millis() - lastBeat;
    lastBeat = millis();
    beatsPerMinute = 60 / (delta / 1000.0);

    if (beatsPerMinute < 255 && beatsPerMinute > 20)
    {
      rates[rateSpot++] = (byte)beatsPerMinute; // Store this reading in the array
      rateSpot %= RATE_SIZE;                    // Wrap variable

      // Take average of readings
      beatAvg = 0;
      for (byte x = 0; x < RATE_SIZE; x++)
        beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }
  }

  if (irValue < 50000)

    Serial.println("*** Sem dedo *** ");

  while (particleSensor.available())
  {                                   // do we have new data
    red = particleSensor.getFIFOIR(); // why getFOFOIR output Red data by MAX30102 on MH-ET LIVE breakout board
    ir = particleSensor.getFIFORed(); // why getFIFORed output IR data by MAX30102 on MH-ET LIVE breakout board
    i++;
    fred = (double)red;
    fir = (double)ir;
    avered = avered * frate + (double)red * (1.0 - frate); // average red level by low pass filter
    aveir = aveir * frate + (double)ir * (1.0 - frate);    // average IR level by low pass filter
    sumredrms += (fred - avered) * (fred - avered);        // square sum of alternate component of red level
    sumirrms += (fir - aveir) * (fir - aveir);             // square sum of alternate component of IR level

    if ((i % Num) == 0)
    {
      double R = (sqrt(sumredrms) / avered) / (sqrt(sumirrms) / aveir);
      SpO2 = -23.3 * (R - 0.4) + 100;
      ESpO2 = FSpO2 * ESpO2 + (1.0 - FSpO2) * SpO2; // passa baixa
      sumredrms = 0.0;
      sumirrms = 0.0;
      i = 0;
      break;
    }
    particleSensor.nextSample(); // We're finished with this sample so move to next sample
  
  
  }
  
  mqtt_dados_t dados;
  dados.bpm = beatAvg;
  dados.espo2 = SpO2;
  tempo_envio += 50;
  if (tempo_envio == 5000)
  {
    tempo_envio = 0;
    xQueueSend(fila, &dados, portMAX_DELAY);
  }

  delay(50);
}
bool mqttInit()
{
  // Inicia WiFi com o SSID e a senha
  WiFi.begin(WIFISSID, PASSWORD);

  // Loop até que o WiFi esteja conectado
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    // Serial.println("Establishing connection to WiFi..");
  }

   // Seta servidor com o broker e a porta
  client.setServer(SERVER, PORT);

  // Conecta no ubidots com o Device id e o token, o password é informado como vazio
  while (!client.connect(DEVICE_ID, TOKEN, ""))
  {
    // Serial.println("MQTT - Connect error");
    return false;
  }

  // Serial.println("MQTT - Connect ok");
  return true;
}
#define countof(a) (sizeof(a) / sizeof(a[0]))
void printDateTime(const RtcDateTime& dt)
{
    char datestring[20];

    snprintf_P(datestring, 
            countof(datestring),
            PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
            dt.Month(),
            dt.Day(),
            dt.Year(),
            dt.Hour(),
            dt.Minute(),
            dt.Second() );
    Serial.print(datestring);
}
void reconnect()
{

  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi.begin(WIFISSID, PASSWORD);
    Serial.println("Conectado");
  }
  while (!client.connected())
  {
    client.setServer(SERVER, PORT);
    client.connect(DEVICE_ID, TOKEN, "");
    Serial.println("******* Conectando *******");
    delay(2000);
  }
}

bool sendValues(double bpm, double espo2)
{
  char json[250];

  // Atribui para a cadeia de caracteres "json" os valores referentes a temperatura e os envia para a variável do ubidots correspondente
  sprintf(json, "{\"%s\":{\"value\":%02.02f}}", variable_label_spo2, espo2);
  Serial.printf("%s\n", json);
  if (!client.publish(TOPIC, json))
    return false;

  // Atribui para a cadeia de caracteres "json" os valores referentes a umidade e os envia para a variável do ubidots correspondente
  sprintf(json, "{\"%s\":{\"value\":%02.02f}}", VARIABLE_LABEL_HEAT_RATE, bpm);
  Serial.printf("%s\n", json);
  if (!client.publish(TOPIC, json))
    return false;

  // Se tudo der certo retorna true
  return true;
  // Serial.println("* PUBLICADO ** ");
}

void mqtt_task(void *pvt)

{

  mqtt_dados_t dados;

  while (1)
  {
    reconnect();
    while (xQueueReceive(fila, &dados, 100 * portTICK_PERIOD_MS))
    {
      sendValues(dados.bpm, dados.espo2);
    }
  }
}

void geraAP()
{
  WiFiClient client = sv.available(); // Cria o objeto cliente
  if (client)
  {                   // Se este objeto estiver disponível
    String line = ""; // Variável para armazenar os dados recebidos
    while (client.connected())
    { // Enquanto estiver conectado
      if (client.available())
      {                         // Se estiver disponível
        char c = client.read(); // Lê os caracteres recebidos
        if (c == '\n')
        { // Se houver uma quebra de linha
          if (line.length() == 0)
          {                                    // Se a nova linha tiver 0 de tamanho
            client.println("HTTP/1.1 200 OK"); // Envio padrão de início de comunicação
            client.println("Content-type:text/html");
            client.println();
            client.print("<hr />");
            client.print("<h1 style=text-align:center><strong>RESPIRE</strong></h1>");
            client.print("<hr />");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<h1 style=text-align:center><span style=font-size:28px><span style=color:#0000FF><strong>Satura&ccedil;&atilde;o de Oxig&ecirc;nio</strong></span></span></h1>");
            client.print("<p style=text-align:center><strong>X</strong></p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<h1 style=text-align:center><span style=font-size:28px><span style=color:#FF0000><strong>Batimentos Card&iacute;acos</strong></span></span></h1>");
            client.print("<p style=text-align:center><strong>Y</strong></p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<hr />");
            client.println();
            break;
          }
          else
          {
            line = "";
          }
        }
        else if (c != '\r')
        {
          line += c; // Adiciona o caractere recebido à linha de leitura
        }
        if (line.endsWith("GET /ligar"))
        { // Se a linha terminar com "/ligar", liga o led
          digitalWrite(23, HIGH);
        }
        if (line.endsWith("GET /desligar"))
        { // Se a linha terminar com "/desligar", desliga o led
          digitalWrite(23, LOW);
        }
      }
    }
    client.stop(); // Para o cliente
  }
}

