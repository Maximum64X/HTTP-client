# HTTP-client

## Description

It is a small program that can send request on any server in the internet every 5 seconds and display it's response in terminal.
You can change target server by changing value of constant variable "hostName" in main.cpp.
If you want to end execution of program press Esc.

## Building

To build program on Linux Mint use these commands

```bash
cmake --configure . -B build
cmake --build build --target all
```