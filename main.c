#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h> // open() işlevi için gerekli
#include <signal.h> // Sinyal işleme için gerekli
#include <increment.h>

// Eğer SA_RESTART veya SA_NOCLDSTOP tanımlı değilse, tanımlayın
#ifndef SA_RESTART
#define SA_RESTART 0x10000000
#endif

#ifndef SA_NOCLDSTOP
#define SA_NOCLDSTOP 0x00000001
#endif

// Arka planda çalışan süreçlerin durumunu yakalamak için sinyal işleyici
void handle_background_processes() {
    int status;
    pid_t pid;

    // Tamamlanan arka plan süreçlerini kontrol et
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // Sürecin PID'si ve dönüş değerini yazdır
        printf("[%d] retval: %d\n", pid, WEXITSTATUS(status));
        fflush(stdout); // Çıktıyı hemen yazdır
    }
}

int main() {
    // SIGCHLD sinyalini yakalamak için bir handler tanımlıyoruz
    struct sigaction sa;
    sa.sa_handler = handle_background_processes; // Sinyal işleyiciyi ayarla
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; // İşleyici seçeneklerini ayarla
    sigaction(SIGCHLD, &sa, NULL); // SIGCHLD sinyalini yakala

    while (1) {
        // Kullanıcıdan komut almak için prompt yazdır
        printf("> ");
        fflush(stdout);

        char command[256];
        fgets(command, sizeof(command), stdin); // Kullanıcıdan giriş al
        command[strcspn(command, "\n")] = 0; // Sonundaki yeni satır karakterini kaldır

        // Kullanıcı "quit" komutunu girerse kabuktan çık
        if (strcmp(command, "quit") == 0) {
            // Tüm arka plan süreçlerini bekle
            while (waitpid(-1, NULL, 0) > 0);
            break; // Döngüden çık ve programı sonlandır
        }

        // Arka plan işlemi kontrolü
        int background = 0;
        if (command[strlen(command) - 1] == '&') {
            background = 1; // Eğer komut '&' ile bitiyorsa arka planda çalıştırılacak
            command[strlen(command) - 1] = '\0'; // '&' karakterini kaldır
        }

        // Pipe (|) operatörü kontrolü
        if (strchr(command, '|')) {
            char *commands[10]; // Birden fazla komut için dizi
            int i = 0;
            char *token = strtok(command, "|"); // Komutları '|' ile ayır

            while (token != NULL) {
                commands[i++] = token;
                token = strtok(NULL, "|");
            }

            int num_pipes = i - 1; // Pipe sayısı (komut sayısı - 1)
            int pipefds[2 * num_pipes]; // Pipe'lar için file descriptor dizisi

            // Gerekli pipe'ları oluştur
            for (int j = 0; j < num_pipes; j++) {
                if (pipe(pipefds + j * 2) < 0) {
                    perror("pipe error"); // Hata mesajı
                    exit(1);
                }
            }

            int pid;
            for (int j = 0; j <= num_pipes; j++) {
                pid = fork(); // Yeni süreç oluştur

                if (pid == 0) { // Child process
                    // Eğer bu pipe'ın girişiyse stdin'i ayarla
                    if (j != 0) {
                        dup2(pipefds[(j - 1) * 2], STDIN_FILENO);
                    }

                    // Eğer bu pipe'ın çıkışıysa stdout'u ayarla
                    if (j != num_pipes) {
                        dup2(pipefds[j * 2 + 1], STDOUT_FILENO);
                    }

                    // Tüm pipe'ları kapat
                    for (int k = 0; k < 2 * num_pipes; k++) {
                        close(pipefds[k]);
                    }

                    // Komut argümanlarını ayır
                    char *args[10];
                    char *arg_token = strtok(commands[j], " ");
                    int arg_idx = 0;

                    while (arg_token != NULL) {
                        args[arg_idx++] = arg_token;
                        arg_token = strtok(NULL, " ");
                    }
                    args[arg_idx] = NULL;

                    char program_path[512];
                    if (access(args[0], F_OK) == 0) {
                        snprintf(program_path, sizeof(program_path), "./%s", args[0]);
                        execvp(program_path, args);
                    } else {
                        execvp(args[0], args);
                    }

                    perror("execvp failed"); // Hata durumunda mesaj yazdır
                    exit(1);
                }
            }

            // Parent process: Pipe'ları kapat
            for (int j = 0; j < 2 * num_pipes; j++) {
                close(pipefds[j]);
            }

            // Tüm süreçlerin bitmesini bekle
            for (int j = 0; j <= num_pipes; j++) {
                wait(NULL);
            }

            continue;
        }

        // Eğer komut giriş yönlendirmesi içeriyorsa (<)
        if (strchr(command, '<')) {
            char *args[10]; // Komut argümanları için dizi
            char *token = strtok(command, " ");
            int i = 0;
            int input_redirection = 0;
            char *input_file = NULL;

            while (token != NULL) {
                if (strcmp(token, "<") == 0) {
                    input_redirection = 1;
                    token = strtok(NULL, " ");
                    input_file = token; // Giriş dosyasını al
                } else {
                    args[i++] = token; // Komut argümanlarını diziye ekle
                }
                token = strtok(NULL, " ");
            }
            args[i] = NULL;

            pid_t pid = fork(); // Yeni süreç oluştur
            if (pid == 0) {
                if (input_redirection && input_file) {
                    int fd = open(input_file, O_RDONLY); // Dosyayı oku
                    if (fd < 0) { // Eğer dosya açılamazsa hata yazdır
                        fprintf(stderr, "Giriş dosyası bulunamadı: %s\n", input_file);
                        fflush(stderr);
                        exit(1);
                    }
                    dup2(fd, STDIN_FILENO); // Standart girdiyi yönlendir
                    close(fd);
                }

                char program_path[512];
                if (access(args[0], F_OK) == 0) {
                    snprintf(program_path, sizeof(program_path), "./%s", args[0]);
                    execvp(program_path, args);
                } else {
                    execvp(args[0], args);
                }

                perror("execvp failed");
                exit(1);
            } else if (pid > 0) {
                if (!background) {
                    waitpid(pid, NULL, 0); // Foreground ise bekle
                } else {
                    printf("[%d] running in background\n", pid);
                    fflush(stdout);
                }
            }
            continue;
        }

        // Eğer komut çıkış yönlendirmesi içeriyorsa (>)
        if (strchr(command, '>')) {
            char *args[10];
            char *token = strtok(command, " ");
            int i = 0;
            int output_redirection = 0;
            char *output_file = NULL;

            while (token != NULL) {
                if (strcmp(token, ">") == 0) {
                    output_redirection = 1;
                    token = strtok(NULL, " ");
                    if (token != NULL) {
                        output_file = token; // Çıkış dosyasını al
                    }
                } else {
                    args[i++] = token; // Argümanları diziye ekle
                }
                token = strtok(NULL, " ");
            }
            args[i] = NULL;

            pid_t pid = fork(); // Yeni süreç oluştur
            if (pid == 0) {
                if (output_redirection && output_file) {
                    int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644); // Dosyayı yazma modunda aç
                    if (fd < 0) { // Dosya açılamazsa hata yazdır
                        fprintf(stderr, "Çıkış dosyası oluşturulamadı: %s\n", output_file);
                        fflush(stderr);
                        exit(1);
                    }
                    dup2(fd, STDOUT_FILENO); // Standart çıktıyı yönlendir
                    close(fd);
                }

                char program_path[512];
                if (access(args[0], F_OK) == 0) {
                    snprintf(program_path, sizeof(program_path), "./%s", args[0]);
                    execvp(program_path, args);
                } else {
                    execvp(args[0], args);
                }

                perror("execvp failed");
                exit(1);
            } else if (pid > 0) {
                if (!background) {
                    waitpid(pid, NULL, 0); // Foreground ise bekle
                } else {
                    printf("[%d] running in background\n", pid);
                    fflush(stdout);
                }
            }
            continue;
        }

        // Komutu argümanlara ayır ve çalıştır
        char *args[10];
        char *token = strtok(command, " ");
        int i = 0;
        while (token != NULL) {
            args[i++] = token;
            token = strtok(NULL, " ");
        }
        args[i] = NULL;

        pid_t pid = fork(); // Yeni süreç oluştur
        if (pid == 0) {
            char program_path[512];
            if (access(args[0], F_OK) == 0) {
                snprintf(program_path, sizeof(program_path), "./%s", args[0]);
                execvp(program_path, args);
            } else {
                execvp(args[0], args);
            }

            perror("execvp failed"); // Hata durumunda mesaj yazdır
            exit(1);
        } else if (pid > 0) {
            if (!background) {
                waitpid(pid, NULL, 0); // Foreground işlem tamamlanana kadar bekle
            } else {
                printf("[%d] running in background\n", pid);
                fflush(stdout);
            }
        } else {
            perror("fork failed"); // fork başarısız olursa hata yazdır
        }
    }

    printf("Shell exited.\n"); // Kabuktan çıkış mesajı
    return 0;
}