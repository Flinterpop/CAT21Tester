#ifndef SOCKETSTUFF_H
#define SOCKETSTUFF_H


#include <iostream>
#include <winsock2.h>
#include <iphlpapi.h>


void getAdapterInfo();
bool initialise_winsock();

bool OpenSocket(char* ip, u_short port);

bool SendUDP(char* buffer, int len);

bool CloseSocket();
void closeandclean_winsock();


#endif



