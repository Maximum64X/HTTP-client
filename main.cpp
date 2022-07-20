#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <termios.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

using namespace std;

atomic<bool> loop{1};
atomic<bool> secondThreadEnded{0};
atomic<bool> messageRead{0};

vector<char> charVector;

const char hostName[] = "www.google.com";

int getKey() {
    struct termios originalTerminalAttributes;
    struct termios newTerminalAttributes;

    // Set the terminal to raw mode
    tcgetattr(fileno(stdin), &originalTerminalAttributes);
    memcpy(&newTerminalAttributes, &originalTerminalAttributes, sizeof(struct termios));
    newTerminalAttributes.c_lflag &= ~(ECHO|ICANON);
    newTerminalAttributes.c_cc[VTIME] = 0;
    newTerminalAttributes.c_cc[VMIN] = 0;
    tcsetattr(fileno(stdin), TCSANOW, &newTerminalAttributes);

    // Read a character from the stdin stream without blocking
    // Returns EOF (-1) if no character is available
    int key = fgetc(stdin);
    if(key == -1)
    {
        rewind(stdin);
    }

    // Restore the original terminal attributes
    tcsetattr(fileno(stdin), TCSANOW, &originalTerminalAttributes);

    return key;
}

string getLine(int socketDescriptor)
{
    string line = "";
    char symbol, previousSymbol = 0;
    while(read(socketDescriptor, &symbol, 1)!=0)
    {
        line += symbol;

        // Stop if it is end of line
        if(previousSymbol == '\r' && symbol == '\n')
        {
            break;
        }
        previousSymbol = symbol;
    }
    return line;
}

void sendRequest()
{
    int socketDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if(socketDescriptor < 0)
    {
        perror("Function socket");
        exit(1);
    }

    struct hostent *remoteHost;
    remoteHost = gethostbyname(hostName);

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(inet_ntoa(*( struct in_addr*)remoteHost->h_addr_list[0]));
    server.sin_port = htons(80);

    if(connect(socketDescriptor, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("Function connect");
        exit(1);
    }

    string message = string("GET / HTTP/1.1\r\nHost: ") + hostName + "\r\n\r\n";
    const int length = message.size();
    const char *request = message.c_str();

    while(loop)
    {
        if(send(socketDescriptor, request, length, 0) < 0)
        {
            perror("Function send");
            exit(1);
        }

        bool chunked = 0;
        string response;
        string line;
        int contentLength = 0;
        while((line = getLine(socketDescriptor)) != "")
        {
            response += line;

            int pos = line.find(":");
            string leftLine = line.substr(0, ++pos);
            if(leftLine == "Content-Length:")
            {
                contentLength = stoi(line.substr(++pos));
                break;
            }

            if(line == "Transfer-Encoding: chunked\r\n")
            {
                chunked = 1;
                break;
            }

            if(line == "\r\n")
            {
                cerr << "Error: head of the server response read, but neither \"Transfer-Encoding: chunked\" nor \"Content-Length\" found!" << endl;
                exit(1);
            }
        }

        if(contentLength)
        {
            // Skip remaining lines in head
            do
            {
                line = getLine(socketDescriptor);
                response += line;
            } while (line != "\r\n");

            char *buffer = new char[contentLength + 1];
            if(recv(socketDescriptor, buffer, contentLength, MSG_WAITALL) < 0)
            {
                perror("recv");
                exit(1);
            }
            buffer[contentLength] = '\0';
            response += buffer;
            delete[] buffer;
        }

        while(chunked)
        {
            line = getLine(socketDescriptor); // It will return \r\n line
            response += line;

            line = getLine(socketDescriptor);
            response += line;
            int chunkSize = stoi(line, NULL, 16);
            if(chunkSize == 0)
            {
                break;
            }

            char *buffer = new char[chunkSize + 1];
            if(recv(socketDescriptor, buffer, chunkSize, MSG_WAITALL) < 0)
            {
                perror("Function recv");
                exit(1);
            }
            buffer[chunkSize] = '\0';
            response += buffer;
            delete[] buffer;
        }

        if(chunked)
        {
            line = getLine(socketDescriptor); // It will return \r\n line
            response += line;
        }

        // Wait if main thread not ended working with vector
        while(messageRead)
        {
            this_thread::sleep_for(chrono::milliseconds(10));
        }
        charVector.insert(charVector.end(), response.begin(), response.end());
        messageRead = 1;

        this_thread::sleep_for(chrono::seconds(5));
    }
    close(socketDescriptor);

    secondThreadEnded = 1;
    return;
}

int main()
{
    thread secondThread(sendRequest);
    secondThread.detach();

    const int escapeCode = 27;
    while(!secondThreadEnded)
    {
        if(messageRead)
        {
            for(vector<char>::iterator it = charVector.begin(); it != charVector.end(); ++it)
            {
                cout << *it << flush;
            }
            while(charVector.size())
            {
                charVector.pop_back();
            }
            messageRead = 0;
            cout << endl << endl << endl << endl;
        }

        if(getKey() == escapeCode)
        {
            loop = 0;
        }
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    return 0;
}
