#define _GNU_SOURCE
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

void validateHTTPformat(char* line){
   
}

void handle_request(int nfd)
{
   FILE *network = fdopen(nfd, "r");
   char *line = NULL;
   size_t size;
   ssize_t num;

   if (network == NULL)
   {
      perror("fdopen");
      close(nfd);
      return;
   }

   while ((num = getline(&line, &size, network)) >= 0)
   {
      printf("Message received from client: %s", line);
      // MODIFICATION: Use write() to send the data back to the client
      if (write(nfd, line, num) == -1)
      // MODIFICATION: (Optional) Standard error check
      {
         perror("write");
         break;
      }
   }

   free(line);
   fclose(network);
}

void zombie_handler(int i){
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
         if (pid == 0) //child process
         {
            close(fd);
            printf("Connection established\n");
            handle_request(nfd);
            printf("Connection closed\n");
            close(nfd);
            exit(1);
         }
         close(nfd);
      }
   }
}

int main(int argc, char *argv[])
{
   if (argc != 2)
   {
      perror("Invalid number of arguments\n");
      exit(1);
   }

   // verify port
   int port = atoi(argv[1]);
   if (port < 1024 || port > 65535)
   {
      perror("Invalid port\n");
      exit(1);
   }

   int fd = create_service(port);
   if (fd == -1)
   {
      perror("creat_service failed:\n");
      exit(1);
   }

   printf("listening on port: %d\n", port);
   run_service(fd);
   close(fd);

   return 0;
}
