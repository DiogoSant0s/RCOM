#include "../include/download.h"

// Function to parse the input URL and populate the URL structure
int parse(char *input, struct URL *url) {
    regex_t regex;

    // Compile and execute regular expression for checking '/'
    regcomp(&regex, BAR, 0);
    if (regexec(&regex, input, 0, NULL, 0)) return -1;

    // Check if the input URL follows the format ftp://<host>/<url-path>
    regcomp(&regex, AT, 0);
    if (regexec(&regex, input, 0, NULL, 0) != 0) { // ftp://<host>/<url-path>

        sscanf(input, HOST_REGEX, url->host);
        strcpy(url->user, DEFAULT_USER);
        strcpy(url->password, DEFAULT_PASSWORD);

    } else { // ftp://[<user>:<password>@]<host>/<url-path>
    
        sscanf(input, HOST_AT_REGEX, url->host);
        sscanf(input, USER_REGEX, url->user);
        sscanf(input, PASS_REGEX, url->password);
    }

    sscanf(input, RESOURCE_REGEX, url->resource);
    strcpy(url->file, strrchr(input, '/') + 1);

    // Resolve the IP address from the hostname
    struct hostent *h;
    if (strlen(url->host) == 0) return -1;

    if ((h = gethostbyname(url->host)) == NULL) {
        printf("Invalid hostname '%s'\n", url->host);
        exit(-1);
    }
    strcpy(url->ip, inet_ntoa(*((struct in_addr *) h->h_addr)));

    // Return 0 if parsing and validation are successful
    return !(strlen(url->host) && strlen(url->user) && strlen(url->password) && strlen(url->resource) && strlen(url->file));
}

// Function to create a socket and connect to the server
int createSocket(char *ip, int port) {
    int sockfd;
    struct sockaddr_in server_addr;

    // Initialize server address structure
    bzero((char *) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        exit(-1);
    }

    // Connect to the server
    if (connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("connect()");
        exit(-1);
    }

    return sockfd;
}

// Function to authenticate the connection with the FTP server
int authConn(const int socket, const char* user, const char* pass) {
    char userCommand[5 + strlen(user) + 1];
    sprintf(userCommand, "user %s\n", user);

    char passCommand[5 + strlen(pass) + 1];
    sprintf(passCommand, "pass %s\n", pass);

    char answer[MAX_LENGTH];

    // Send user command and check the response
    write(socket, userCommand, strlen(userCommand));
    if (readResponse(socket, answer) != SV_READY4PASS) {
        printf("Unknown user '%s'. Abort.\n", user);
        exit(-1);
    }

    // Send password command and return the response
    write(socket, passCommand, strlen(passCommand));
    return readResponse(socket, answer);
}

// Function to enter passive mode and get the data connection details
int passiveMode(const int socket, char *ip, int *port) {
    char answer[MAX_LENGTH];
    int ip1, ip2, ip3, ip4, port1, port2;

    // Send PASV command and check the response
    write(socket, "pasv\n", 5);
    if (readResponse(socket, answer) != SV_PASSIVE) return -1;

    // Extract IP and port information from the PASV response
    sscanf(answer, PASSIVE_REGEX, &ip1, &ip2, &ip3, &ip4, &port1, &port2);
    *port = port1 * 256 + port2;
    sprintf(ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);

    return SV_PASSIVE;
}

// Function to read server responses from the control connection
int readResponse(const int socket, char* buffer) {
    char byte;
    int index = 0, responseCode;
    ResponseState state = START;

    // Initialize buffer and read server response
    memset(buffer, 0, MAX_LENGTH);

    while (state != END) {
        read(socket, &byte, 1);
        switch (state) {
            case START:
                if (byte == ' ') state = SINGLE;
                else if (byte == '-') state = MULTIPLE;
                else if (byte == '\n') state = END;
                else buffer[index++] = byte;
                break;
            case SINGLE:
                if (byte == '\n') state = END;
                else buffer[index++] = byte;
                break;
            case MULTIPLE:
                if (byte == '\n') {
                    memset(buffer, 0, MAX_LENGTH);
                    state = START;
                    index = 0;
                }
                else buffer[index++] = byte;
                break;
            case END:
                break;
            default:
                break;
        }
    }

    // Extract response code and return it
    sscanf(buffer, RESPCODE_REGEX, &responseCode);
    return responseCode;
}

// Function to request a specific resource from the server
int requestResource(const int socket, char *resource) {
    char fileCommand[5+strlen(resource)+1], answer[MAX_LENGTH];

    // Send RETR command for the requested resource
    sprintf(fileCommand, "retr %s\n", resource);
    write(socket, fileCommand, sizeof(fileCommand));

    // Return the response code
    return readResponse(socket, answer);
}

// Function to receive the requested resource from the data connection
int getResource(const int socketA, const int socketB, char *filename) {
    FILE *fd = fopen(filename, "wb");

    // Check if file can be opened or created
    if (fd == NULL) {
        printf("Error opening or creating file '%s'\n", filename);
        exit(-1);
    }

    char buffer[MAX_LENGTH];
    int bytes;

    // Read data from the data connection and write to the file
    do {
        bytes = read(socketB, buffer, MAX_LENGTH);
        if (fwrite(buffer, bytes, 1, fd) < 0) return -1;
    } while (bytes);

    // Close the file and return the response code
    fclose(fd);

    return readResponse(socketA, buffer);
}

// Function to close both control and data connections
int closeConnection(const int socketA, const int socketB) {
    char answer[MAX_LENGTH];

    // Send QUIT command and check the response
    write(socketA, "quit\n", 5);
    
    if(readResponse(socketA, answer) != SV_GOODBYE) return -1;

    // Close both control and data connections
    return close(socketA) || close(socketB);
}

// Main function
int main(int argc, char *argv[]) {
    // Check if the correct number of arguments is provided
    if (argc != 2) {
        printf("Usage: ./download ftp://[<user>:<password>@]<host>/<url-path>\n");
        exit(-1);
    }

    // Initialize URL structure
    struct URL url;
    memset(&url, 0, sizeof(url));

    // Parse the input URL
    if (parse(argv[1], &url) != 0) {
        printf("Parse error. Usage: ./download ftp://[<user>:<password>@]<host>/<url-path>\n");
        exit(-1);
    }

    // Print parsed URL information
    printf("Host: %s\nResource: %s\nFile: %s\nUser: %s\nPassword: %s\nIP Address: %s\n",
           url.host, url.resource, url.file, url.user, url.password, url.ip);

    char answer[MAX_LENGTH];
    int socketA = createSocket(url.ip, FTP_PORT);

    // Check if control connection is successful
    if (socketA < 0 || readResponse(socketA, answer) != SV_READY4AUTH) {
        printf("Socket to '%s' and port %d failed\n", url.ip, FTP_PORT);
        exit(-1);
    }

    // Authenticate the connection
    if (authConn(socketA, url.user, url.password) != SV_LOGINSUCCESS) {
        printf("Authentication failed with username = '%s' and password = '%s'.\n", url.user, url.password);
        exit(-1);
    }

    int port;
    char ip[MAX_LENGTH];

    // Enter passive mode and get data connection details
    if (passiveMode(socketA, ip, &port) != SV_PASSIVE) {
        printf("Passive mode failed\n");
        exit(-1);
    }

    int socketB = createSocket(ip, port);

    // Check if data connection is successful
    if (socketB < 0) {
        printf("Socket to '%s:%d' failed\n", ip, port);
        exit(-1);
    }

    // Request the specified resource from the server
    if (requestResource(socketA, url.resource) != SV_READY4TRANSFER) {
        printf("Unknown resouce '%s' in '%s:%d'\n", url.resource, ip, port);
        exit(-1);
    }

    // Receive the requested resource from the data connection
    if (getResource(socketA, socketB, url.file) != SV_TRANSFER_COMPLETE) {
        printf("Error transferring file '%s' from '%s:%d'\n", url.file, ip, port);
        exit(-1);
    }

    // Close both control and data connections
    if (closeConnection(socketA, socketB) != 0) {
        printf("Sockets close error\n");
        exit(-1);
    }

    return 0;
}
