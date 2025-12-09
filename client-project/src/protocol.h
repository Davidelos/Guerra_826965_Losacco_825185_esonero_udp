/*
 * protocol.h
 */

#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#if defined _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
    #define strcasecmp _stricmp
    typedef int socklen_t;
    static void cleanup_winsock(void) { WSACleanup(); }
#else
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <strings.h>
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#define DEFAULT_PORT 56700
#define DEFAULT_IP "localhost" /* Default cambiato a localhost come da esempi traccia, ma gestito dinamico */
#define CITY_LEN 64

/* * NOTA: Non useremo queste struct direttamente nelle sendto/recvfrom.
 * Useremo buffer di byte per serializzare/deserializzare manualmente.
 */

// Messaggio di Richiesta (Client -> Server)
typedef struct {
    char type;           // 't', 'h', 'w', 'p'
    char city[CITY_LEN]; // Nome della città
} weather_request_t;

// Messaggio di Risposta (Server -> Client)
typedef struct {
    unsigned int status; // 0=OK, 1=No Città, 2=Tipo non valido
    char type;           // Eco del tipo di dato richiesto
    float value;         // Il valore meteo
} weather_response_t;

#endif /* PROTOCOL_H_ */
