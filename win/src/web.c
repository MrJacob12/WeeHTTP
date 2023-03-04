#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

int main() {
    WSADATA wsaData;
    SOCKET listenSocket, clientSocket;
    struct sockaddr_in serverAddress, clientAddress;
    char buffer[1024];
    int clientAddressSize = sizeof(clientAddress);

    // Inicjalizacja biblioteki Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Blad: Nie mozna zainicjowac biblioteki Winsock\n");
        return 1;
    }

    // Tworzenie gniazda nasłuchującego
    if ((listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET) {
        printf("Blad: Nie mozna utworzyc gniazda nasluchujacego\n");
        WSACleanup();
        return 1;
    }

    // Konfiguracja adresu serwera
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(80);

    // Bindowanie gniazda nasłuchującego do adresu serwera
    if (bind(listenSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        printf("Blad: Nie mozna zbindowac gniazda nasluchujacego\n");
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    // Ustawienie gniazda nasłuchującego w tryb nasłuchiwania
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        printf("Blad: Nie mozna ustawic gniazda nasluchujacego w tryb nasluchiwania\n");
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    printf("Serwer jest gotowy do obslugi zadan HTTP na porcie 80\n");

    while (1) {
        // Akceptowanie połączenia przychodzącego
        if ((clientSocket = accept(listenSocket, (struct sockaddr*)&clientAddress, &clientAddressSize)) == INVALID_SOCKET) {
            printf("Blad: Nie można zaakceptowac polaczenia przychodzącego\n");
            closesocket(listenSocket);
            WSACleanup();
            return 1;
        }

        puts("Przed recv");
        recv(clientSocket, buffer, 1024, 0);
        // printf("Otrzymano zapytanie:\n%s\n", buffer);
        puts("Po recv");
        
        char method[64];
        char path[256];
        char args[256];
        // Parsowanie zapytania HTTP i zapisanie metody i ścieżki w zmiennych method i path
        // Używamy funkcji sscanf_s, która działa podobnie jak scanf, ale jest bardziej bezpieczna (posiada dodatkowe zabezpieczenia przed przepełnieniami bufora).
        // Funkcja sscanf_s oczekuje w pierwszym argumencie ciągu znaków (bufora), które chcemy sparsować, a w kolejnych argumentach nazwy zmiennych, do których chcemy zapisać sparsowane wartości.
        // W przypadku niepowodzenia funkcja zwróci wartość inna niż 2 (liczba sparsowanych wartości).
        if (sscanf_s(buffer, "%s %s", method, 64, path, 256) != 2){
            printf("Nie udalo sie sparsowac zapytania");
            closesocket(clientSocket);
            continue;
        }
        printf("Zapytanie: %s, Sciezka: %s\n", method, path);

        char *question_mark= strchr(path, '?');
        if (question_mark != NULL) {
            // Zapisanie argumentów zapytania w zmiennej args
            strcpy_s(args, 256, question_mark + 1);
            // Usunięcie argumentów z ścieżki
            *question_mark = '\0';
        }

        // Zapobieganie wychodzeniu z katalogu statycznego public
        char *dbl_dots = strstr(path, "..");
        if (dbl_dots != NULL) {
            puts("Zlodziej!!! Klient probowal wyjsc poza katalog public");
            closesocket(clientSocket);
            continue;
        }

        // Obsługa zapytań plikow statycznych
        char file_path[512];
        strcpy(file_path, "../build");
        strncat(file_path, path, 256);
        printf("Sciezka do pliku: %s\n", file_path);

        FILE *file = fopen(file_path, "rb");
        if (file == NULL)
        {
            char response[] = "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n\r\n{\"status\": \"Not Found\"}";
            send(clientSocket, response, strlen(response), 0);
            closesocket(clientSocket);
            continue;        
        }
        fseek(file, 0, SEEK_END);
        size_t file_size = ftell(file);
        fseek(file, 0, SEEK_SET);
        void* file_data = malloc(file_size);
        fread(file_data, file_size, 1, file);
        fclose(file);

        // dobranie mime type na podstawie rozszerzenia pliku
        char *extension = strrchr(file_path, '.');
        if (extension == NULL) {
            extension = "";
        }

        // TODO: obsługa innych typów plików
        char mime_type[64];
        if (strcmp(extension, ".html") == 0) {
            strcpy(mime_type, "text/html");
        } else if (strcmp(extension, ".css") == 0) {
            strcpy(mime_type, "text/css");
        } else if (strcmp(extension, ".js") == 0) {
            strcpy(mime_type, "application/javascript");
        } else if (strcmp(extension, ".png") == 0) {
            strcpy(mime_type, "image/png");
        } else if (strcmp(extension, ".jpg") == 0) {
            strcpy(mime_type, "image/jpeg");
        } else if (strcmp(extension, ".gif") == 0) {
            strcpy(mime_type, "image/gif");
        } else if (strcmp(extension, ".ico") == 0) {
            strcpy(mime_type, "image/x-icon");
        } else {
            strcpy(mime_type, "text/plain");
        }

        char response_header[256];
        strcpy(response_header, "HTTP/1.1 200 OK\r\nContent-Type: ");
        strcat(response_header, mime_type);
        strcat(response_header, "\r\n\r\n");
        send(clientSocket, response_header, strlen(response_header), 0);
        send(clientSocket, file_data, file_size, 0);
        closesocket(clientSocket);

        free(file_data);
        continue;


        // Wysyłanie odpowiedzi do klienta
        char response[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"status\": \"OK\"}";
        send(clientSocket, response, strlen(response), 0);

        // Zamykanie połączenia z klientem
        closesocket(clientSocket);
    }
    // Zamykanie gniazda nasłuch
    closesocket(listenSocket);
    // Wyjście z biblioteki Winsock
    WSACleanup();
    return 0;
}