#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <signal.h>
#include <pthread.h>

#include "web.h"

#define UNUSED(x) (void)(x);

static int quit = 0;
static SOCKET clients[MAX_CLIENTS];
static size_t numClients = 0;
static SOCKET client_to_handle = -1;

typedef struct Request {
    char method[16];
    char path[256];
    char content[4096];
    int contentLength;
    int keepAlive;
} Request;

static Request* ParseRequest(char *buffer);
static void FreeRequest(Request* request);

static Request* ParseRequest(char *buffer) {
    Request* request;
    char header_name[256];
    char header_value[256];
    
    request = (Request*)malloc(sizeof(Request));
    if (request == NULL) {
        return NULL;
    }
    memset(request, 0, sizeof(Request));

    char *buf_ptr = buffer;
    
    // skopuj metodę
    size_t i = 0;
    char *method_ptr = request->method;
    while (*buf_ptr != ' ' && i < sizeof(request->method) - 1) {
        *method_ptr++ = *buf_ptr++;
        i++;
    }

    // przewiń spacje
    while (*buf_ptr == ' ') {
        buf_ptr++;
    }

    // skopuj ścieżkę
    i = 0;
    char *path_ptr = request->path;
    while (*buf_ptr != ' ' && i < sizeof(request->path) - 1) {
        *path_ptr++ = *buf_ptr++;
    }

    // przewiń do następnej linii
    while (*buf_ptr != '\n') {
        buf_ptr++;
    }
    buf_ptr++;

    // sparsuj kolejne nagłówki
    char *headers_end = strstr(buf_ptr, "\r\n\r\n");
    if (headers_end == NULL) {
        // nie wczytano całego żądania
        puts("Nie wczytano całego żądania");
        free(request);
        return NULL;
    }

    while (buf_ptr < headers_end)
    {
        // przewiń spacje
        while (*buf_ptr == ' ') {
            buf_ptr++;
        }

        // skopiuj nazwę nagłówka
        i = 0;
        char *header_name_ptr = header_name;
        while (*buf_ptr != ':' && i < sizeof(header_name) - 1) {
            *header_name_ptr++ = *buf_ptr++;
            i++;
        }
        *header_name_ptr = '\0';

        // przewiń dwukropek
        buf_ptr++;

        // przewiń spacje
        while (*buf_ptr == ' ') {
            buf_ptr++;
        }

        // skopiuj wartość nagłówka
        i = 0;
        char *header_value_ptr = header_value;
        while (*buf_ptr != '\r' && i < sizeof(header_value) - 1) {
            *header_value_ptr++ = *buf_ptr++;
            i++;
        }
        *header_value_ptr = '\0';

        // przewiń do następnej linii
        while (*buf_ptr != '\n') {
            buf_ptr++;
        }
        buf_ptr++;

        if (strcmp(header_name, "Connection") == 0) {
            if (strcmp(header_value, "close") == 0) {
                request->keepAlive = 0;
            } else {
                request->keepAlive = 1;
            }
        }

        if (strcmp(header_name, "Content-Length") == 0) {
            request->contentLength = atoi(header_value);
        }
    }

    if (request->contentLength > 0) {
        // przewiń do początku treści
        buf_ptr = headers_end + 4;

        // skopiuj treść
        i = 0;
        char *content_ptr = request->content;
        while (*buf_ptr != '\0' && i < sizeof(request->content) - 1) {
            *content_ptr++ = *buf_ptr++;
            i++;
        }
        *content_ptr = '\0';
    }

    return request;
}

static void FreeRequest(Request* request) {
    free(request);
}

typedef enum HandleRequestResult {
    HRR_KEEP_ALIVE,
    HRR_CLOSE_CONNECTION,
    HRR_ERROR,
} HandleRequestResult;

static HandleRequestResult HandleRequest(SOCKET conn)
{
    // todo: recvall
    char buffer[4096];
    int rv = recv(conn, buffer, sizeof(buffer) - 1, 0);
    if (rv < 0) {
        // błąd
        return HRR_ERROR;
    }
    else if (rv == 0) {
        // zamknięcie połączenia przez klienta
        return HRR_CLOSE_CONNECTION;
    }
    else {
        // odebrano dane
        buffer[1023] = '\0';
        // printf("Received: %s", buffer);

        Request* request = ParseRequest(buffer);
        if (request == NULL)
        {
            puts("Scheduler: error parsing request");
            return HRR_ERROR;
        }

        printf("Method: %s, Path: %s, Content-Length: %d, Keep-Alive: %d\n", request->method, request->path, request->contentLength, request->keepAlive);

        int keepAlive = request->keepAlive;
        FreeRequest(request);

        char path[256];
        strncpy(path, request->path, 256);

        // wyślij odpowiedź
        // Zapobieganie wychodzeniu z katalogu statycznego public
        char *dbl_dots = strstr(path, "..");
        if (dbl_dots != NULL) {
            puts("Zlodziej!!! Klient probowal wyjsc poza katalog public");
            return HRR_CLOSE_CONNECTION;
        }

        // Obsługa zapytań plikow statycznych
        char file_path[512];
        strcpy(file_path, "../build");
        strncat(file_path, path, 256);
        // printf("Sciezka do pliku: %s\n", file_path);

        FILE *file = fopen(file_path, "rb");
        if (file == NULL)
        {
            char response[] = "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n\r\n{\"status\": \"Not Found\"}";
            send(conn, response, strlen(response), 0);
            return HRR_KEEP_ALIVE;        
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
        } else if (strcmp(extension, ".json") == 0) {
            strcpy(mime_type, "application/json");
        } else if (strcmp(extension, ".svg") == 0) {
            strcpy(mime_type, "image/svg+xml");
        } else if (strcmp(extension, ".map") == 0) {
            strcpy(mime_type, "application/json");
        } else {
            printf("Nieznany typ pliku: %s\n", extension);
            strcpy(mime_type, "text/plain");
        }

        char response_header[256];
        strcpy(response_header, "HTTP/1.1 200 OK\r\nContent-Type: ");
        strcat(response_header, mime_type);
        strcat(response_header, "\r\nContent-Length: ");
        char content_length[32];
        snprintf(content_length, 32, "%lld", file_size);
        strcat(response_header, content_length);
        strcat(response_header, "\r\n\r\n");
        send(conn, response_header, strlen(response_header), 0);
        send(conn, file_data, file_size, 0);

        free(file_data);

        return keepAlive ? HRR_KEEP_ALIVE : HRR_CLOSE_CONNECTION;
    }

    return HRR_ERROR;
}

static void *Scheduler(void*) {
    FD_SET fds;
    struct timeval timeout;

    // Ustaw timeout na dziesięć milisekund
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000;

    while (!quit) {
        if (numClients == 0) {
            // nie ma żadnych klientów, czekaj dziesięć milisekund
            select(0, NULL, NULL, NULL, &timeout);
            continue;
        }

        SOCKET maxFd = 0;
        FD_ZERO(&fds);
        for (size_t i = 0; i < numClients; ++i) {
            FD_SET(clients[i], &fds);
            if (clients[i] > maxFd) {
                maxFd = clients[i];
            }
        }
        
        int rv = select(maxFd + 1, &fds, NULL, NULL, &timeout);
        if (rv < 0) {
            // błąd selecta
            puts("Scheduler: select error");
            quit = 1;
            return NULL;
        } else if (rv == 0) {
            // timeout
            continue;
        } else {
            for (size_t i = 0; i < numClients; ++i) {
                if (FD_ISSET(clients[i], &fds)) {
                    switch (HandleRequest(clients[i]))
                    {
                        case HRR_KEEP_ALIVE:
                            // nic nie rób
                            break;
                        case HRR_CLOSE_CONNECTION:
                            puts("Connection closed");
                            // zamknij połączenie
                            closesocket(clients[i]);
                            clients[i] = clients[numClients - 1];
                            --numClients;
                            break;
                        case HRR_ERROR:
                            // błąd, wyłącz serwer
                            puts("Scheduler: error handling request");
                            quit = 1;
                            break;
                        default:
                            // bug w handlerze requestów, wyłącz serwer
                            puts("Scheduler: unknown HandleRequestResult");
                            quit = 1;
                            break;
                    }
                }
            }
        }
    }

    return NULL;
}

static void HandleSigint(int sig) {
    UNUSED(sig);
    if (quit) {
        puts("Forcing shutdown...");
        exit(1);
    }
    puts("Requesting shutdown... Press Ctrl+C again to force shutdown.");
    quit = 1;
}

int main(void) {
    WSADATA wsaData;
    struct sockaddr_in serverAddress, clientAddress;
    int clientAddressSize = sizeof(clientAddress);
    SOCKET listenSocket, clientSocket; 
    FD_SET acceptFds, readFds;
    struct timeval timeout;

    signal(SIGINT, HandleSigint);

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
    serverAddress.sin_port = htons(DEFAULT_PORT);

    // Bindowanie gniazda nasłuchującego do adresu serwera
    if (bind(listenSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        printf("Blad: Nie mozna zbindowac gniazda nasluchujacego\n");
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    // Przełącz gniazdo słuchające w tryb nieblokujący
    u_long nonBlocking = 1;
    if (ioctlsocket(listenSocket, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
        printf("Blad: Nie mozna przejsc w tryb nieblokujacy\n");
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    // Nasłuchiwanie na porcie
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        printf("Blad: Nie mozna nasluchiwac na porcie\n");
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }
    printf("Server started. Listening for connections on Port: %d ...\n", DEFAULT_PORT);

    // Utworzenie wątku obsługującego połącznia
    pthread_t scheduler;
    pthread_create(&scheduler, NULL, Scheduler, NULL);

    // Ustaw timeout na jedna sekunde
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    while (!quit) {
        FD_ZERO(&acceptFds);
        FD_SET(listenSocket, &acceptFds);
        int rv = select(listenSocket + 1, &acceptFds, NULL, NULL, &timeout); 
        if (rv < 0) {
            // bład selecta
            printf("Blad: Nie mozna wykonywac select\n");
            closesocket(listenSocket);
            WSACleanup();
            return 1;
        } else if (rv == 0) {
            // timeout
            continue;
        } else {
            if (!FD_ISSET(listenSocket, &acceptFds)) {
                // nie ma żadnego połączenia
                continue;
            }

            SOCKET client = accept(listenSocket, NULL, NULL);
            if (client == INVALID_SOCKET) {
                // błąd accepta
                printf("Blad: Nie mozna przyjac polaczenia\n");
                closesocket(listenSocket);
                WSACleanup();
                return 1;
            } 

            // przyjęto połączenie
            if (numClients < MAX_CLIENTS) {
                puts("Client connected.");
                clients[numClients++] = client;
            } else {
                // zbyt dużo klientów
                puts("Too many clients");
                printf("Blad: Zbyt duzo klientow\n");
                closesocket(client);
            }
        }
    }

    // Zamykanie gniazda nasłuchującego
    closesocket(listenSocket);

    // Czekanie na zakończenie wątków
    pthread_join(scheduler, NULL);

    // Czyszczenie biblioteki Winsock
    WSACleanup();
    puts("Server shutdown complete.");
    
    return 0;
}

//int main() {
//    int addressSize;
//    WSADATA wsaData;
//    SOCKET listenSocket, clientSocket;
//    struct sockaddr_in serverAddress, clientAddress;
//    char buffer[1024];
//    int clientAddressSize = sizeof(clientAddress);
//
//    // Inicjalizacja biblioteki Winsock
//    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
//        printf("Blad: Nie mozna zainicjowac biblioteki Winsock\n");
//        return 1;
//    }
//
//    // Tworzenie gniazda nasłuchującego
//    if ((listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET) {
//        printf("Blad: Nie mozna utworzyc gniazda nasluchujacego\n");
//        WSACleanup();
//        return 1;
//    }
//
//    // Konfiguracja adresu serwera
//    serverAddress.sin_family = AF_INET;
//    serverAddress.sin_addr.s_addr = INADDR_ANY;
//    serverAddress.sin_port = htons(DEFAULT_PORT);
//
//    // Ustawienie opcji SO_KEEPALIVE
//    int enableKeepAlive = 1;
//    if (setsockopt(listenSocket, SOL_SOCKET, SO_KEEPALIVE, (const char*)&enableKeepAlive, sizeof(int)) == SOCKET_ERROR) {
//        printf("Blad: Nie mozna ustawic opcji SO_KEEPALIVE\n");
//        closesocket(listenSocket);
//        WSACleanup();
//        return 1;
//    }
//
//    // Bindowanie gniazda nasłuchującego do adresu serwera
//    if (bind(listenSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
//        printf("Blad: Nie mozna zbindowac gniazda nasluchujacego\n");
//        closesocket(listenSocket);
//        WSACleanup();
//        return 1;
//    }
//
//    // Nasłuchiwanie na porcie
//    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
//        printf("Blad: Nie mozna nasluchiwac na porcie\n");
//        closesocket(listenSocket);
//        WSACleanup();
//        return 1;
//    }
//
//    // Akceptowanie połączeń od klientów
//    while ((clientSocket = accept(listenSocket, (struct sockaddr*)&clientAddress, &clientAddressSize)) != INVALID_SOCKET) {
//        // printf("Polaczono z %s:%d\n", inet_ntoa(clientAddress.sin_addr), ntohs(clientAddress.sin_port));
//
//        // Obsługa żądania klienta
//        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
//        if (bytesReceived == SOCKET_ERROR) {
//            printf("Blad: Nie mozna odebrac danych od klienta\n");
//            closesocket(clientSocket);
//            continue;
//        }
//
//        buffer [bytesReceived] = '\0';
//        // printf("Otrzymano zapytanie:\n%s\n", buffer);
//
//        char method[64];
//        char path[256];
//        char args[256];
//        if (sscanf_s(buffer, "%s %s", method, 64, path, 256) != 2) {
//            printf("Nie udalo sie sparsowac zapytania");
//            closesocket(clientSocket);
//            continue;
//        }
//        printf("Zapytanie: %s %s \n", method, path);
//
//        // const char* response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<html><body><h1>Hello, world!</h1></body></html>";
//
//        char filePath[256];
//        strcpy(filePath, "../build");
//        strcat(filePath, path);
//
//        FILE* file = fopen(filePath, "rb");
//        if (file == NULL) {
//            char response[] = "HTTP/1.1 404 Not Found\r \nContent-Type: text/html\r\n\r\n<html><body><h1>404 Not Found</h1></body></html>";
//            send(clientSocket, response, strlen(response), 0);
//            closesocket(clientSocket);
//            continue;
//        }
//
//        fseek(file, 0, SEEK_END);
//        size_t fileSize = ftell(file);
//        fseek(file, 0, SEEK_SET);
//        void *fileContent = malloc(fileSize);
//        fread(fileContent, 1, fileSize, file);
//        fclose(file);
//
//        char *extension = strrchr(filePath, '.');
//        if (extension == NULL) {
//            extension = "";
//        }
//
//        char mime_type[128];
//        if (strcmp(extension, ".html") == 0) {
//            strcpy(mime_type, "text/html");
//        } else if (strcmp(extension, ".css") == 0) {
//            strcpy(mime_type, "text/css");
//        } else if (strcmp(extension, ".js") == 0) {
//            strcpy(mime_type, "application/javascript");
//        } else if (strcmp(extension, ".png") == 0) {
//            strcpy(mime_type, "image/png");
//        } else if (strcmp(extension, ".jpg") == 0) {
//            strcpy(mime_type, "image/jpeg");
//        } else if (strcmp(extension, ".gif") == 0) {
//            strcpy(mime_type, "image/gif");
//        } else if (strcmp(extension, ".ico") == 0) {
//            strcpy(mime_type, "image/x-icon");
//        } else if (strcmp(extension, ".json") == 0) {
//            strcpy(mime_type, "application/json");
//        } else {
//            strcpy(mime_type, "text/plain");
//        }
//
//        char responseHeader[1024];
//        sprintf(responseHeader, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n", mime_type, (int)fileSize);
//        send(clientSocket, responseHeader, strlen(responseHeader), 0);
//        send(clientSocket, fileContent, fileSize, 0);
//        free(fileContent);
//
//        // int bytesSent = send(clientSocket, response, strlen(response), 0);
//        // if(bytesSent == SOCKET_ERROR) {
//        //     printf("Blad: Nie mozna wyslac danych do klienta\n");
//        //     closesocket(clientSocket);
//        //     continue;
//        // }
//    }
//
//    closesocket(listenSocket);
//    WSACleanup();
//    return 0;
//}

    // while (1) {    
    //     // Akceptowanie połączenia przychodzącego
    //     if ((clientSocket = accept(listenSocket, (struct sockaddr*)&clientAddress, &clientAddressSize)) == INVALID_SOCKET) {
    //         printf("Blad: Nie można zaakceptowac polaczenia przychodzącego\n");
    //         closesocket(listenSocket);
    //         WSACleanup();
    //         return 1;
    //     }

    //     // puts("Przed recv");
    //     recv(clientSocket, buffer, 1024, 0);
    //     printf("Otrzymano zapytanie:\n%s\n", buffer);
    //     // puts("Po recv");
        
    //     char method[64];
    //     char path[256];
    //     char args[256];
    //     // Parsowanie zapytania HTTP i zapisanie metody i ścieżki w zmiennych method i path
    //     // Używamy funkcji sscanf_s, która działa podobnie jak scanf, ale jest bardziej bezpieczna (posiada dodatkowe zabezpieczenia przed przepełnieniami bufora).
    //     // Funkcja sscanf_s oczekuje w pierwszym argumencie ciągu znaków (bufora), które chcemy sparsować, a w kolejnych argumentach nazwy zmiennych, do których chcemy zapisać sparsowane wartości.
    //     // W przypadku niepowodzenia funkcja zwróci wartość inna niż 2 (liczba sparsowanych wartości).
    //     if (sscanf_s(buffer, "%s %s", method, 64, path, 256) != 2){
    //         printf("Nie udalo sie sparsowac zapytania");
    //         closesocket(clientSocket);
    //         continue;
    //     }
    //     printf("Zapytanie: %s, Sciezka: %s\n", method, path);

    //     char *question_mark= strchr(path, '?');
    //     if (question_mark != NULL) {
    //         // Zapisanie argumentów zapytania w zmiennej args
    //         strcpy_s(args, 256, question_mark + 1);
    //         // Usunięcie argumentów z ścieżki
    //         *question_mark = '\0';
    //     }

    //     // Zapobieganie wychodzeniu z katalogu statycznego public
    //     char *dbl_dots = strstr(path, "..");
    //     if (dbl_dots != NULL) {
    //         puts("Zlodziej!!! Klient probowal wyjsc poza katalog public");
    //         closesocket(clientSocket);
    //         continue;
    //     }

    //     // Obsługa zapytań plikow statycznych
    //     char file_path[512];
    //     strcpy(file_path, "../build");
    //     strncat(file_path, path, 256);
    //     // printf("Sciezka do pliku: %s\n", file_path);

    //     FILE *file = fopen(file_path, "rb");
    //     if (file == NULL)
    //     {
    //         char response[] = "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n\r\n{\"status\": \"Not Found\"}";
    //         send(clientSocket, response, strlen(response), 0);
    //         closesocket(clientSocket);
    //         continue;        
    //     }
    //     fseek(file, 0, SEEK_END);
    //     size_t file_size = ftell(file);
    //     fseek(file, 0, SEEK_SET);
    //     void* file_data = malloc(file_size);
    //     fread(file_data, file_size, 1, file);
    //     fclose(file);

    //     // dobranie mime type na podstawie rozszerzenia pliku
    //     char *extension = strrchr(file_path, '.');
    //     if (extension == NULL) {
    //         extension = "";
    //     }

    //     // TODO: obsługa innych typów plików
    //     char mime_type[64];
    //     if (strcmp(extension, ".html") == 0) {
    //         strcpy(mime_type, "text/html");
    //     } else if (strcmp(extension, ".css") == 0) {
    //         strcpy(mime_type, "text/css");
    //     } else if (strcmp(extension, ".js") == 0) {
    //         strcpy(mime_type, "application/javascript");
    //     } else if (strcmp(extension, ".png") == 0) {
    //         strcpy(mime_type, "image/png");
    //     } else if (strcmp(extension, ".jpg") == 0) {
    //         strcpy(mime_type, "image/jpeg");
    //     } else if (strcmp(extension, ".gif") == 0) {
    //         strcpy(mime_type, "image/gif");
    //     } else if (strcmp(extension, ".ico") == 0) {
    //         strcpy(mime_type, "image/x-icon");
    //     } else if (strcmp(extension, ".json") == 0) {
    //         strcpy(mime_type, "application/json");
    //     } else {
    //         strcpy(mime_type, "text/plain");
    //     }

    //     char response_header[256];
    //     strcpy(response_header, "HTTP/1.1 200 OK\r\nContent-Type: ");
    //     strcat(response_header, mime_type);
    //     strcat(response_header, "\r\n\r\n");
    //     send(clientSocket, response_header, strlen(response_header), 0);
    //     send(clientSocket, file_data, file_size, 0);
    //     closesocket(clientSocket);

    //     free(file_data);
    //     continue;


    //     // Wysyłanie odpowiedzi do klienta
    //     char response[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"status\": \"OK\"}";
    //     send(clientSocket, response, strlen(response), 0);

    //     // Zamykanie połączenia z klientem
    //     closesocket(clientSocket);
    // }