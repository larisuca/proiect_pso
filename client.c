#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/stat.h>

#define PORT 9001
#define SERVER_IP "172.20.10.2"   // <<-- pune IP-ul serverului tău
#define MAX_LOG_SIZE 256
#define MAX_MESSAGES 20              // <-- trimite doar 20 de loguri

// Niveluri posibile
const char *levels[] = {"INFO", "WARN", "ERROR"};

// Funcție care generează un log automat în funcție de aplicație
void generate_log(char *buffer, size_t size, const char *app_name) {
    const char **msg_list;
    int msg_count;

    // Definim mesaje specifice pentru fiecare aplicație
    if (strcmp(app_name, "App1") == 0) {
        static const char *ui_msgs[] = {
            "User login successful",
            "UI button clicked",
            "File saved successfully",
            "Unauthorized access attempt",
            "Disk space low"
        };
        msg_list = ui_msgs;
        msg_count = sizeof(ui_msgs) / sizeof(ui_msgs[0]);
    } 
    else if (strcmp(app_name, "App2") == 0) {
        static const char *db_msgs[] = {
            "Database query executed",
            "Connection reset by peer",
            "Memory usage high",
            "Disk space low",
            "API request timeout"
        };
        msg_list = db_msgs;
        msg_count = sizeof(db_msgs) / sizeof(db_msgs[0]);
    } 
    else {
        static const char *sensor_msgs[] = {
            "Sensor value out of range",
            "Memory usage high",
            "API request timeout",
            "Unauthorized access attempt",
            "Disk space low"
        };
        msg_list = sensor_msgs;
        msg_count = sizeof(sensor_msgs) / sizeof(sensor_msgs[0]);
    }

    // Selectăm nivelul și mesajul random
    const char *level = levels[rand() % (sizeof(levels) / sizeof(levels[0]))];
    const char *msg = msg_list[rand() % msg_count];

    // Timestamp
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

    snprintf(buffer, size, "[%s] [%s] [%s]: %s\n", timestamp, app_name, level, msg);
}

// Scrie logul și local în folderul logs/
void write_local_log(const char *app_name, const char *log_msg) {
    mkdir("logs", 0777); // dacă nu există, îl creează

    char path[64];
    snprintf(path, sizeof(path), "logs/%s.log", app_name);

    FILE *f = fopen(path, "a");
    if (f) {
        fprintf(f, "%s", log_msg);
        fclose(f);
    } else {
        perror("Eroare la scrierea logului local");
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Utilizare: %s <AppName>\n", argv[0]);
        return -1;
    }

    const char *app_name = argv[1];
    srand(time(NULL) ^ getpid()); // seed unic pentru fiecare instanță

    int sock = 0;
    struct sockaddr_in serv_addr;
    char log_msg[MAX_LOG_SIZE];

    // Creare socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Eroare la crearea socketului");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Conversie IP
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("Adresa IP invalidă");
        return -1;
    }

    // Conectare la server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Conectare eșuată la server");
        return -1;
    }

    printf("[%s] Conectat la server (%s:%d)\n", app_name, SERVER_IP, PORT);

    // Trimite un număr limitat de loguri
    for (int i = 0; i < MAX_MESSAGES; i++) {
        generate_log(log_msg, sizeof(log_msg), app_name);
        write_local_log(app_name, log_msg);

        if (send(sock, log_msg, strlen(log_msg), 0) < 0) {
            perror("Eroare la trimiterea logului");
            break;
        }

        printf("[%s] Trimis (%d/%d): %s", app_name, i + 1, MAX_MESSAGES, log_msg);
        sleep(1 + rand() % 3); // interval aleator 1-3 secunde
    }

    close(sock);
    printf("\n[%s] Conexiune închisă. Simulare terminată.\n", app_name);
    return 0;
}