#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include<signal.h>
#define PORT 9001
#define XML_FILE "logs.xml"
#define MAX_CLIENTS 50
#define SERVER_LOG "server_errors.log"
#include <sys/stat.h>  // ← Adaugă la începutul fișierului dacă nu există

#define MAX_LOG_SIZE (10 * 1024 * 1024)  // 10 MB
// ← Adaugă după #define XML_FILE "logs.xml"

// Funcție pentru escapare caractere XML
void xml_escape(char *output, const char *input, size_t max_len) {
    size_t j = 0;
    for (size_t i = 0; input[i] && j < max_len - 6; i++) {
        switch (input[i]) {
            case '<':  
                if (j + 4 < max_len) { strcpy(&output[j], "&lt;"); j += 4; }
                break;
            case '>':  
                if (j + 4 < max_len) { strcpy(&output[j], "&gt;"); j += 4; }
                break;
            case '&':  
                if (j + 5 < max_len) { strcpy(&output[j], "&amp;"); j += 5; }
                break;
            case '"':  
                if (j + 6 < max_len) { strcpy(&output[j], "&quot;"); j += 6; }
                break;
            case '\'': 
                if (j + 6 < max_len) { strcpy(&output[j], "&apos;"); j += 6; }
                break;
            default:   
                output[j++] = input[i];
        }
    }
    output[j] = '\0';
}

pthread_mutex_t file_lock; // protejeaza scrierea în fisierul comun

// Închide corect fișierul XML
void close_xml_file() {
    pthread_mutex_lock(&file_lock);
    FILE *f = fopen(XML_FILE, "a");
    if (f) {
        fprintf(f, "</logs>\n");
        fclose(f);
        printf("\n[INFO] Fișierul XML a fost închis corect.\n");
    }
    pthread_mutex_unlock(&file_lock);
}


// Rotație loguri când fișierul devine prea mare
void rotate_logs_if_needed() {
    struct stat st;
    if (stat(XML_FILE, &st) == 0 && st.st_size > MAX_LOG_SIZE) {
        pthread_mutex_lock(&file_lock);
        
        // Închide XML-ul curent
        FILE *f = fopen(XML_FILE, "a");
        if (f) {
            fprintf(f, "</logs>\n");
            fclose(f);
        }
        
        // Generează nume cu timestamp pentru backup
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char backup_name[128];
        strftime(backup_name, sizeof(backup_name), "logs_%Y%m%d_%H%M%S.xml", t);
        
        // Mută fișierul vechi
        if (rename(XML_FILE, backup_name) == 0) {
            printf("[INFO] Loguri rotite: %s (%.2f MB)\n", 
                   backup_name, st.st_size / (1024.0 * 1024.0));
        }
        
        // Creează XML nou
        f = fopen(XML_FILE, "w");
        if (f) {
            fprintf(f, "<logs>\n");
            fclose(f);
        }
        
        pthread_mutex_unlock(&file_lock);
    }
}// ← Adaugă la începutul fișierului, lângă celelalte include-uri





// Loghează erorile serverului
void log_server_event(const char *level, const char *message) {
    FILE *f = fopen(SERVER_LOG, "a");
    if (f) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);
        
        fprintf(f, "[%s] [%s] %s\n", timestamp, level, message);
        fclose(f);
    }
}
// Handler pentru închidere curată
void handle_shutdown(int sig) {
    printf("\n[INFO] Semnal %d primit. Se închide serverul...\n", sig);
    close_xml_file();
    pthread_mutex_destroy(&file_lock);
    exit(0);
}
void write_log_to_xml(const char *log_msg) {


    static int call_count = 0;
    if (++call_count % 100 == 0) {
        rotate_logs_if_needed();
    }

    pthread_mutex_lock(&file_lock);
    FILE *f = fopen(XML_FILE, "a");
    if (!f) {
        perror("Eroare la deschiderea fisierului XML");
        pthread_mutex_unlock(&file_lock);
        return;
    }

    // Extrage componentele din logul primit
    char timestamp[64], app[64], level[32], message[512];
    
    // Formatul logului este: [timestamp] [AppX] [LEVEL]: message
    if (sscanf(log_msg, "[%63[^]]] [%63[^]]] [%31[^]]]: %511[^\n]",
               timestamp, app, level, message) == 4) {
        
        // Buffer-e pentru versiuni escapate
        char esc_timestamp[256], esc_app[256], esc_level[128], esc_message[2048];
        
        xml_escape(esc_timestamp, timestamp, sizeof(esc_timestamp));
        xml_escape(esc_app, app, sizeof(esc_app));
        xml_escape(esc_level, level, sizeof(esc_level));
        xml_escape(esc_message, message, sizeof(esc_message));
        
        fprintf(f,
            "    <log>\n"
            "        <timestamp>%s</timestamp>\n"
            "        <app>%s</app>\n"
            "        <level>%s</level>\n"
            "        <message>%s</message>\n"
            "    </log>\n",
            esc_timestamp, esc_app, esc_level, esc_message);
    } else {
        // fallback dacă formatul nu e recunoscut
        char esc_log[2048];
        xml_escape(esc_log, log_msg, sizeof(esc_log));
        fprintf(f, "    <log raw=\"true\">%s</log>\n", esc_log);
    }

    fclose(f);
    pthread_mutex_unlock(&file_lock);
}
void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);

    // debug: ID thread + timestamp
    printf("[INFO] Thread ID %lu pornit pentru un client nou.\n", pthread_self());
    log_server_event("INFO", "Client nou conectat");

    char buffer[1024];
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int valread = read(client_socket, buffer, sizeof(buffer) - 1);
        if (valread <= 0)
            break;

        printf("Log primit: %s", buffer);
        write_log_to_xml(buffer);
    }

    printf("[INFO] Thread %lu – Client deconectat.\n", pthread_self());
    log_server_event("INFO", "Client deconectat");
    close(client_socket);
    return NULL;
}

int main() {
    int server_fd, *new_sock;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    pthread_mutex_init(&file_lock, NULL);

    // Înregistrează handler pentru SIGINT (CTRL+C) și SIGTERM
    signal(SIGINT, handle_shutdown);
    signal(SIGTERM, handle_shutdown);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Eroare la crearea socketului");

        log_server_event("ERROR", "Eroare la crearea socketului");
        exit(EXIT_FAILURE);
    }



    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Eroare la setsockopt");
      log_server_event("ERROR", "Eroare la setsockopt"); 
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Eroare la bind");
        log_server_event("ERROR", "Eroare la bind"); 
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Eroare la listen");
        log_server_event("ERROR", "Eroare la listen"); 
        exit(EXIT_FAILURE);
    }

    printf(" Server (multithreaded) pornit pe portul %d...\n", PORT);

    // Initializare fisier XML
   // Inițializare fișier XML (dacă nu există sau e gol)
FILE *f = fopen(XML_FILE, "r");
if (!f) {
    // Fișierul nu există, îl creăm
    f = fopen(XML_FILE, "w");
    if (f) {
        fprintf(f, "<logs>\n");
        fclose(f);
        printf("[INFO] Fișier XML creat: %s\n", XML_FILE);
    }
} else {
    // Verificăm dacă fișierul e gol sau incomplet
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    
    if (size == 0) {
        f = fopen(XML_FILE, "w");
        if (f) {
            fprintf(f, "<logs>\n");
            fclose(f);
        }
    }
}

    // bucla principală – accepta clienti
    while (1) {
        int client_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (client_socket < 0) {
            perror("Eroare la accept");
            continue;
        }

        printf("Client conectat.\n");

        new_sock = malloc(sizeof(int));
        *new_sock = client_socket;

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, new_sock) != 0) {
            perror("Eroare la crearea thread-ului");
            close(client_socket);
            free(new_sock);
            continue;
        }

        pthread_detach(tid);
    }

    pthread_mutex_destroy(&file_lock);
    close(server_fd);
    return 0;
}

// În server, la final sau la SIGINT:
