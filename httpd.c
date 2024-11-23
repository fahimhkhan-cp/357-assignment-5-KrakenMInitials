#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MAXBUF 1024

int parseLine(const char *line, char *type, char *fn, char *ver)
{
    char _line[1024];
    strncpy(_line, line, sizeof(_line) - 1);
    _line[sizeof(_line) - 1] = '\0';

    char *token = strtok(_line, " ");
    if (!token)
        return 0;
    strncpy(type, token, 10);

    token = strtok(NULL, " ");
    if (!token)
        return 0;
    strncpy(fn, token, 512);

    token = strtok(NULL, " ");
    if (!token)
        return 0;
    strncpy(ver, token, 20);

    if (strtok(NULL, " "))
        return 0;

    if (strcmp(type, "GET") != 0 && strcmp(type, "HEAD") != 0)
        return 0;

    if (strstr(fn, ".."))
        return 0;

    return 1;
}

void send_header(int client_sock, int status_code, const char *status_msg, const char *content_type, size_t content_length)
{
    char buffer[1024];
    snprintf(buffer, sizeof(buffer),
             "HTTP/1.0 %d %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n",
             status_code, status_msg, content_type, content_length);
    send(client_sock, buffer, strlen(buffer), 0);
}

void handle_request(int nfd)
{
    FILE *network = fdopen(nfd, "r+");
    if (network == NULL)
    {
        perror("fdopen");
        close(nfd);
        return;
    }

    char *line = NULL;
    size_t size = 0;
    ssize_t num;
    char type[10] = {0}, fn[512] = {0}, ver[20] = {0};

    if ((num = getline(&line, &size, network)) > 0)
    {
        if (!parseLine(line, type, fn, ver))
        {
            send_header(nfd, 400, "Bad Request", "text/html", 0);
            send(nfd, "<html><body><h1>400 Bad Request</h1></body></html>", 42, 0);
            free(line);
            fclose(network);
            return;
        }

        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s", fn[0] == '/' ? fn + 1 : fn);

        struct stat st;

        if (stat(filepath, &st) == -1 || !S_ISREG(st.st_mode))
        {
            send_header(nfd, 404, "Not Found", "text/html", 0);
            send(nfd, "<html><body><h1>404 Not Found</h1></body></html>", 43, 0);
        }
        else
        {
            send_header(nfd, 200, "OK", "text/html", st.st_size);

            if (strcmp(type, "GET") == 0)
            {
                FILE *file = fopen(filepath, "r");
                if (file)
                {
                    char buffer[MAXBUF];
                    size_t bytes;
                    while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0)
                    {
                        size_t written = send(nfd, buffer, bytes, 0);
                        if (written != bytes)
                        {
                            perror("Failed to send file data");
                            break;
                        }
                    }
                    fclose(file);
                }
                else
                {
                    perror("Error opening file");
                    send_header(nfd, 500, "Internal Server Error", "text/html", 0);
                    send(nfd, "<html><body><h1>500 Internal Server Error</h1></body></html>", 55, 0);
                }
            }
        }
    }

    free(line);
    fclose(network);
}

void zombie_handler(int i)
{
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int accept_connection(int fd)
{
    struct sockaddr_in remote_addr;
    socklen_t size = sizeof(remote_addr);
    int new_fd = accept(fd, (struct sockaddr*)&remote_addr, &size);
    return new_fd;
}

int create_service(int port)
{
    int fd;
    struct sockaddr_in local_addr;
    int yes = 1;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
    {
        perror("Socket creation failed");
        return -1;
    }

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(port);
    local_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) == -1)
    {
        perror("Bind failed");
        close(fd);
        return -1;
    }

    if (listen(fd, 10) == -1)
    {
        perror("Listen failed");
        close(fd);
        return -1;
    }

    return fd;
}

void run_service(int fd)
{
    signal(SIGCHLD, zombie_handler);

    while (1)
    {
        int nfd = accept_connection(fd);
        if (nfd != -1)
        {
            pid_t pid = fork();
            if (pid == 0)
            {
                close(fd);
                handle_request(nfd);
                close(nfd);
                exit(1);
            }
            close(nfd);
        }
    }
}

int main(int argc, char *argv[])
{
    int port = 2830;

    if (port < 1024 || port > 65535)
    {
        perror("Invalid port");
        exit(1);
    }

    int fd = create_service(port);
    if (fd == -1)
    {
        perror("Create service failed");
        exit(1);
    }

    printf("Server started on port %d\n", port);
    run_service(fd);

    close(fd);

    return 0;
}
