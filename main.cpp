#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <termios.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

atomic<bool> loop{1};
atomic<bool> secondThreadEnded{0};
atomic<bool> messageRead{0};
vector<char> charVector;

// This function for properly handle of pressing the escape key
int getKey() {
    struct termios originalTerminalAttributes;
    struct termios newTerminalAttributes;

    /* set the terminal to raw mode */
    tcgetattr(fileno(stdin), &originalTerminalAttributes);
    memcpy(&newTerminalAttributes, &originalTerminalAttributes, sizeof(struct termios));
    newTerminalAttributes.c_lflag &= ~(ECHO|ICANON);
    newTerminalAttributes.c_cc[VTIME] = 0;
    newTerminalAttributes.c_cc[VMIN] = 0;
    tcsetattr(fileno(stdin), TCSANOW, &newTerminalAttributes);

    /* read a character from the stdin stream without blocking */
    /*   returns EOF (-1) if no character is available */
    int key = fgetc(stdin);
    if(key == -1)
    {
        rewind(stdin);
    }

    /* restore the original terminal attributes */
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
        perror("Socket");
        exit(1);
    }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("142.250.185.196");
    server.sin_port = htons(80);

    if(connect(socketDescriptor, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("Connect");
        exit(1);
    }

    char message[] = "GET / HTTP/1.1\r\nHost: www.google.com\r\n\r\n";
    while(loop)
    {
        if(send(socketDescriptor, message, strlen(message), 0) < 0)
        {
            perror("Send");
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

            char buffer[contentLength];
            if(recv(socketDescriptor, buffer, contentLength, 0) < 0)
            {
                perror("recv");
                exit(1);
            }
            buffer[contentLength] = '\0';
            response += buffer;
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

            char buffer[chunkSize];
            if(recv(socketDescriptor, buffer, chunkSize, 0) < 0)
            {
                perror("recv");
                exit(1);
            }
            response += buffer;
        }

        if(chunked)
        {
            line = getLine(socketDescriptor); // It will return \r\n line
        }

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
