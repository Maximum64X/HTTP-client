#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <termios.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

using namespace std;

std::mutex m;
std::condition_variable cv;

atomic<bool> loop{1};
atomic<bool> messageRead{0};

bool ready = false;

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
    std::unique_lock<std::mutex> lk(m);

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
        const int messageSize = 1024;
        char buffer[messageSize];
        // Wait for first byte of response
        recv(socketDescriptor, buffer, 0, 0);
        int receivedBytes = 0;
        while((receivedBytes = recv(socketDescriptor, buffer, messageSize, MSG_DONTWAIT)) != -1)
        {
            buffer[receivedBytes] = '\0';
            response += buffer;
            // Wait for next part of response
            this_thread::sleep_for(chrono::milliseconds(10));
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

    ready = true;
    std::notify_all_at_thread_exit(cv, std::move(lk));
    return;
}

int main()
{
    thread secondThread(sendRequest);
    secondThread.detach();

    const int escapeCode = 27;
    for(;;)
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
            break;
        }
        this_thread::sleep_for(chrono::milliseconds(100));
    }

    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk, []{return ready;});
    return 0;
}
