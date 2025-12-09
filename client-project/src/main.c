#include "protocol.h"

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);

    char server_host[256] = "localhost"; // Default host
    int port = DEFAULT_PORT;
    weather_request_t req;
    memset(&req, 0, sizeof(req));
    int request_provided = 0;

    // --- 1. Parsing Argomenti ---
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            strncpy(server_host, argv[i + 1], 255);
            server_host[255] = '\0';
            i++;
        } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            char* input_str = argv[i + 1];

            // Logica di parsing richiesta come da specifiche
            char* space_ptr = strchr(input_str, ' ');

            if (space_ptr == NULL) {
                // Nessuno spazio trovato, formato errato
                printf("Errore: formato richiesta invalido. Usa \"type city\"\n");
                return -1;
            }

            // Calcolo lunghezza primo token
            int type_len = space_ptr - input_str;
            if (type_len != 1) {
                printf("Errore: il tipo deve essere un singolo carattere.\n");
                return -1;
            }

            req.type = input_str[0];

            // Il resto è la città (dopo lo spazio)
            char* city_ptr = space_ptr + 1;

            // Validazione lunghezza città
            if (strlen(city_ptr) >= CITY_LEN) { // >= 64 include il terminatore
                printf("Errore: nome città troppo lungo (max 63 caratteri).\n");
                return -1;
            }

            strncpy(req.city, city_ptr, CITY_LEN - 1);
            req.city[CITY_LEN - 1] = '\0';

            request_provided = 1;
            i++;
        }
    }

    if (!request_provided) {
        printf("Uso: %s [-s server] [-p port] -r \"type city\"\n", argv[0]);
        return -1;
    }

    #if defined _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) return -1;
    #endif

    // --- 2. Risoluzione DNS (Host -> IP) ---
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;      // IPv4
    hints.ai_socktype = SOCK_DGRAM; // UDP

    char port_str[10];
    sprintf(port_str, "%d", port);

    if (getaddrinfo(server_host, port_str, &hints, &res) != 0) {
        printf("Errore risoluzione DNS per %s\n", server_host);
        #if defined _WIN32
            WSACleanup();
        #endif
        return -1;
    }

    // --- 3. Preparazione Output (Reverse Lookup IP -> Hostname) ---
    // La traccia richiede di stampare l'IP risolto e il nome host risolto dall'IP.
    char resolved_ip[INET_ADDRSTRLEN];
    char resolved_name[NI_MAXHOST];

    struct sockaddr_in* saddr = (struct sockaddr_in*)res->ai_addr;
    inet_ntop(AF_INET, &(saddr->sin_addr), resolved_ip, INET_ADDRSTRLEN);

    // Reverse lookup per ottenere il nome host canonico dall'IP
    if (getnameinfo((struct sockaddr*)saddr, sizeof(struct sockaddr_in),
                    resolved_name, sizeof(resolved_name),
                    NULL, 0, 0) != 0) {
        // Se fallisce il reverse lookup, usiamo l'IP come nome
        strcpy(resolved_name, resolved_ip);
    }

    // --- 4. Creazione Socket UDP ---
    int client_socket = socket(PF_INET, SOCK_DGRAM, 0);
    if (client_socket < 0) {
        perror("Socket error");
        freeaddrinfo(res);
        return -1;
    }

    // --- 5. Serializzazione Manuale (Request) ---
    // Buffer size: 1 byte (type) + 64 bytes (city)
    char send_buffer[sizeof(char) + CITY_LEN];
    int offset = 0;

    // Type
    memcpy(send_buffer + offset, &req.type, sizeof(char));
    offset += sizeof(char);

    // City (copiamo tutto il buffer statico per sicurezza o solo stringa?
    // La traccia dice "strutture dati... invariate", e city è char[64].
    // Copiamo l'intero array fixed size per evitare problemi di spazzatura o padding esterni).
    memcpy(send_buffer + offset, req.city, CITY_LEN);
    offset += CITY_LEN;

    // --- 6. Invio (Sendto) ---
    ssize_t sent_bytes = sendto(client_socket, send_buffer, offset, 0,
                                res->ai_addr, res->ai_addrlen);

    if (sent_bytes != offset) {
        printf("Errore invio.\n");
        close(client_socket);
        freeaddrinfo(res);
        return -1;
    }

    // --- 7. Ricezione (Recvfrom) ---
    char recv_buffer[512]; // Buffer abbastanza grande per la risposta
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    ssize_t received_bytes = recvfrom(client_socket, recv_buffer, sizeof(recv_buffer), 0,
                                      (struct sockaddr*)&from_addr, &from_len);

    if (received_bytes <= 0) {
        printf("Errore ricezione o timeout.\n");
        close(client_socket);
        freeaddrinfo(res);
        return -1;
    }

    // --- 8. Deserializzazione Manuale (Response) ---
    weather_response_t resp;
    offset = 0;

    // Status (uint32_t -> ntohl)
    uint32_t net_status;
    if (received_bytes < sizeof(uint32_t)) { printf("Risposta corrotta.\n"); return -1; }
    memcpy(&net_status, recv_buffer + offset, sizeof(uint32_t));
    resp.status = ntohl(net_status);
    offset += sizeof(uint32_t);

    // Type (char)
    if (received_bytes < offset + sizeof(char)) { printf("Risposta corrotta.\n"); return -1; }
    memcpy(&resp.type, recv_buffer + offset, sizeof(char));
    offset += sizeof(char);

    // Value (float -> uint32 -> ntohl -> float)
    if (received_bytes < offset + sizeof(float)) { printf("Risposta corrotta.\n"); return -1; }
    uint32_t net_value;
    memcpy(&net_value, recv_buffer + offset, sizeof(uint32_t));
    net_value = ntohl(net_value);
    memcpy(&resp.value, &net_value, sizeof(float));

    // --- 9. Output Formattato ---
    // Formato: "Ricevuto risultato dal server <nomeserver> (ip <IP>). <Messaggio>"

    printf("Ricevuto risultato dal server %s (ip %s). ", resolved_name, resolved_ip);

    if (resp.status == 1) {
        printf("Città non disponibile\n");
    } else if (resp.status == 2) {
        printf("Richiesta non valida\n");
    } else if (resp.status == 0) {
        // La traccia richiede "NomeCittà: Tipo = Valore"
        // Nota: Il server non ci rimanda il nome della città, usiamo quello della richiesta (req.city)
        // Dobbiamo assicurarci che la prima lettera sia maiuscola per l'output, come nell'esempio "Bari"
        // Anche se l'utente ha scritto "bari".
        char formatted_city[CITY_LEN];
        strcpy(formatted_city, req.city);
        if (formatted_city[0] >= 'a' && formatted_city[0] <= 'z') {
            formatted_city[0] = formatted_city[0] - ('a' - 'A');
        }

        printf("%s: ", formatted_city);

        switch (resp.type) {
            case 't': printf("Temperatura = %.1f°C\n", resp.value); break;
            case 'h': printf("Umidità = %.1f%%\n", resp.value); break;
            case 'w': printf("Vento = %.1f km/h\n", resp.value); break;
            case 'p': printf("Pressione = %.1f hPa\n", resp.value); break;
            default:  printf("Dato sconosciuto = %.1f\n", resp.value);
        }
    } else {
        printf("Status sconosciuto (%d).\n", resp.status);
    }

    freeaddrinfo(res);
    close(client_socket);
    #if defined _WIN32
        WSACleanup();
    #endif
    return 0;
}
