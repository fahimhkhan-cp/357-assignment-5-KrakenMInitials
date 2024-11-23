#define _GNU_SOURCE
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

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

   printf("Parsed: type=%s, fn=%s, ver=%s\n", type, fn, ver);

   if (strtok(NULL, " "))
      return 0;

   if (strcmp(type, "GET") != 0 && strcmp(type, "HEAD") != 0)
      return 0;

   if (strstr(fn, ".."))
      return 0;

   return 1;
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
      printf("Test: Received line: %s\n", line);

      if (!parseLine(line, type, fn, ver))
      {
         fprintf(network, "HTTP/1.0 400 Bad Request\r\n\r\n");
         fflush(network);
         free(line);
         fclose(network);
         return;
      }

      printf("test:: Parsed values: type='%s', fn='%s', ver='%s'\n", type, fn, ver);

      char filepath[512];
      snprintf(filepath, sizeof(filepath), "%s", fn[0] == '/' ? fn + 1 : fn); 
      printf("filepath: %s\n", filepath);
      struct stat st;

      if (stat(filepath, &st) == -1 || !S_ISREG(st.st_mode))
      {
         fprintf(network, "HTTP/1.0 404 Not Found\r\nContent-Type: text/html\r\n\r\n<html><body>File not found</body></html>");
      }
      else
      {
         fprintf(network, "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n", st.st_size);
         fflush(network);

         if (strcmp(type, "GET") == 0)
         {

            FILE *file = fopen(filepath, "r");
            if (file)
            {
               char buffer[1024];
               size_t bytes;
               while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0)
               {
                  printf("test:: Read %zu bytes from file\n", bytes); 
                  size_t written = fwrite(buffer, 1, bytes, network);
                  fflush(network);                                        
                  printf("test:: Wrote %zu bytes to network\n", written); 

                  if (written != bytes)
                  {
                     fprintf(stderr, "Failed to write all bytes to network\n");
                     break;
                  }
               }
               fclose(file);
            }
            if (!file)
            {
               perror("Error opening file");
               fprintf(network, "HTTP/1.0 500 Internal Server Error\r\nContent-Type: text/html\r\n\r\n<html><body>Unable to open file</body></html>");
               fclose(network);
               return;
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
      perror("Invalid port\n");
      exit(1);
   }

   int fd = create_service(port);
   if (fd == -1)
   {
      perror("create_service failed");
      exit(1);
   }
   printf("Socket created successfully, fd: %d\n", fd);

   printf("listening on port: %d\n", port);
   run_service(fd);

   close(fd);

   return 0;
}
