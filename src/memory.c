/*#include "SPIFFS.h"
#include "FS.h"
i

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

void writeFile(String msg) {
 
  //Abre o arquivo para adição (append)
  //Inclue sempre a escrita na ultima linha do arquivo
  File rFile = SPIFFS.open("/log.txt","a+");
 
  if(!rFile){
    Serial.println("Erro ao abrir arquivo!");
  } else {
    rFile.println("Log: " + msg);
    Serial.println(msg);
  }
  rFile.close();
}
 
void readFile(void) {

  //Faz a leitura do arquivo
  File rFile = SPIFFS.open("/ESCRITA.txt","r");
  Serial.println("Reading file...");
  while(rFile.available()) {
    String line = rFile.readStringUntil('\n');
    buf += line;
    buf += "<br />";
  }
  rFile.close();
}
void writeFile(String msg) {
 
  //Abre o arquivo para adição (append)
  //Inclue sempre a escrita na ultima linha do arquivo
  File rFile = SPIFFS.open("/log.txt","a+");
 
  if(!rFile){
    Serial.println("Erro ao abrir arquivo!");
  } else {
    rFile.println("Log: " + msg);
    Serial.println(msg);
  }
  rFile.close();
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
}*/