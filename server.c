#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define LISTEN_BACKLOG 10

int main(int argc, char *argv[])
{
    int sockfd, connfd;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));

    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = INADDR_ANY;
    serv.sin_port = htons(8080);

    if (bind(sockfd, (struct sockaddr *)&serv, sizeof(serv)) == -1) {
        perror("bind");
        return 1;
    }

    if (listen(sockfd, LISTEN_BACKLOG) == -1) {
        perror("listen");
        return 1;
    }

    struct sockaddr_in client;
    for (;;) {
    socklen_t len = sizeof(client);

    connfd = accept(sockfd, (struct sockaddr *)&client, &len);
    if (connfd == -1) {
        perror("accept");
        continue;
    }

    char buff[1024];
    ssize_t n;

    while ((n = recv(connfd, buff, sizeof(buff), 0)) > 0) {
      if (n > 0 && buff[n - 1] == '\n') {
    n--;
}
  
      const char suffix[] = " Received";
        size_t m = sizeof(suffix) - 1;   // exclude '\0'

        char out[n + m+1];

        memcpy(out, buff, n);
        memcpy(out + n, suffix, m);
        out[n + m] = '\0';

        printf("String: %s\n", out);
        send(connfd, out, n + m, 0);
        

    }

    if (n == 0)
        printf("client disconnected\n");
    else
        perror("recv");

    close(connfd);
}

  close(sockfd); 
    return 0;
   
}
