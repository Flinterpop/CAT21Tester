

#include "SocketStuff.h"

#pragma comment(lib,"ws2_32.lib") 
#pragma (commentlib, "IPHLPAPI.lib") //used by get adapters


#define BUFLEN 1510  
sockaddr_in m_server;
int m_client_socket;

/*
void getAdapterInfo()
{
    IP_ADAPTER_INFO* pAdapterInfo;
    ULONG            ulOutBufLen;
    DWORD            dwRetVal;

    pAdapterInfo = (IP_ADAPTER_INFO*)malloc(sizeof(IP_ADAPTER_INFO));
    ulOutBufLen = sizeof(IP_ADAPTER_INFO);

    if (GetAdaptersInfo(pAdapterInfo, &ulOutBufLen) != ERROR_SUCCESS) { //first call is meant to fail
        free(pAdapterInfo);
        pAdapterInfo = (IP_ADAPTER_INFO*)malloc(ulOutBufLen);
    }

    if ((dwRetVal = GetAdaptersInfo(pAdapterInfo, &ulOutBufLen)) != ERROR_SUCCESS) {
        printf("GetAdaptersInfo call failed with %d\n", dwRetVal);
    }

    int anum = 0;
    PIP_ADAPTER_INFO pAdapter = pAdapterInfo;
    while (pAdapter) {
        printf("Adapter # %02d\r\n", anum++);

        printf("Adapter Name: %s\n", pAdapter->AdapterName);
        printf("Adapter Desc: %s\n", pAdapter->Description);
        printf("\tAdapter Addr: \t");
        for (UINT i = 0; i < pAdapter->AddressLength; i++) {
            if (i == (pAdapter->AddressLength - 1))
                printf("%.2X\n", (int)pAdapter->Address[i]);
            else
                printf("%.2X-", (int)pAdapter->Address[i]);
        }
        printf("IP Address: %s\n", pAdapter->IpAddressList.IpAddress.String);
        printf("IP Mask: %s\n", pAdapter->IpAddressList.IpMask.String);
        printf("\tGateway: \t%s\n", pAdapter->GatewayList.IpAddress.String);
        printf("\t***\n");

        if (pAdapter->DhcpEnabled) {
            printf("\tDHCP Enabled: Yes\n");
            printf("\t\tDHCP Server: \t%s\n", pAdapter->DhcpServer.IpAddress.String);
        }
        else
            printf("\tDHCP Enabled: No\n");

        pAdapter = pAdapter->Next;
        Sleep(1);
    }


    if (pAdapterInfo)  free(pAdapterInfo);

}

*/

bool initialise_winsock()
{
    WSADATA ws;
    printf("Initialising Winsock...\r\n");
    if (WSAStartup(MAKEWORD(2, 2), &ws) != 0)
    {
        printf("Failed. Error Code: %d\r\n", WSAGetLastError());
        return true;
    }
    return false;
}




bool OpenSocket(char* ip, u_short port)
{
    // create socket
    if ((m_client_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == SOCKET_ERROR) // <<< UDP socket
    {
        printf("socket() failed with error code: %d", WSAGetLastError());
        return true;
    }

    // setup address structure
    memset((char*)&m_server, 0, sizeof(m_server));
    m_server.sin_family = AF_INET;
    m_server.sin_port = htons(port);

    //InetPton(AF_INET, _T("192.168.1.1"), &m_server.sin_addr.s_addr);

    m_server.sin_addr.S_un.S_addr = inet_addr(ip);
    return false;
}


bool SendUDP(char *buffer, int len)
{
    if (sendto(m_client_socket, (const char *)buffer, len, 0, (sockaddr*)&m_server, sizeof(sockaddr_in)) == SOCKET_ERROR)
    {
        printf("sendto() failed with error code: %d\r\n", WSAGetLastError());
        return true;
    }
    //printf("Sent message of length %d\r\n", len);
    return false;
}


bool CloseSocket()
{
    closesocket(m_client_socket);
    return false;
}

void closeandclean_winsock()
{
    WSACleanup();
}
