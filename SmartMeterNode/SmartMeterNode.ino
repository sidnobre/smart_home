#include <ESP8266HTTPClient.h>
#include <NTPClient.h>
#include <ArduinoJson.h>
#include "EmonLib.h"
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

//CONSTANTES
const char *ssid     = "SDN";
const char *password = "sidney00";
//const String url = "http://10.0.1.101/smartmeter/hardware/";
const String url = "http://ec2-54-207-190-218.sa-east-1.compute.amazonaws.com/smartmeter/hardware/";


//Fuso Horario -3 * 3600
const long utcOffsetInSeconds = -10800;

const float valorKwh = 0.7988; //valor do kwh pela concessionaria
const float tensao = 220.0;
const float fator_potencia = 0.83; //fornecido pela concessionaria

//VARIAVEIS
int dia,
    mes,
    ano;
unsigned long ltmillis = 0.0,
              tmillis = 0.0,
              timems = 0.0;

float corrente = 0.0,
      potencia = 0.0,
      consumoValor = 0.0;

double medicao = 0.0,
       consumoDia = 0.0,
       consumoKwh = 0.0;

WiFiUDP ntpUDP;
NTPClient tm(ntpUDP, "a.st1.ntp.br", utcOffsetInSeconds);
EnergyMonitor emon;
hd44780_I2Cexp lcd;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);

  //INICIALIZACAO DO MONITOR SERIAL
  Serial.begin(115200);

  Serial.println("");
  Serial.println("[S M A R T M E T E R]");

  //INICIALIZACAO DO LCD
  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("  SMART  METER");
  lcd.setCursor(0, 1);

  //WIFI
  WiFi.begin(ssid, password);

  lcd.print("WIFI Conectando");
  Serial.print("[WIFI] Conectando ");
  int count = 0;
  while ( WiFi.status() != WL_CONNECTED ) {
    if (count < 50) {
      delay ( 200 );
      Serial.print ( "." );
      count++;
    }
    else {
      ESP.restart();
    }
  }
  Serial.println("");

  //NTP
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  SMART  METER");
  lcd.setCursor(0, 1);
  lcd.print("NTP Sicronizando");
  tm.begin();
  sincronizarNTP();

  //CONFIGURACOES SENSORES DE TENSAO E CORRENTE
  emon.current(0, 6.06060606);       // Amperimetro: pino A0, calibragem (2000 espiras / 330ohm)

  //RECUPERAR DADOS
  recuperarDadosMedicaoMes();
  recuperarDadosMedicaoDia();
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  medir();
  exibirDados();


  if (dia >= 1 && mes >= 1 && ano >= 2020) {
    apiPost();
  }

  mudouData(getDia(), tm.getMonth(), tm.getYear()); //alterar para o mes

  digitalWrite(LED_BUILTIN, LOW);
  delay(650);
}

void medir() {
  //VERIFICA O TEMPO DECORRIDO DESDE A ULTIMA MEDICAO
  ltmillis = tmillis;
  tmillis = millis();
  timems = tmillis - ltmillis;

  Serial.print("[TIME]");
  Serial.println(timems / 1000.0);

  corrente = emon.calcIrms(1480); //corrente ATIVA;

  if (corrente <= 0.015) { //Se corrente menor que 15mA
    corrente = 0.0;
  }

  potencia = (corrente * tensao ); //Potencia ATIVA em Watt
  corrente = corrente * 1.3125; //corrente aparente;
  //medicao = ((potencia / 1000.0) / (3600.0) * (timems / 1000.0) * fator_potencia);
  medicao = ((potencia / 1000.0) / (3600.0) * (timems / 1000.0));
  consumoDia += medicao;
  consumoKwh += medicao;
  consumoValor = consumoKwh * valorKwh;

}

void exibirDados() {
  lcd.clear();

  lcd.setCursor(0, 0);
  if (consumoKwh < 1.0) {
    lcd.print(consumoKwh * 1000, 1);
    lcd.print("Wh");
  }
  else {
    lcd.print(consumoKwh, 1);
    lcd.print("kWh");
  }

  lcd.setCursor(9, 0);
  lcd.print(corrente, 2);
  lcd.print("A");


  lcd.setCursor(0, 1);
  lcd.print("R$");
  lcd.print(consumoValor, 2);

  lcd.setCursor(9, 1);
  if (potencia < 1000) {
    lcd.print(potencia, 1);
    lcd.print("W");
  }
  else {
    lcd.print(potencia / 1000.0, 2);
    lcd.print("kW");
  }

  Serial.print("[MEDICAO] ");
  Serial.print(corrente, 5);
  Serial.print("A // ");
  Serial.print(potencia, 0);
  Serial.print("W // ");
  Serial.print(consumoKwh, 6);
  Serial.print("kWh // ");
  Serial.print("R$");
  Serial.println(consumoValor, 2);

}

void mudouData(int diaMedicao, int mesMedicao, int anoMedicao) {
  if (diaMedicao != dia) {
    consumoDia = 0;
    dia = diaMedicao;

    if (mesMedicao != mes) {
      consumoKwh = 0;
      mes = mesMedicao;

      if (anoMedicao != ano) {
        ano = anoMedicao;
      }
    }
  }
}

String criarJson() {
  const size_t capacity = JSON_OBJECT_SIZE(7) + 70;
  DynamicJsonDocument doc(capacity);

  doc["dia"] = dia;
  doc["mes"] = mes;
  doc["ano"] = ano;
  doc["corrente"] = corrente;
  doc["potencia"] = potencia;
  doc["consumoDia"] = consumoDia;
  doc["consumoKwh"] = consumoKwh;

  String json;

  serializeJson(doc, json);
  return json;

}

void apiPost() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;  //Object of class HTTPClient
    http.begin(url + "inserir_dados.php");
    http.addHeader("Content-Type", "application/json");
    Serial.print("[HTTP-POST] ");
    if (dia != 0 && mes != 0 && ano != 0) {
      int httpCode = http.POST(criarJson());
      String payload = http.getString();

      if (payload != "") {
        Serial.println(payload);
      } else {
        Serial.println(httpCode);
      }
    } else {
      Serial.println("Data invalida");
    }

    //Check the returning code
    http.end();
  }
  else {

    Serial.println("[WIFI] Conexao perdida");
  }
}

void recuperarDadosMedicaoMes() {
  // Check WiFi Status
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;  //Object of class HTTPClient
    http.begin(url + "medicao_mes.php");
    int httpCode = http.GET();
    //Check the returning code
    if (httpCode > 0) {
      const size_t capacity = 5 * JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(4) + 60;
      DynamicJsonDocument doc(capacity);
      String payload = http.getString();

      DeserializationError err = deserializeJson(doc, http.getString());

      Serial.print("[HTTP-GET] ");

      if (err) {
        Serial.print(F(" DeserializeJson() falhou: "));
        Serial.println(err.c_str());
      }

      if (mes == doc["mes"].as<int>() && ano == doc["ano"].as<int>()) {
        consumoKwh = doc["consumoKwh"].as<float>();
        Serial.println("Dados de Consumo Mes atualizados");
      }
      else {
        Serial.print("ERRO: Recuperacao de dados MES falhou");
      }

    }
    http.end();   //Close connection
  }
}

void recuperarDadosMedicaoDia() {
  // Check WiFi Status
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;  //Object of class HTTPClient
    http.begin(url + "medicao_dia.php");
    int httpCode = http.GET();
    //Check the returning code
    if (httpCode > 0) {
      const size_t capacity = 5 * JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(5) + 70;
      DynamicJsonDocument doc(capacity);
      String payload = http.getString();

      DeserializationError err = deserializeJson(doc, http.getString());

      Serial.print("[HTTP-GET] ");

      if (err) {
        Serial.print(F(" DeserializeJson() falhou: "));
        Serial.println(err.c_str());
      }
      if (dia == doc["dia"].as<int>() && mes == doc["mes"].as<int>() && ano == doc["ano"].as<int>()) {
        consumoDia = doc["consumoDia"].as<float>();
        Serial.println("Dados de Consumo Dia atualizados");
      } else {
        Serial.print("ERRO: Recuperacao de dados DIA falhou");
      }
    }
    http.end();   //Close connection
  }
}

int getDia() {
  String dia = tm.getFormattedDate().substring(8, 10);
  return dia.toInt();
}

void sincronizarNTP() {
  tm.forceUpdate();
  Serial.print("[NTP] Sicronizando ");
  int count = 0;
  while (tm.getYear() < 2020) {
    if (count < 50) {
      delay ( 200 );
      Serial.print ( "." );
      count++;
    }
    else {
      ESP.restart();
    }
  }
  Serial.println("");

  //DATA
  dia = getDia();
  mes = tm.getMonth();
  ano = tm.getYear();
}
