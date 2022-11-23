#include "arduino.h"
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h> //Biblioteca para as publicações via mqtt
#include "MQTT_Client.h"  //Arquivo com as funções de mqtt
#include <threewire.h>  
#include <RtcDS1302.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "SPIFFS.h"
#include "FS.h"
//------------------------------------Definições de rede-----------------------------
#define WIFISSID                 "trator"                                    // Coloque seu SSID de WiFi aqui
#define PASSWORD                 "Pauloh01"                                  // Coloque seu password de WiFi aqui
#define TOKEN                    "BBFF-rX9QpMCCnyo6oNUW0xErfP0e7ixvjJ"       // Coloque seu TOKEN do Ubidots aqui
#define VARIABLE_LABEL_SPO2      "Saturação de Oxigenio"                     // Label referente a variável de temperatura criada no ubidots
#define VARIABLE_LABEL_HEAT_RATE "Frequencia Cardiaca"                       // Label referente a variável de umidade criada no ubidots
#define VARIABLE_TEXT            "TITULO A SER DEFINIDO"
#define variable_dia             "Dia"
#define variable_mes             "Mês"
#define variable_ano             "Ano"
#define variable_hora            "Hora"
#define variable_minuto          "Minuto"
#define variable_segundos        "Segundos"
#define DEVICE_ID                "BBFF-fc2c3213408ddcd65448b2fc1996251f155"    // ID do dispositivo (Device id, também chamado de client name)
#define SERVER                   "things.ubidots.com"                          // Servidor do Ubidots (broker)
#define PORT 1883                                            // Porta padrão
#define ARQUIVO                  "/Batimentos.csv"

// Tópico aonde serão feitos os publish, "esp32-dht" é o DEVICE_LABEL
#define TOPIC "/v1.6/devices/CPAP_RESPIRE"
int CLK = 5, RST = 12, DAT = 14;

WiFiClient           ubidots;           // Objeto WiFiClient usado para a conexão wifi
PubSubClient         client(ubidots); // Objeto PubSubClient usado para publish–subscribe
WiFiServer           sv(80);
MAX30105             particleSensor;
ThreeWire            myWire(DAT,CLK,RST); // IO, SCLK, CE
RtcDS1302<ThreeWire> Rtc(myWire);
QueueHandle_t        fila;

struct
{
  float bpm;
  float espo2;
  int   hora;
  int   minuto;
  int   segundos;
  int   dia;
  int   mes;
  int   ano;
  } typedef mqtt_dados_t;

#define TIMETOBOOT   3000       // wait for this time(msec) to output SpO2
#define SCALE        93.0            // 88.0      //adjust to display heart beat and SpO2 in the same scale
#define SAMPLING     5            // if you want to see heart beat more precisely , set SAMPLING to 1
#define FINGER_ON    30000       // if red signal is lower than this , it indicates your finger is not on the sensor
#define MINIMUM_SPO2 10.0

const char* ssid = "ESP32-AP"; //Define o nome do ponto de acesso Access Point
const char* pass = "12345678"; //Define a senha

const byte RATE_SIZE  = 4;     // Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE];        // Array of heart rates
byte rateSpot         = 0;
long lastBeat         = 0;            // Time at which the last beat occurred
float beatsPerMinute;
float beatAvg;
//String batimentos.txt;
/* ------------------------------- RTC 1302 -------------------------------------------
CLK/SCLK --> D5 - IO05
DAT/IO   --> D4 - IO12
RST/CE   --> D2 - IO14
VCC      --> 3,3v 
GND      --> GND*/

//------------------------Funções--------------------------------------------------
void reconnect();
bool mqttInit();
bool sendValues(float bpm, float espo2);
void mqtt_task(void *pvt);
void printDateTime(const RtcDateTime& dt);
void geraAP(int bmp,float spo2);
bool listDir() {
  File root = SPIFFS.open("/"); // Abre o "diretório" onde estão os arquivos na SPIFFS
  //                                e passa o retorno para
  //                                uma variável do tipo File.
  if (!root) // Se houver falha ao abrir o "diretório", ...
  {
    // informa ao usuário que houve falhas e sai da função retornando false.
    Serial.println(" - falha ao abrir o diretório");
    return false;
  }
  File file = root.openNextFile(); // Relata o próximo arquivo do "diretório" e
  //                                    passa o retorno para a variável
  //                                    do tipo File.
  int qtdFiles = 0; // variável que armazena a quantidade de arquivos que
  //                    há no diretório informado.
  while (file) { // Enquanto houver arquivos no "diretório" que não foram vistos,
    //                executa o laço de repetição.
    Serial.print("  FILE : ");
    Serial.print(file.name()); // Imprime o nome do arquivo
    Serial.print("\tSIZE : ");
    Serial.println(file.size()); // Imprime o tamanho do arquivo
    qtdFiles++; // Incrementa a variável de quantidade de arquivos
    file = root.openNextFile(); // Relata o próximo arquivo do diretório e
    //                              passa o retorno para a variável
    //                              do tipo File.
  }
  if (qtdFiles == 0)  // Se após a visualização de todos os arquivos do diretório
    //                      não houver algum arquivo, ...
  {
    // Avisa o usuário que não houve nenhum arquivo para ler e retorna false.
    Serial.print(" - Sem arquivos para ler. Crie novos arquivos pelo menu ");
    Serial.println("principal, opção 2.");
    return false;
  }
  return true; // retorna true se não houver nenhum erro
}
void readFile(void);
//---------------------------------SETUP------------------------------------------------
void setup()
{
  Serial.begin(9600);

   //---------------------------------------- Configurando AP------------------------------------------------
  Serial.println("\n"); //Pula uma linha                                                                  |
  WiFi.softAP(ssid, pass); //Inicia o ponto de acesso                                                     |
  Serial.print("Se conectando a: "); //Imprime mensagem sobre o nome do ponto de acesso                   |
  Serial.println(ssid); //                                                                                |
  IPAddress ip = WiFi.softAPIP(); //Endereço de IP                                                        |
  Serial.print("Endereço de IP: "); //Imprime o endereço de IP                                            |
  Serial.println(ip);               //                                                                    |
  sv.begin(); //Inicia o servidor                                                                         |
  Serial.println("Servidor online"); //Imprime a mensagem de início                                       |
  //-------------------------------------------------------------------------------------------------------
  //--------------------------------- LittleFS ------------------------------------------------------------
  SPIFFS.begin();
  Serial.println();
    
  //--------------------------------- RTC 1302 ------------------------------------------------------------
    Serial.print(" compiled: ");
    Serial.println(__DATE__);
    Serial.println(__TIME__);
    Rtc.Begin();
    RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
    printDateTime(compiled); 
    //compiled.Hour();
    
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
  byte ledMode       = 2;    // Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
  int sampleRate     = 400;  // Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth     = 69;   // Options: 69, 118, 215, 411
  int adcRange       = 2048; // Options: 2048, 4096, 8192, 16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); // Configure sensor with these settings

  xTaskCreatePinnedToCore(mqtt_task, "MQTT_TASK", 4096, NULL, 1, NULL, 1);
}
//------------------------------------------------------------------------------------
int tempo_envio    = 0;
//char y   
uint32_t ir, red;
float red_forGraph = 0.0;
float ir_forGraph  = 0.0;
float avered       = 0.0;
float aveir        = 0.0;
float sumirrms     = 0.0;
float sumredrms    = 0.0;
int i              = 0;
int Num            = 100; // calculate SpO2 by this sampling interval
float fred, fir;
float SpO2         = 0.0;
float ESpO2        = 95.0; // initial value of estimated SpO2
float FSpO2        = 0.7;  // filter factor for estimated SpO2
float frate        = 0.95; // low pass filter for IR/red LED value to eliminate AC component

void loop()
{
 
  
  RtcDateTime now = Rtc.GetDateTime();
    
  long irValue = particleSensor.getIR();

  // Checa batimento
  if (checkForBeat(irValue) == true)
  {

    long delta  = millis() - lastBeat;
    lastBeat    = millis();
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
  {
    SpO2 = 0;
    beatAvg = 0;
    Serial.println("*** Sem dedo *** ");
  }
  while (particleSensor.available())
  {                                   // do we have new data
    red = particleSensor.getFIFOIR(); // why getFOFOIR output Red data by MAX30102 on MH-ET LIVE breakout board
    ir = particleSensor.getFIFORed(); // why getFIFORed output IR data by MAX30102 on MH-ET LIVE breakout board
    i++;
    fred = (float)red;
    fir = (float)ir;
    avered = avered * frate + (float)red * (1.0 - frate); // average red level by low pass filter
    aveir = aveir * frate + (float)ir * (1.0 - frate);    // average IR level by low pass filter
    sumredrms += (fred - avered) * (fred - avered);        // square sum of alternate component of red level
    sumirrms += (fir - aveir) * (fir - aveir);             // square sum of alternate component of IR level

    if ((i % Num) == 0)
    {
      float R = (sqrt(sumredrms) / avered) / (sqrt(sumirrms) / aveir);
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
  dados.dia      = now.Day();
  dados.mes      = now.Month();
  dados.ano      = now.Year();
  dados.hora     = now.Hour();
  dados.minuto   = now.Minute();
  dados.segundos = now.Second(); 
  dados.bpm      = beatAvg;
  dados.espo2    = SpO2;
  tempo_envio    += 50;
  
 
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

    snprintf_P(datestring,countof(datestring),PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
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

bool sendValues(float bpm, float espo2, int dia,int mes,int ano,int hora, int minuto,int segundo){ //, float mes
  char json[250];
 // Atribui para a cadeia de caracteres "json" os valores referentes a temperatura e os envia para a variável do ubidots correspondente
  sprintf(json, "%02.02f; %02.02f ; %02d/%02d/%02d ; %02d:%02d:%02d\n", bpm, espo2,dia,mes,ano,hora,minuto,segundo);
  
    
      if(SPIFFS.exists(ARQUIVO)){
        Serial.println("Arquivo ja existe!");
        } else { 
          Serial.print("Gravando o arquivo ");
          Serial.println(" : ");
          File file = SPIFFS.open(ARQUIVO, "w"); // Abre o arquivo, no modo escrita,

          if (!file) // Se houver falha ao abrir o caminho, ...
          {
          // informa ao usuário que houve falhas e sai da função retornando false.
            Serial.println(" falha ao abrir arquivo para gravação");
          }
          if (file.print(json)) // Se a escrita do arquivo com seu conteúdo der certo, ...
          {
            // informa ao usuário que deu certo
            Serial.println(" <<< arquivo escrito >>>");
          } else {
            // informa ao usuário que deu erros e sai da função retornando false.
            Serial.println("<<<< falha na ESCRITA do arquivo >>>>");
            }  
      Serial.println("-------ARQUIVO CRIADO COM SUCESSO----------------");
      file.close(); 
        }
      Serial.println(" Inserindo novo registro : ");
          
          File file = SPIFFS.open(ARQUIVO, "a"); // Abre o arquivo, no modo escrita,

          if (!file) // Se houver falha ao abrir o caminho, ...
          {
          // informa ao usuário que houve falhas e sai da função retornando false.
            Serial.println(" falha abrir Arquivo");
          }
          if (file.print(json)) // Se a escrita do arquivo com seu conteúdo der certo, ...
          {
            // informa ao usuário que deu certo
            Serial.println(" <<< Registro Adicionado >>>");
          } else {
            // informa ao usuário que deu erros e sai da função retornando false.
            Serial.println("<<<< falha na ESCRITA do arquivo >>>>");
            }  
      Serial.printf("%s\n", json);
      Serial.println("-------REGISTRO INSERIDO COM SUCESSO----------------");
      file.close();
  
  sprintf(json, "{\"%s\":{\"value\":%02.02f}}", VARIABLE_LABEL_SPO2, espo2);
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
    geraAP(dados.bpm,dados.espo2);
    while (xQueueReceive(fila, &dados, 100 * portTICK_PERIOD_MS))
    {
      sendValues( dados.bpm, dados.espo2,
                  dados.dia, dados.mes,   dados.ano,
                  dados.hora,dados.minuto,dados.segundos); //, dados.mes, dados.ano
    }
  }
}

void createFile(void){
  File wFile;

  //Cria o arquivo se ele não existir
  if(SPIFFS.exists("/log.txt")){
    Serial.println("Arquivo ja existe!");
  } else {
    Serial.println("Criando o arquivo...");
    wFile = SPIFFS.open("/log.txt","w+");

    //Verifica a criação do arquivo
    if(!wFile){
      Serial.println("Erro ao criar arquivo!");
    } else {
      Serial.println("Arquivo criado com sucesso!");
    }
  }
  wFile.close();
}

void deleteFile(void) {
  //Remove o arquivo
  if(SPIFFS.remove("/log.txt")){
    Serial.println("Erro ao remover arquivo!");
  } else {
    Serial.println("Arquivo removido com sucesso!");
  }
}
 

void geraAP(int bmp,float spo2)
{
  WiFiClient client = sv.available(); // Cria o objeto cliente
  if (client)
  {                   // Se este objeto estiver disponível
    String line = ""; // Variável para armazenar os dados recebidos
    while (client.connected())
    { // Enquanto estiver conectado
      if (client.available())
      {                         // Se estiver disponível
        //char c = client.read(); // Lê os caracteres recebidos
        //if (c == '\n')
        //{ // Se houver uma quebra de linha
          if (line.length() == 0)
          {                                    // Se a nova linha tiver 0 de tamanho
            client.println("HTTP/1.1 200 OK"); // Envio padrão de início de comunicação
            client.println("Content-type:text/html");
            client.println();
           // client.print("<p>&nbsp;Nome:&nbsp;&nbsp;<input type=text />&nbsp; Idade:&nbsp;<input size=5 type=text />&nbsp; Sexo:&nbsp;<input size=10 type=text /></p>");
            client.print("<h1 style=text-align:center><span style=font-size:90px><strong>RESPIRE</strong></h1>");
            client.print("<hr />");
            
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
           // client.print("<p>&nbsp;</p>");
           // client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<h1 style=text-align:center><span style=font-size:58px><span style=color:#0000FF><strong>Satura&ccedil;&atilde;o de Oxig&ecirc;nio (%)</strong></span></span></h1>");
            client.print("<h1 style=text-align:center><span style=font-size:58px><span style=color:#0000FF>"+String(spo2));//client.print(spo2);
            client.print("<p>&nbsp;</p>");
            //client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<p>&nbsp;</p>");
            client.print("<h1 style=text-align:center><span style=font-size:58px><span style=color:#FF0000><strong>Batimentos Card&iacute;acos</strong></span></span></h1>");
            client.print("<h1 style=text-align:center><span style=font-size:58px><span style=color:#FF0001>"+String(bmp));//client.print(bmp);
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
        //}
      }
    client.stop(); // Para o cliente
    }
  }
}

