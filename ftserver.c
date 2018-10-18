/*******************************************************************************
** Program Filename: ftserver.c
** Author: Zachary Anderson
** Description: This program works in conjunction with ftclient.py to implement
**              file transfer over a connection between TCP sockets. This program
**              opens a TCP socket on a specified port and listens on it. When
**              it establishes a connection, it receives the request and spawns a new
**              process. The child establishes a second connection with a new socket. The
**              parent process waits for a new connection while the child
**              sends the requested data on this second socket then closes
**              its connections. Program exits when sent SIGINT.
** Citations: # Beej's guide was used as a reference for the socket programming.
**            http://beej.us/guide/bgnet/html/multi/index.html
**            # CS 344 Lecture 2.4 was used for how to deal with reading/writing
**            to/from files.
**            # https://www.tutorialspoint.com/c_standard_library/c_function_system.htm
**            was used to learn about using system() to call command line programs.
**            # https://stackoverflow.com/questions/238603/how-can-i-get-a-files-size-in-c (user: Volodymyr M. Lisivka)
**            was used for finding the size of a file.
**            # https://stackoverflow.com/questions/3060950/how-to-get-ip-address-from-sock-structure-in-c
**            was used for finding the address of the client connecting to the server.
*******************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <netdb.h>

#define MAX_SIZE 2048 // Size of all-purpose memory buffer.

int processesRunning = 0; // Number of processes running.
int children[5]; // PIDs of the running child processes.

int startServer(int port);
void receiveData(int connectionFD, char* storage);
void sendData(char* sendText, int size, int socketFD);
void assignArgs(char* command, int* dataPort, char* input, char* fileName);
char* fileToBuffer(int fileDescriptor);
void huntZombies();
void handleL(int establishedConnection_FD_d, char* txt_buffer);
int connectDataSocket(char* address, int port);
void handleG(int fileDescriptor, int establishedConnectionFD_d);

int main(int argc, char *argv[])
{
    //Connection socket data
    int listenSocketFD, establishedConnectionFD, listenSocketFD_d, establishedConnectionFD_d; // Listening socket and established connection file descriptors.
    socklen_t sizeOfClientInfo, sizeOfClientInfo_d; // Information for the client.
    struct sockaddr_in clientAddress, clientAddress_d; // Server and client address structs.

    int dataPort; // Port for data connection from client.
    char txt_buffer[MAX_SIZE]; // All-purpose memory buffer.
    char command[256]; // Command from client.
    char fileToOpen[256]; // Requested file from client.
    char* continueMsg = "Continue@@@"; // Message to tell client no errors with message terminator '@@@'.

    // Check usage and arguments.
    if (argc < 2)
    {
        fprintf(stderr,"USAGE: %s port\n", argv[0]);
        exit(1);
    }

    listenSocketFD = startServer(atoi(argv[1])); // Start listening on port for a control connection.
    printf("Server open on %d\n\n", atoi(argv[1]));

    while(1)
    {

        // Accept a control connection, blocking if one isn't available until one connects.
        sizeOfClientInfo = sizeof(clientAddress); // Get the size of the address for the client that will connect.
        establishedConnectionFD = accept(listenSocketFD, (struct sockaddr *)&clientAddress, &sizeOfClientInfo); // Accept connection.
        if (establishedConnectionFD < 0)
        {
            fprintf(stderr, "ERROR on accept\n");
            //continue;
            exit(1);
        }

        // Get the ip address of the client connecting.
        struct sockaddr_in* pV4Addr = (struct sockaddr_in*)&clientAddress;
        struct in_addr ipAddr = pV4Addr->sin_addr;
        char ipAddress[INET_ADDRSTRLEN];
        inet_ntop( AF_INET, &ipAddr, ipAddress, INET_ADDRSTRLEN);
        printf("Connection from %s\n", ipAddress);

        // Get the command and data port from the client on the control connection.
        memset(txt_buffer, '\0', sizeof(txt_buffer));
        receiveData(establishedConnectionFD, txt_buffer);
        memset(command, '\0', sizeof(command));
        memset(fileToOpen, '\0', sizeof(fileToOpen));
        assignArgs(command, &dataPort, txt_buffer, fileToOpen); // Store variables sent from client.

        huntZombies(); // Collect zombie child processes.
        pid_t spawnpid = fork(); // Spawn a new child process.
        switch(spawnpid)
        {
            case -1: // Something went wrong in attempting a fork.
                fprintf(stderr, "ERROR on fork()\n");
                close(establishedConnectionFD); // Close the connection for the failed fork and loop up to listen for a new one.
                continue;
                break;
            case 0: // Child process is running.
                break;
            default: // Parent process is running.
                children[processesRunning++] = spawnpid; // Add new child PID to array of child processes.
                continue; // Listen for another connection.
                break;
        }

        sleep(1); // Give client time to start listening on its data socket.
        establishedConnectionFD_d = connectDataSocket(ipAddress, dataPort); // Establish data connection.

        // Handle command specified by client.
        if(strcmp(command, "-l") == 0)
        {
            sendData(continueMsg, strlen(continueMsg), establishedConnectionFD); // Tell client ok to get directory contents.
            printf("Sending directory to %s:%d\n", ipAddress, dataPort);
            memset(txt_buffer, '\0', sizeof(txt_buffer));
            handleL(establishedConnectionFD_d, txt_buffer); // Send the directory contents.
            printf("Directory transfer complete.\n\n");
        }
        else if(strcmp(command, "-g") == 0)
        {
            printf("File \"%s\" requested on port %d\n", fileToOpen, dataPort);

            // Make sure requested file exists.
            int fileDescriptor = open(fileToOpen, O_RDONLY);
            if(fileDescriptor < 0)
            {
                // Send error 'file not found' on control connection.
                char* errorMsg = "File not found.@@@"; // File not found message with terminator '@@@'.
                sendData(errorMsg, strlen(errorMsg), establishedConnectionFD);
                printf("Requested file not found.\n\n");
                break;
            }

            sendData(continueMsg, strlen(continueMsg), establishedConnectionFD); // Tell client ok to recieve file.

            printf("Sending file \"%s\" to %s:%d\n", fileToOpen, ipAddress, dataPort);
            handleG(fileDescriptor, establishedConnectionFD_d); // Transfer requested file.
            printf("File transfer complete.\n\n");
        }
        else
        {
            // If somehow client sent a command other than -l or -g, send error on control connection.
            char* errorMsg = "Illegal Command.@@@"; // Illegal command message with terminator '@@@'.
            sendData(errorMsg, strlen(errorMsg), establishedConnectionFD);
            printf("Illegal command requested.\n\n");
        }
        close(establishedConnectionFD_d);  // Close data connection.
        break;
    }

    // Close control connections and exit.
    close(establishedConnectionFD);
    close(listenSocketFD);
    return 0;
}


/* Description: This function receives and stores the request/arguments from the client.
 */

void receiveData(int connectionFD, char* storage)
{
    char buffer[2]; // Character buffer.
    int i = 0; // Index for given storage buffer.
    memset(storage, '\0', MAX_SIZE); // Clear storage memory.
    while(1) // Get a character at a time.
    {
        memset(buffer, '\0', sizeof(buffer)); // Clear buffer.
        int numChar = recv(connectionFD, &buffer, 1, 0); // Get one character from the socket.
        if(numChar < 0)
        {
            fprintf(stderr, "Error Reading Data\n");
            exit(1);
        }
        else if(numChar == 0)
        {
            // If didn't get a character, try again.
            continue;
        }

        // Check if all the data has be received by comparing the character to the '*' flag.
        if(strcmp("*", buffer) == 0)
        {
            return;
        }
        strcpy(&storage[i++], buffer); // Store next character into given storage buffer.
    }
}


/* Description: This function stores the request args from the client into
 *              the appropriate variables. It takes the args and pointers to
 *              the variables to save them in as input.
 */

void assignArgs(char* command, int* dataPort, char* input, char* fileName)
{
    char intBuffer[256];
    int i = 0;
    int j = 0;

    // args are separated by '&'
    // First store the command such as -l or -g.
    while((int)input[i] != (int) '&')
    {
        command[j] = input[i];
        ++i;
        ++j;
    }
    ++i;
    j = 0;

    if(strcmp(command, "-g") == 0)
    {
        // If command is -g, store the filename to transfer.
        while((int)input[i] != (int) '&')
        {
            fileName[j] = input[i];
            ++i;
            ++j;
        }
        ++i;
        j = 0;
    }

    // Finally, store the port to use for the data connection.
    memset(intBuffer, '\0', sizeof(intBuffer));
    while((int)input[i])
    {
        intBuffer[j] = input[i];
        ++i;
        ++j;
    }
    *dataPort = atoi(intBuffer);

}


/* Description: This function sends a specified amout of a data to a given
 *              socket in bunches.
 */

void sendData(char* sendText, int size, int socketFD)
{
    int toWrite = 1000; // Number of characters to send.
    int sendIndex = 0;

    // Continue sending data until everything has been sent.
    while(size > 0)
    {
        int alreadySent = 0; // Amount of data already sent in this batch.
        int nextSend = 0; // Tracks amount of data left to send.

        if(size < toWrite) toWrite = size; // If amount of data to send is smaller than 1000, send the exact amount.
        nextSend = toWrite; // Initialize amount of data to be sent.

        while(alreadySent < toWrite) // Loop until full batch is sent.
        {
            int sentData = send(socketFD, &sendText[sendIndex], nextSend, 0); // Try sending everything still unsent.
            if(sentData < 0)
            {
                fprintf(stderr, "SERVER: ERROR writing to socket\n");
                exit(1);
            }
            sendIndex += sentData;
            alreadySent += sentData; // Increment amount of data sent in this batch. It controls the loop and buffer index.
            nextSend -= sentData; // Decrement amount of data left to send in this batch.
        }
        size -= toWrite; // Decrement size left to be sent.
    }
}


/* Description: This function gets a file descriptor. It finds the length of the file,
 *              allocates memory to store the contents of the file, then saves it. It
 *              appends '@@@' to the end to indicate the end of the data to the eventual
 *              receiver. It returns a pointer to the memory.
 */

char* fileToBuffer(int fileDescriptor)
{
    int i;
    int fileSize = (int)lseek(fileDescriptor, 0, SEEK_END) + 10; // Find the size of the file and add some extra bytes of space.
    char* fileContents = malloc(fileSize); // Allocate memory to store the file contents.
    memset(fileContents, '\0', sizeof(fileContents));
    lseek(fileDescriptor, 0, SEEK_SET);
    read(fileDescriptor, fileContents, fileSize); // Read file contents into buffer.

    // Append terminator to the end of the buffer contents.
    for(i = 0; i < 3; ++i)
    {
        fileContents[strlen(fileContents)] = '@';
    }
    return fileContents;
}


/* Description: This function catches any child processes that have terminated.
 */

void huntZombies()
{
	int zombies; // Stores the pid of caught child processes.
	int childExitMethod = -5; // Stores the exit method.
	int i; // For loop counter.

	// Loop through all child processes that haven't been reaped and catch the zombies.
	for(i = 0; i < processesRunning; ++i)
	{
		zombies = waitpid(children[i], &childExitMethod, WNOHANG); // Try to catch the process.
		if(zombies > 0)
		{
			// If process was caught, remove it from the global array holding the children.
			children[i] = children[--processesRunning];
		}
	}
}


/* Description: This function starts a TCP socket/server on a specified port and listens on
 *              that port. It returns the socket's file descriptor.
 */

int startServer(int port)
{
    //Connection socket data
    int listenSocketFD; // Listening socket and established connection file descriptors and port number.
    struct sockaddr_in serverAddress; // Server and client address structs.

    // Set up the address struct for this process (the server).
    memset((char *)&serverAddress, '\0', sizeof(serverAddress)); // Clear out the address struct.
    serverAddress.sin_family = AF_INET; // Create a network-capable TCP socket.
    serverAddress.sin_port = htons(port); // Store the port number.
    serverAddress.sin_addr.s_addr = INADDR_ANY; // Any address is allowed for connection to this process.

    // Set up the socket.
    listenSocketFD = socket(AF_INET, SOCK_STREAM, 0); // Create the socket.
    if (listenSocketFD < 0)
    {
        fprintf(stderr, "ERROR opening socket\n");
        exit(1);
    }

    // Enable the socket to begin listening.
    if (bind(listenSocketFD, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) // Connect socket to port.
    {
        fprintf(stderr, "ERROR on binding\n");
        exit(1);
    }

    listen(listenSocketFD, 5); // Flip the socket on, allow it to receive up to 5 connections.
    return listenSocketFD;
}


/* Description: This function handles the -l command. It saves the directory contents to a temp
 *              file, writes the contents of that file to the data socket, then removes the temp file.
 */

void handleL(int establishedConnectionFD_d, char* txt_buffer)
{
    int fileDescriptor;
            char tempFile[256];
            char lsCmd[256]; // Command to store directory contents in a temp file.
            char rmCmd[256]; // Commmand to delete temp file.

            memset(tempFile, '\0', sizeof(tempFile));
            memset(lsCmd, '\0', sizeof(lsCmd));
            memset(rmCmd, '\0', sizeof(rmCmd));

            // Store the temp filename and commands as strings.
            // Temp file will be hidden and use the pid in the name. This will make it unique.
            sprintf(tempFile, ".ls%d.txt", getpid());
            sprintf(lsCmd, "ls > %s", tempFile);
            sprintf(rmCmd, "rm %s", tempFile);

            // Open user specified file for output as write only. Truncate the file or create one if it doesn't exist.
    		if(fileDescriptor < 0)
    		{
    			// Error opening the file. Exit child process and display error message.
    			printf("cannot open %s for output\n", tempFile);
    			fflush(stdout);
    	        exit(1);
    		}

            system(lsCmd); // Use system() to run bash ls program.
            fileDescriptor = open(tempFile, O_RDONLY);
            char* fileContents = fileToBuffer(fileDescriptor); // Get pointer to directory contents.
            close(fileDescriptor);
            system(rmCmd); // Delete temp file.
            sendData(fileContents, strlen(fileContents), establishedConnectionFD_d); // Send directory content data.
            free(fileContents);
}


/* Description: This function creates a TCP socket to connect with a specified address and port.
 *              It returns the socket's file descriptor after connection has been established.
 */

int connectDataSocket(char* address, int port)
{
    int socketFD; // Socket file descriptor and port number for communication.
    struct sockaddr_in serverAddress; // Server address struct.
    struct hostent* serverHostInfo; // Struct for the localhost address.

    memset((char*)&serverAddress, '\0', sizeof(serverAddress)); // Clear out the address struct.

    serverAddress.sin_family = AF_INET; // Create a network capable socket.
    serverAddress.sin_port = htons(port); // Store the port number.
    serverHostInfo = gethostbyname(address); // Convert localhost into special form of address.

    if (serverHostInfo == NULL)
    {
        fprintf(stderr, "CLIENT: ERROR, no such host\n");
        exit(2);
    }

    // Copy in the address.
    memcpy((char*)&serverAddress.sin_addr.s_addr, (char*)serverHostInfo->h_addr, serverHostInfo->h_length);

    // Set up the socket.
    socketFD = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFD < 0)
    {
        fprintf(stderr, "CLIENT: ERROR opening socket\n");
        exit(2);
    }

    // Connect to server.
    if (connect(socketFD, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) // Connect to socket address.
    {
        fprintf(stderr, "CLIENT: ERROR connecting to port %d\n", port);
        exit(2);
    }
    return socketFD;
}


/* Description: This function handles the -g command. It puts the file contents into a buffer,
 *              writes the contents of that buffer to the data socket, then frees the buffer.
 */

void handleG(int fileDescriptor, int establishedConnectionFD_d)
{
    char* fileContents = fileToBuffer(fileDescriptor); // Get file contents.
    sendData(fileContents, strlen(fileContents), establishedConnectionFD_d); // Send file.
    free(fileContents); // Free allocated memory to store file contents.
}
