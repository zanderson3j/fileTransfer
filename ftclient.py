################################################################################
# Program Filename: ftclient.py
# Author: Zachary Anderson
# Description: This program works in conjunction with ftserver.c to implement
#              file transfer over a connection between TCP sockets. This program
#              connects to the server at a specified address and port, sends a
#              request for either the directory contents or a file transfer, opens
#              another socket that listens on a specified port, and receives its
#              requested data on from the new socket after the server program
#              connects to it.
# Citations: # The socket programming is based on the CS 372 Lecture 15 TCP server
#            example.
#            # Used https://stackoverflow.com/questions/82831/how-to-check-whether-a-file-exists
#            for checking if a file already exists in the current directory.
################################################################################


import sys
from socket import *
from time import sleep
import os.path


################################################################################
# Description: This function error checks the command line arguments.
################################################################################

def argCheck():
    # Check the correct args were entered.
    dPortIndex = 0 # sys.argv index containing the data port to use.
    if len(sys.argv) == 5 and "-l" in sys.argv[3]:
        dPortIndex = 4
    elif len(sys.argv) == 6 and "-g" in sys.argv[3]:
        dPortIndex = 5
    else:
        print ('USAGE: python ' + sys.argv[0] + ' <SERVER_HOST>' + ' <SERVER_PORT>' + ' <COMMAND (-l/-g)>' + ' (FILENAME)' + ' <DATA_PORT>')
        sys.exit(1)
    # Check that the port numbers supplied are integers.
    try:
        cPortEntered = int(sys.argv[dPortIndex])
        dPortEntered = int(sys.argv[2])
    except ValueError:
        print("Error: Ports should integers.")
        print ('USAGE: python ' + sys.argv[0] + ' <SERVER_HOST>' + ' <SERVER_PORT>' + ' <COMMAND (-l/-g)>' + ' (FILENAME)' + ' <DATA_PORT>')
        sys.exit(1)
    return dPortIndex


################################################################################
# Description: This function receives and returns data from a socket.
################################################################################

def receiveData(readSocket):
    data = ''
    # Read data until the agreed upon terminator is reached.
    while "@@@" not in data:
        batch = readSocket.recv(1024)
        if not batch:
            return data
        data += batch
    return data[:-3]


################################################################################
# Description: This function creates and returns a TCP socket listening on a
#              specified port.
################################################################################

def dataStartUp(port):
    sSocket = socket(AF_INET, SOCK_STREAM) # Create a TCP socket.
    sSocket.bind(('', int(port))) # Bind socket to user specified port.
    sSocket.listen(1) # Listen for a connection.
    return sSocket


################################################################################
# Description: This function receives a filename and data. It creates the file,
#              handling duplicates by appending an integer to the end of the
#              name, writes the data to it, then closes the file and returns
#              its name.
################################################################################

def saveFile(filename, data):
    ext = 0
    newFilename = filename
    # Append an int to the end of the filname if the file already exists.
    while os.path.isfile(newFilename):
        ext += 1
        indexOfPeriod = filename.find('.')
        newFilename = filename[:indexOfPeriod] + '-' + str(ext) + filename[indexOfPeriod:]
    f = open(newFilename, 'w')
    f.write(data)
    f.close()
    return newFilename


################################################################################
# Description: This function creates a TCP socket and connects it to a specified
#              server and port. It returns the new socket.
################################################################################

def initiateContact(serverName, serverPort):
    connectionSocket = socket(AF_INET, SOCK_STREAM)
    connectionSocket.connect((serverName,serverPort))
    return connectionSocket


################################################################################
# Description: This function sends the client request to the server on the
#              control connection socket.
################################################################################

def makeRequest(connectionSocket, maxArgIdx):
    # For this request '&' separates args and '*' marks the end of the transmission.
    if maxArgIdx == 4:
        connectionSocket.send(sys.argv[3] + '&' + sys.argv[maxArgIdx] + '*')
    elif maxArgIdx == 5:
        connectionSocket.send(sys.argv[3] + '&' + sys.argv[4] + '&' + sys.argv[maxArgIdx] + '*')


# Start of main program.
maxArgIdx = argCheck() # Validate args and get the number of args.
serverName = sys.argv[1]
serverPort = int(sys.argv[2])
connectionSocket = initiateContact(serverName, serverPort) # Connect to the server.
makeRequest(connectionSocket, maxArgIdx) # Send the request to the server.

serverSocket = dataStartUp(sys.argv[maxArgIdx]) # Start listening on the data connection.
dataSocket, addr = serverSocket.accept() # Data connection established.

sleep(1); # Give server time to check for errors and respond.
errorMsg = receiveData(connectionSocket) # Get error or go ahead message from server.

if "Continue" in errorMsg:
    # Display what will be received.
    if sys.argv[3] == "-g":
        print ('Receiving \"' + sys.argv[4] + '\" from ' + addr[0] + ':' + sys.argv[maxArgIdx])
    else:
        print ("Receiving directory structure from " + addr[0] + ":" + sys.argv[maxArgIdx] + "\n")

    returnData = receiveData(dataSocket) # Get the data requested

    # Display results of the transfer.
    if sys.argv[3] == "-g":
        savedFile = saveFile(sys.argv[4], returnData)
        print ('Transfer complete. Saved "' + savedFile + '" to hard drive.')
    else:
        print ("Transfer complete. Remote directory contents:" + "\n")
        print returnData
else:
    print errorMsg # Error dectected on the server side.

# Close open sockets.
dataSocket.close()
connectionSocket.close()
