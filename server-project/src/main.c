#include "protocol.h"

// Funzioni Meteo (Simulazione - INVARIATE)
float get_random_float(float min, float max) {
    return min + (rand() / (float) RAND_MAX) * (max - min);
}

float get_temperature() { return get_random_float(-10.0, 40.0); } // Aggiornato range come da traccia
float get_humidity()    { return get_random_float(20.0, 100.0); } // Aggiornato range
float get_wind()        { return get_random_float(0.0, 100.0); }  // Aggiornato range
float get_pressure()    { return get_random_float(950.0, 1050.0);}// Aggiornato range

int is_city_valid(const char* city) {
    const char* valid_cities[] = {
        "bari", "roma", "milano", "napoli", "torino",
        "palermo", "genova", "bologna", "firenze", "venezia"
    };
    for (int i = 0; i < 10; i++) {
        if (strcasecmp(city, valid_cities[i]) == 0) return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    srand((unsigned int)time(NULL));

    int port = DEFAULT_PORT;
    if (argc == 3 && strcmp(argv[1], "-p") == 0) {
        port = atoi(argv[2]);
    }
    printf("Server avviato sulla porta %d\n", port);

    // Setup Winsock (solo Windows)
    #if defined _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
             printf("Errore critico: WSAStartup fallita.\n");
             return -1;
        }
    #endif

    // --- 1. Creazione Socket UDP ---
    int server_socket = socket(PF_INET, SOCK_DGRAM, 0);
    if (server_socket < 0) {
        perror("Impossibile creare il socket");
        return -1;
    }

    // --- 2. Bind ---
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Errore Bind. Porta occupata?\n");
        close(server_socket);
        return -1;
    }

    printf("In attesa di richieste...\n");

    // --- 3. Loop Infinito ---
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        char recv_buffer[512]; // Buffer per ricevere dati grezzi

        // Ricezione datagramma
        ssize_t bytes_read = recvfrom(server_socket, recv_buffer, sizeof(recv_buffer), 0,
                                      (struct sockaddr*)&client_addr, &client_len);

        if (bytes_read <= 0) continue; // Ignora pacchetti vuoti o errori temporanei

        // --- 4. Risoluzione DNS Client per Log ---
        char client_ip[INET_ADDRSTRLEN];
        char client_host[NI_MAXHOST];

        // Ottieni IP stringa
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

        // Ottieni Nome Host (Reverse Lookup)
        if (getnameinfo((struct sockaddr*)&client_addr, client_len,
                        client_host, sizeof(client_host),
                        NULL, 0, 0) != 0) {
            strcpy(client_host, client_ip); // Fallback su IP se nome non risolvibile
        }

        // --- 5. Deserializzazione Manuale (Request) ---
        weather_request_t req;
        int offset = 0;

        // Controlliamo di aver ricevuto almeno il tipo
        if (bytes_read < sizeof(char)) {
             // Pacchetto troppo piccolo, ignora o gestisci errore
             continue;
        }

        // Type
        memcpy(&req.type, recv_buffer + offset, sizeof(char));
        offset += sizeof(char);

        // City
        // Copiamo il resto del buffer in city.
        // Nota: Dobbiamo stare attenti a non leggere oltre bytes_read
        int city_data_len = bytes_read - offset;
        if (city_data_len > CITY_LEN) city_data_len = CITY_LEN;

        memset(req.city, 0, CITY_LEN); // Pulisci buffer destinazione
        memcpy(req.city, recv_buffer + offset, city_data_len);

        // Assicuriamo terminazione, anche se dovrebbe esserci
        req.city[CITY_LEN - 1] = '\0';

        // Stampa Log richiesto
        printf("Richiesta ricevuta da %s (ip %s): type='%c', city='%s'\n",
               client_host, client_ip, req.type, req.city);

        // --- 6. Logica Business ---
        weather_response_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.type = req.type;

        // Validazione caratteri speciali e tabulazioni nella città (come da traccia)
        int valid_syntax = 1;
        for(int k=0; k<strlen(req.city); k++) {
            char c = req.city[k];
            if (c == '\t' || strchr("@#$%^&*()=<>[]{}\\|;:", c) != NULL) {
                valid_syntax = 0;
                break;
            }
        }

        if (!valid_syntax) {
            resp.status = 2; // Richiesta invalida
        } else if (!is_city_valid(req.city)) {
            resp.status = 1; // Città non trovata
        } else {
            if (strchr("thwp", req.type) == NULL) {
                resp.status = 2; // Tipo invalido
            } else {
                resp.status = 0; // Successo
                if (req.type == 't') resp.value = get_temperature();
                else if (req.type == 'h') resp.value = get_humidity();
                else if (req.type == 'w') resp.value = get_wind();
                else if (req.type == 'p') resp.value = get_pressure();
            }
        }

        // --- 7. Serializzazione Manuale (Response) ---
        // Buffer: status(4) + type(1) + value(4)
        char send_buffer[sizeof(uint32_t) + sizeof(char) + sizeof(float)];
        offset = 0;

        // Status (host -> net)
        uint32_t net_status = htonl(resp.status);
        memcpy(send_buffer + offset, &net_status, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        // Type
        memcpy(send_buffer + offset, &resp.type, sizeof(char));
        offset += sizeof(char);

        // Value (float -> uint32 -> net)
        uint32_t net_value_tmp;
        memcpy(&net_value_tmp, &resp.value, sizeof(float));
        net_value_tmp = htonl(net_value_tmp);
        memcpy(send_buffer + offset, &net_value_tmp, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        // --- 8. Invio Risposta ---
        sendto(server_socket, send_buffer, offset, 0,
               (struct sockaddr*)&client_addr, client_len);
    }

    close(server_socket);
    #if defined _WIN32
         WSACleanup();
    #endif
    return 0;
}
