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

        string response;
        char buffer[1024];
        // Wait for first byte of response
        recv(socketDescriptor, buffer, 0, 0);
        while(recv(socketDescriptor, buffer, strlen(buffer), MSG_DONTWAIT) != -1)
        {
            response += buffer;
            // Wait for next part of response
            this_thread::sleep_for(chrono::nanoseconds(1));
        }

        // Wait if main thread not ended working with vector
        while(messageRead)
        {
            this_thread::sleep_for(chrono::milliseconds(1));
        }
        charVector.insert(charVector.end(), response.begin(), response.end());
        messageRead = 1;

        // Increases the speed of response to pressing the Esc key
        for(int i = 0; i < 50; i++)
        {
            if(!loop)
            {
                break;
            }
            this_thread::sleep_for(chrono::milliseconds(100));
        }
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
        }

        if(getKey() == escapeCode)
        {
            loop = 0;
        }
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    return 0;
}
