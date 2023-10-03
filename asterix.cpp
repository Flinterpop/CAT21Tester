// Dear ImGui: standalone example application for DirectX 9
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

#include "imgui.h"


#include <stdio.h>
#include "asterix.h"
#include "geodesic.h"

#include <iostream>
#include <winsock2.h>
#include "SocketStuff.h"
#include <vector>
#include <mutex>
#include <fstream>

#include "DebugWindow.hpp"



//default ASTERIX details - overrdien by CAT21.ini if present
u_short ASXPort = 6666;
char ASXIP[30] = "192.168.1.255";
int sendInterval = 5;
int sac = 206;
int sic = 102;
int baseUDPSendInterval = 1; //this is 1 second

//Test Aircraft Details
char ACIdent[8] = "WJ1243";
int ICAO = 0xC4321;
int Mode3 = 02647;
double latitude = 46.00;
double longitude = -70.00;
int alt = 20100;
double heading = 90;
double speedInKnots = 350;
int deltaAlt = 5;
int deltaHdg = 1;



std::mutex mtx;
HANDLE ptrTimerHandleSendPacket;
HANDLE ptrTimerHandleSendAircraft;


bool bPause = false;
bool bDebug = false;
bool bMODERN = false;


byte _MSG[1500];
int index = 0;
int  updateCounter = 0;
int Cat21MsgCount = 0;

enum CAT21Type { V0point23, V0point26, V2PLUS };

unsigned char m_FRN[50];


static int sendCount = -1;
void __stdcall TimerCallbackSendUDP(PVOID, BOOLEAN)//(PVOID lpParameter, BOOLEAN TimerOrWaitFired)
{
    if (bPause) return;
    ++sendCount;
    if ((sendCount % sendInterval) == 0)
    {
        CreateAndSendCAT21();
        sendCount = 0;
    }
}

void __stdcall TimerCallbackUpdateAircraft(PVOID, BOOLEAN)//(PVOID lpParameter, BOOLEAN TimerOrWaitFired)
{
    if (bPause) return;
    UpdatePosition();
}


void InitFRN()
{
    for (int x = 1; x <= 49; x++)
    {
        m_FRN[x] = 0b10000000 >> ((x - 1) % 7);
    }
}


void InitADSB()
{
    initialise_winsock();
    std::fstream my_file;
    my_file.open("cat21.ini", std::ios::in);
    if (!my_file) {
        puts("No Init file!");
    }
    else {
        puts("File opened successfully!\r\n");
        char buf[50];
        my_file >> buf;
        ASXPort = atoi(buf);
        printf("Port is %d\r\n", ASXPort);

        my_file >> buf;
        strcpy(ASXIP, buf);
        printf("IP is %s\r\n", ASXIP);

        my_file >> buf;
        sendInterval = atoi(buf);
        printf("Send Period is %d (seconds)\r\n", sendInterval);

        my_file >> buf;
        sac = atoi(buf);
        printf("SAC %d\r\n", sac);

        my_file >> buf;
        sic = atoi(buf);
        printf("SIC %d\r\n", sic);

        my_file.close();
        Sleep(200);
    }


    if (OpenSocket(ASXIP, ASXPort) == true)
    {
        puts("Open Socket failed!");
        CloseSocket();
        closeandclean_winsock();
    }


    CreateTimerQueueTimer(&ptrTimerHandleSendPacket, NULL, TimerCallbackSendUDP, NULL, 500, baseUDPSendInterval * 1000, WT_EXECUTEDEFAULT); //send ASX every sendPeriod seconds
    CreateTimerQueueTimer(&ptrTimerHandleSendAircraft, NULL, TimerCallbackUpdateAircraft, NULL, 500, 1000, WT_EXECUTEDEFAULT); //update Aircraft once per second
    InitFRN();
    pme("ADSB Initialized\r\n");
    if (bDebug) puts("ADSB Initialized");
}

void CleanUpADSB()
{
    (void) DeleteTimerQueueTimer(NULL, ptrTimerHandleSendPacket, NULL);

    CloseSocket();
    closeandclean_winsock();
}


void UpdatePosition()
{
    CalcNextPosition();
    ++updateCounter;
    if (updateCounter > 0 && updateCounter < 200) heading += deltaHdg;
    else if (updateCounter > 200 && updateCounter < 360) heading += 0;
    else if (updateCounter > 360 && updateCounter < 540) heading += deltaHdg;
    else if (updateCounter > 540 && updateCounter < 760) heading += 0;
    else if (updateCounter == 760) updateCounter = 0;
    if (heading > 360) heading = heading - 360;
    else if (heading < 0) heading = heading + 360;
    //if (bDebug) pmef("Update Counter: %d New Hdg: %f\r\n", updateCounter, heading);
    //printf("Update Counter: %d New Hdg: %f\r\n", updateCounter,heading);
}

void CalcNextPosition()
{
    alt += deltaAlt;
    
    if (alt < 100)
    {
        alt = 100;
        deltaAlt = 0;
    }
    else if (alt > 60000)
    {
        alt = 60000;
        deltaAlt = 0;
    }
    

    double deltaDistanceInMetres = Distance(speedInKnots, 1.0);

    double lat2, lon2, azi2;
    azi2 = EndPoint(latitude, longitude, heading, deltaDistanceInMetres, lat2, lon2);
    double kmh = speedInKnots * 1.852;
    if (bDebug) pmef("New Pos:  %6.4f deg, %7.4f deg, New Hdg: %3.0f deg %03.0f knots is %03.0f m/s\r\n", lat2, lon2, azi2, speedInKnots, kmh);
    //printf("New Pos:  %6.4f deg, %7.4f deg, New Hdg: %3.0f deg %03.0f knots is %03.0f m/s\r\n", lat2, lon2, azi2, speedInKnots, deltaDistanceInMetres);    latitude = lat2;
    latitude= lat2;
    longitude = lon2;

}



void InsertSAC_SIC_DI010()
{
    _MSG[index++] = (byte)sac;
    _MSG[index++] = (byte)sic;
    //printf("SIC: %X  SAC: %X \r\n", (int)sic,(int)sac);
    //printf("SIC: %X  SAC: %X \r\n", (int)_MSG[index-1], (int)_MSG[index - 2]);
}

//THIS is HARD CODED
void InsertEmitterCategoryDI020()
{
    /*
    0 = No ADS - B Emitter Category Information
    1 = light aircraft <= 15500 lbs
    2 = 15500 lbs < small aircraft < 75000 lbs
    3 = 75000 lbs < medium a / c < 300000 lbs
    4 = High Vortex Large
    5 = 300000 lbs <= heavy aircraft
    6 = highly manoeuvrable (5g acceleration capability) and high speed (>400 knots cruise)
    7 to 9 = reserved
    10 = rotocraft
    11 = glider / sailplane
    12 = lighter-than-air
    13 = unmanned aerial vehicle
    14 = space / transatmospheric vehicle
    15 = ultralight / handglider / paraglider
    16 = parachutist / skydiver
    17 to 19 = reserved
    20 = surface emergency vehicle
    21 = surface service vehicle
    22 = fixed ground or tethered obstruction
    23 = cluster obstacle
    24 = line obstacle
    */
    _MSG[index++] = 3;
}

//V0.23/26 DI
void InsertTimeOfDayDI030()
{
    SYSTEMTIME st;
    GetSystemTime(&st);
    int elapsed = (int)st.wHour * 3600 + (int)st.wMinute * 60 + (int)st.wSecond;

    int TOD_16 = (int)(elapsed * 128);
    _MSG[index++] = (byte)(TOD_16 / (256 * 256));
    _MSG[index++] = (byte)(TOD_16 / (256));
    _MSG[index++] = (byte)(TOD_16 - (_MSG[index - 2] * 256 * 256) - (_MSG[index - 1] * 256));
}

//THIS is HARD CODED
void InsertTargetReportDescriptorDI040()
{
    _MSG[index++] = 0x01;
    _MSG[index++] = 0x00;
}

void InsertMode3ADI070()
{
    _MSG[index++] = (byte)(Mode3 / 256);
    _MSG[index++] = (byte)(Mode3 - _MSG[index - 1] * 256);
}

void InsertTimeOfMsgReceptionDI073()
{
    SYSTEMTIME st;
    GetSystemTime(&st);
    int elapsed = (int)st.wHour * 3600 + (int)st.wMinute * 60 + (int)st.wSecond;

    int TOD_16 = (int)(elapsed * 128);
    _MSG[index++] = (byte)(TOD_16 / (256 * 256));
    _MSG[index++] = (byte)(TOD_16 / (256));
    _MSG[index++] = (byte)(TOD_16 - (_MSG[index - 2] * 256 * 256) - (_MSG[index - 1] * 256));
}

void InsertTimeOfMsgReceptionDI075()
{
    SYSTEMTIME st;
    GetSystemTime(&st);
    int elapsed = (int)st.wHour * 3600 + (int)st.wMinute * 60 + (int)st.wSecond;

    int TOD_16 = (int)(elapsed * 128);
    _MSG[index++] = (byte)(TOD_16 / (256 * 256));
    _MSG[index++] = (byte)(TOD_16 / (256));
    _MSG[index++] = (byte)(TOD_16 - (_MSG[index - 2] * 256 * 256) - (_MSG[index - 1] * 256));
}

void InsertACAddress_DI80()
{
    //Tgt Address  24 bit ICAO
    _MSG[index++] = (byte)((ICAO & 0xFF0000) >> 16);
    _MSG[index++] = (byte)((ICAO & 0x00FF00) >> 8);
    _MSG[index++] = (byte)((ICAO & 0x0000FF));
}

//V0.23/26 DI
void InsertFOMDI090()
{
    _MSG[index++] = 0b10100000;
    _MSG[index++] = 0b00001000;

}

void InsertPositionDI130_3BytePos()
{
    int Lat = (int)(latitude / 0.0000214576721191406);  //this number is 180/2^23
    _MSG[index++] = (byte)((Lat & 0xFF0000) >> 16);
    _MSG[index++] = (byte)((Lat & 0x00FF00) >> 8);
    _MSG[index++] = (byte)((Lat & 0x0000FF));

    int Lon = (int)(longitude / 0.0000214576721191406);
    _MSG[index++] = (byte)((Lon & 0xFF0000) >> 16);
    _MSG[index++] = (byte)((Lon & 0x00FF00) >> 8);
    _MSG[index++] = (byte)((Lon & 0x0000FF));
}

void InsertPositionDI130_4BytePos()
{
    int Lat = (int)(latitude / 0.00000536441802978515625); //this number is 180/2^25
    _MSG[index++] = (byte)((Lat & 0xFF000000) >> 24);
    _MSG[index++] = (byte)((Lat & 0x00FF0000) >> 16);
    _MSG[index++] = (byte)((Lat & 0x0000FF00) >> 8);
    _MSG[index++] = (byte)((Lat & 0x000000FF));



    int Lon = (int)(longitude / 0.00000536441802978515625);
    //byte val = (byte)((Lon & 0xFF000000) >> 24);
    //if (longitude > 0) val |= 0x10000000;
    //_MSG[index++] = val;

    _MSG[index++] = (byte)((Lon & 0xFF000000) >> 24);
    _MSG[index++] = (byte)((Lon & 0x00FF0000) >> 16);
    _MSG[index++] = (byte)((Lon & 0x0000FF00) >> 8);
    _MSG[index++] = (byte)((Lon & 0x000000FF));
}

void InsertGeoHeightDI140()
{
    int gcount = (int)(alt / 6.25);
    byte msb = (byte)(gcount / 256);
    byte lsb = gcount % 256;
    _MSG[index++] = msb;
    _MSG[index++] = lsb;
}

void InsertFLDI145()
{
    int gcount = (int)(alt / 6.25);
    gcount = alt / 25;
    byte msb = (byte)(gcount / 256);
    byte lsb = gcount % 256;
    _MSG[index++] = msb;
    _MSG[index++] = lsb;
}

//HARD CODED 
void InsertBarometricVerticalRateDI155()
{
    _MSG[index++] = 0x00;
    _MSG[index++] = 0x00;
}

void InsertAirborneGroundVectorDI160()
{
    int AGVSpeed = (int)(speedInKnots / 0.22);
    _MSG[index++] = (byte)((AGVSpeed & 0xFF00) >> 8);
    _MSG[index++] = (byte)((AGVSpeed & 0x00FF));

    int angle = (int)(heading / 0.0055);
    _MSG[index++] = (byte)((angle & 0xFF00) >> 8); // / 256);
    _MSG[index++] = (byte)((angle & 0x00FF)); //  % 256);
}

void InsertTrackNumberDI161()
{
    _MSG[index++] = (byte)((Mode3 & 0xFF00) >> 8);  //Track Number MSB
    _MSG[index++] = (byte)(Mode3 & 0x00FF);         //Track Number LSB
}

byte ICAOencode(char c)
{
    if ((c >= 'A') && (c <= 'Z'))
    {
        return (byte)((c - 'A') + 1);
    }
    else if (c == ' ')
    {
        return 0x20;
    }
    else //number 0 thru 9
    {
        return (byte)c;
    }
}

void Insert_ACIdent_DI170()
{
    byte c[8];

    for (int x = 0; x < 8; x++) c[x] = ICAOencode(ACIdent[x]);

    _MSG[index] = (byte)((c[0] & 0x3F) << 2);
    _MSG[index] = (byte)(_MSG[index] | (byte)((c[1] & 0x30) >> 4));
    index++;

    _MSG[index] = (byte)((c[1] & 0x0F) << 4);
    _MSG[index] = (byte)(_MSG[index] | (byte)((c[2] & 0x3C) >> 2));
    index++;

    _MSG[index] = (byte)((c[2] & 0x03) << 6);
    _MSG[index] = (byte)(_MSG[index] | c[3]);
    index++;

    _MSG[index] = (byte)((c[4] & 0x3F) << 2);
    _MSG[index] = (byte)(_MSG[index] | (c[5] & 0x30) >> 4);
    index++;

    _MSG[index] = (byte)((c[5] & 0x0F) << 4);
    _MSG[index] = (byte)(_MSG[index] | (c[6] & 0x3C) >> 2);
    index++;

    _MSG[index] = (byte)((c[6] & 0x03) << 6);
    _MSG[index] = (byte)(_MSG[index] | (c[7] & 0x3F));
    index++;

}

//HARD CODED
void InsertTargetStatusDI200()
{
    _MSG[index++] = 0b00000000;
}

//HARD CODED
void InsertLinkTechnologyDI210()
{
    _MSG[index++] = 0b00000000;
}

void UpdateMsgLength(int _Length)
{
    int msb = _Length / 256;
    int lsb = _Length - (msb * 256);
    _MSG[1] = (byte)msb;
    _MSG[2] = (byte)lsb;
}



enum MSG_STATE { MANDATORY, OPT, NOTIMPLEMENTED};
struct _MsgDetCAT21
{
    char *name;
    MSG_STATE state;
};
struct _MsgDetCAT21 MsgDetSitaware[] = {
    {"FRN01: I021/010 Data Source Identification", MSG_STATE::MANDATORY},
    {"FRN02: I021/040 Target Report Descriptor", MSG_STATE::MANDATORY},
    {"FRN03: I021/030 Time of Day",MSG_STATE::MANDATORY},
    {"FRN04: I021/130 Position in WGS84 Coords",MSG_STATE::MANDATORY},
    {"FRN05: I021/080 Target Address",MSG_STATE::MANDATORY},
    {"FRN06: I021/140 Geometric Altitude",MSG_STATE::OPT},
    {"FRN07: I021/090 Figure of Merit",MSG_STATE::MANDATORY},
    {"FRN08: I021/210 Link Technology",MSG_STATE::MANDATORY},
    {"FRN09: I021/230 Roll Angle",MSG_STATE::NOTIMPLEMENTED},
    {"FRN10: I021/145 Flight Level",MSG_STATE::OPT},
    {"FRN11: I021/150 Air Speed",MSG_STATE::NOTIMPLEMENTED},
    {"FRN12: I021/151 True Airspeed",MSG_STATE::NOTIMPLEMENTED},
    {"FRN13: I021/152 Magnetic Heading",MSG_STATE::NOTIMPLEMENTED},
    {"FRN14: I021/155 Baro Verical Rate",MSG_STATE::NOTIMPLEMENTED},
    {"FRN15: I021/157 Geom Vertical rate",MSG_STATE::NOTIMPLEMENTED},
    {"FRN16: I021/160 Ground Vector",MSG_STATE::OPT},
    {"FRN17: I021/165 Rate of Turn",MSG_STATE::NOTIMPLEMENTED},
    {"FRN18: I021/170 Target Identification",MSG_STATE::MANDATORY},
    {"FRN19: I021/095 Velocity Accruacy",MSG_STATE::NOTIMPLEMENTED},
    {"FRN20: I021/032 TOD Accuracy",MSG_STATE::NOTIMPLEMENTED},
    {"FRN21: I021/200 Target Status",MSG_STATE::MANDATORY},
    {"FRN22: I021/020 Emitter Catagory",MSG_STATE::MANDATORY},
    {"FRN23: I021/220 Met Report",MSG_STATE::NOTIMPLEMENTED},
    {"FRN24: I021/146 Intermediate State Selected Atitude",MSG_STATE::NOTIMPLEMENTED},
    {"FRN25: I021/148 Final State Selected Atitude",MSG_STATE::NOTIMPLEMENTED},
    {"FRN26: I021/110 Trajectory Intent",MSG_STATE::NOTIMPLEMENTED},
    {"FRN27: I021/070 Mode 3/A",MSG_STATE::OPT},
    {"FRN28: I021/131 Signal Amplitude",MSG_STATE::NOTIMPLEMENTED},
};


struct _MsgDetCAT21 MsgDetModern[] = {
    //NEED to REVIEW MANDATORY/OPTIONAL
    {"FRN01: I021/010 Data Source Identification", MSG_STATE::MANDATORY},
    {"FRN02: I021/040 Target Report Descriptor", MSG_STATE::MANDATORY},
    {"FRN03: I021/161 Track Number",MSG_STATE::MANDATORY},
    {"FRN04: I021/015 Service Identification",MSG_STATE::NOTIMPLEMENTED},
    {"FRN05: I021/071 Time of Applicability for Position",MSG_STATE::NOTIMPLEMENTED},
    {"FRN06: I021/130 Position in WGS84 Coords",MSG_STATE::MANDATORY},  //3 byte version
    {"FRN07: I021/131 Position in WGS84 Coords-High Res",MSG_STATE::OPT},  //3 byte version
    {"FRN08: I021/071 Time of Applicability for Velocity",MSG_STATE::NOTIMPLEMENTED},
    {"FRN09: I021/150 Air Speed",MSG_STATE::NOTIMPLEMENTED},
    {"FRN10: I021/151 True Airspeed",MSG_STATE::NOTIMPLEMENTED},
    {"FRN11: I021/080 Target Address",MSG_STATE::MANDATORY},
    {"FRN12: I021/073 Time of Msg Reception of Position",MSG_STATE::MANDATORY},
    {"FRN13: I021/074 Time of Msg Reception of Position - High Precision",MSG_STATE::NOTIMPLEMENTED},
    {"FRN14: I021/075 Time of Msg Reception of Velocity",MSG_STATE::MANDATORY},
    {"FRN15: I021/076 Time of Msg Reception of Velocity - High Precision",MSG_STATE::NOTIMPLEMENTED},
    {"FRN16: I021/140 Geometric Altitude",MSG_STATE::MANDATORY},
    {"FRN17: I021/090 Quality Indicators",MSG_STATE::NOTIMPLEMENTED},
    {"FRN18: I021/210 MOPS Version",MSG_STATE::NOTIMPLEMENTED},
    {"FRN19: I021/070 Mode 3/A",MSG_STATE::OPT},
    {"FRN20: I021/230 Roll Angle",MSG_STATE::NOTIMPLEMENTED},
    {"FRN21: I021/145 Flight Level",MSG_STATE::MANDATORY},
    {"FRN22: I021/152 Magnetic Heading",MSG_STATE::NOTIMPLEMENTED},
    {"FRN23: I021/200 Target Status",MSG_STATE::NOTIMPLEMENTED},
    {"FRN24: I021/155 Baro Verical Rate",MSG_STATE::MANDATORY},
    {"FRN25: I021/157 Geom Vertical rate",MSG_STATE::NOTIMPLEMENTED},
    {"FRN26: I021/160 Ground Vector",MSG_STATE::MANDATORY},
    {"FRN27: I021/165 Track Angle Rate",MSG_STATE::NOTIMPLEMENTED},
    {"FRN28: I021/077 Time of Report Transmission",MSG_STATE::NOTIMPLEMENTED},
    {"FRN29: I021/170 Target Identification",MSG_STATE::MANDATORY},
    {"FRN30: I021/020 Emitter Catagory",MSG_STATE::MANDATORY},
    {"FRN31: I021/220 Met Report",MSG_STATE::NOTIMPLEMENTED},
    {"FRN32: I021/146 Intermediate State Selected Atitude",MSG_STATE::NOTIMPLEMENTED},
    {"FRN33: I021/148 Final State Selected Atitude",MSG_STATE::NOTIMPLEMENTED},
    {"FRN34: I021/110 Trajectory Intent",MSG_STATE::NOTIMPLEMENTED},
    {"FRN35: I021/016 Service Management",MSG_STATE::NOTIMPLEMENTED},
    {"FRN36: I021/008 Aircraft Operational Status",MSG_STATE::NOTIMPLEMENTED},
    {"FRN37: I021/271 Surface Capabilities and Characteristics",MSG_STATE::NOTIMPLEMENTED},
    {"FRN38: I021/132 Message Amplitude",MSG_STATE::NOTIMPLEMENTED},
    {"FRN39: I021/250 Mode S MB Data",MSG_STATE::NOTIMPLEMENTED},
    {"FRN40: I021/260 ACAS Resolution Advisory Report",MSG_STATE::NOTIMPLEMENTED},
    {"FRN41: I021/400 Receiver ID",MSG_STATE::NOTIMPLEMENTED},
    {"FRN42: I021/295 Data Ages",MSG_STATE::NOTIMPLEMENTED}
};


int NumFRN;
int numMandatoryItems;
int numOptionalItems;

std::vector<int> OptionalItemsToSend;

void SetupModernCAT21()
{
    NumFRN = 42;
    numMandatoryItems = 13;
    numOptionalItems = 3;
}
void CreateCAT21Modern() //this is compliant to V2.X
{
    SetupModernCAT21();
    //this CAT21 is complaint to V0.26
    index = 0; //reset the index
    _MSG[index++] = 21;     //ASTERIX CAT 21
    _MSG[index++] = 0;      //LEN MSB
    _MSG[index++] = 0;      //LEN LSB

    //build and insert the FSPEC
    int FSPEC[7] = { 0,0,0,0,0,0,0 };

    //Add the mandatory items to FSPEC
    for (int x = 0; x < NumFRN; x++)
    {
        int frn = x + 1; //x is zero indexed, FRNs start at 1
        if (MsgDetModern[x].state == MANDATORY)
        {
            int index = (frn - 1) / 7;
            FSPEC[index] |= m_FRN[frn];
            //printf("Adding FRN %d\r\n", frn);
        }
    }

    //Add the selected optional items to FSPEC
    {
        //std::unique_lock<std::mutex> lck(mtx);
        std::lock_guard<std::mutex> lck(mtx);
        for each (int x in OptionalItemsToSend)
        {
            int frn = x + 1; //x is zero indexed, FRNs start at 1
            int index = (frn - 1) / 7;
            FSPEC[index] |= m_FRN[frn];
            //printf("Adding Optional FRN %d\r\n", frn);
        }
    }

    //add the extension bit if required
    if (FSPEC[6] != 0)
    {
        printf("FSPEC[6]: %02X\r\n", FSPEC[6]);
        FSPEC[5] |= FRNFX;
        FSPEC[4] |= FRNFX;
        FSPEC[3] |= FRNFX;
        FSPEC[2] |= FRNFX;
        FSPEC[1] |= FRNFX;
        FSPEC[0] |= FRNFX;
    }
    else if (FSPEC[5] != 0)
    {
        printf("FSPEC[5]: %02X\r\n", FSPEC[5]);
        FSPEC[4] |= FRNFX;
        FSPEC[3] |= FRNFX;
        FSPEC[2] |= FRNFX;
        FSPEC[1] |= FRNFX;
        FSPEC[0] |= FRNFX;
    }
    else if (FSPEC[4] != 0)
    {
        printf("FSPEC[4]: %02X\r\n", FSPEC[4]);
        FSPEC[3] |= FRNFX;
        FSPEC[2] |= FRNFX;
        FSPEC[1] |= FRNFX;
        FSPEC[0] |= FRNFX;
    }
    else if (FSPEC[3] != 0)
    {
        printf("FSPEC[3]: %02X\r\n", FSPEC[3]);
        FSPEC[2] |= FRNFX;
        FSPEC[1] |= FRNFX;
        FSPEC[0] |= FRNFX;
    }
    else if (FSPEC[2] != 0)
    {
        printf("FSPEC[2]: %02X\r\n", FSPEC[2]);
        FSPEC[1] |= FRNFX;
        FSPEC[0] |= FRNFX;
    }
    else if (FSPEC[1] != 0)
    {
        printf("FSPEC[1]: %02X\r\n", FSPEC[1]);
        FSPEC[0] |= FRNFX;
    }

    _MSG[index++] = FSPEC[0];
    if (FSPEC[0] & FRNFX) _MSG[index++] = FSPEC[1];
    if (FSPEC[1] & FRNFX) _MSG[index++] = FSPEC[2];
    if (FSPEC[2] & FRNFX) _MSG[index++] = FSPEC[3];
    if (FSPEC[3] & FRNFX) _MSG[index++] = FSPEC[4];
    if (FSPEC[4] & FRNFX) _MSG[index++] = FSPEC[5];
    if (FSPEC[5] & FRNFX) _MSG[index++] = FSPEC[6];

    InsertSAC_SIC_DI010();

    InsertTargetReportDescriptorDI040();

    InsertTrackNumberDI161();

    InsertPositionDI130_3BytePos();

    InsertACAddress_DI80();

    InsertTimeOfMsgReceptionDI073();//073 Time of MSG Reception for Position

    InsertTimeOfMsgReceptionDI075();//075 Time of MSG Reception for Velocity

    InsertGeoHeightDI140();

    InsertFLDI145();

    InsertBarometricVerticalRateDI155();

    InsertAirborneGroundVectorDI160();

    Insert_ACIdent_DI170();

    InsertEmitterCategoryDI020();

    UpdateMsgLength(index);
}




void SetupSITAWARECAT21()
{
    NumFRN = 28;
    numMandatoryItems = 10;
    numOptionalItems = 4;
}
void CreateCAT21SITAWARE()
{
    SetupSITAWARECAT21();
    //this CAT21 is complaint to V0.26
    index = 0; //reset the index
    _MSG[index++] = 21;     //ASTERIX CAT 21
    _MSG[index++] = 0;      //LEN MSB
    _MSG[index++] = 0;      //LEN LSB

    //build and insert the FSPEC
    int FSPEC[6] = { 0,0,0,0,0,0 };

    //Add the mandatory items to FSPEC
    for (int x = 0; x < NumFRN; x++)
    {
        int frn = x + 1; //x is zero indexed, FRNs start at 1
        if (MsgDetSitaware[x].state == MANDATORY)
        {
            int index = (frn - 1) / 7;
            FSPEC[index] |= m_FRN[frn];
        }
    }

    //Add the selected optional items to FSPEC
    {
        //std::unique_lock<std::mutex> lck(mtx);
        std::lock_guard<std::mutex> lck(mtx);
        for each (int x in OptionalItemsToSend) 
        {
            int frn = x + 1; //x is zero indexed, FRNs start at 1
            int index = (frn - 1) / 7;
            FSPEC[index] |= m_FRN[frn];
        }
    }

    //add the extension bit if required
    if (FSPEC[5] != 0)
    {
        FSPEC[4] |= FRNFX;
        FSPEC[3] |= FRNFX;
        FSPEC[2] |= FRNFX;
        FSPEC[1] |= FRNFX;
        FSPEC[0] |= FRNFX;
    }
    else if (FSPEC[4] != 0)
    {
        FSPEC[3] |= FRNFX;
        FSPEC[2] |= FRNFX;
        FSPEC[1] |= FRNFX;
        FSPEC[0] |= FRNFX;
    }
    else if (FSPEC[3] != 0)
    {
        FSPEC[2] |= FRNFX;
        FSPEC[1] |= FRNFX;
        FSPEC[0] |= FRNFX;
    }
    else if (FSPEC[2] != 0)
    {
        FSPEC[1] |= FRNFX;
        FSPEC[0] |= FRNFX;
    }
    else if (FSPEC[1] != 0)
    {
        FSPEC[0] |= FRNFX;
    }

    _MSG[index++] = FSPEC[0];
    if (FSPEC[0] & FRNFX) _MSG[index++] = FSPEC[1];
    if (FSPEC[1] & FRNFX) _MSG[index++] = FSPEC[2];
    if (FSPEC[2] & FRNFX) _MSG[index++] = FSPEC[3];
    if (FSPEC[3] & FRNFX) _MSG[index++] = FSPEC[4]; //this will never happen with this implementation. FSPEC[4] only contains RE and SP

    InsertSAC_SIC_DI010(); //FRN 1 Mandatory
    InsertTargetReportDescriptorDI040(); //FRN 2 M
    InsertTimeOfDayDI030(); //FRN 3 M
    InsertPositionDI130_4BytePos();   //FRN 4 M
    InsertACAddress_DI80(); // FRN 5 M

    if (FSPEC[0] & FRN6) InsertGeoHeightDI140();  // FRN 6 Optional

    InsertFOMDI090();   //FRN 7 M
    InsertLinkTechnologyDI210();    // FRN 8 M

    if (FSPEC[1] & FRN10) InsertFLDI145();// FRN 10 Optional
    if (FSPEC[2] & FRN16) InsertAirborneGroundVectorDI160();// FRN 16 Optional

    Insert_ACIdent_DI170(); //FRN 18 M
    InsertTargetStatusDI200(); // FRN 21 M
    InsertEmitterCategoryDI020(); //FRN 22 

    if (FSPEC[3] & FRN27) InsertMode3ADI070();

    UpdateMsgLength(index);
}

void CreateAndSendCAT21()
{
    char buf[300];
    SYSTEMTIME st;
    GetSystemTime(&st);


    if (bMODERN) 
    {
        CreateCAT21Modern();
        SendUDP((char*)_MSG, index);
        Cat21MsgCount++;
        sprintf_s(buf,"Sending CAT21 V2+ #%02d at time %02d:%02d:%02d.%d\r\n", Cat21MsgCount, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        pme(buf);
        if (bDebug) printf(buf);
    }
    else {
        CreateCAT21SITAWARE();
        SendUDP((char*)_MSG, index);
        Cat21MsgCount++;
        sprintf_s(buf, "Sending CAT21 V0.26 #%02d at time %02d:%02d:%02d.%d\r\n", Cat21MsgCount, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        pme(buf);
        if (bDebug) printf(buf);
        }

    std::string s;
    //Print HEX Dump of last ASX Message
    for (int x = 0; x < index; x++)
    {
        sprintf_s(buf, "%02X", _MSG[x]);
        s += buf;
        if (!((x + 1) % 8)) s += " ";
        if (!((x + 1) % 32)) s += "\r\n";
    }
    s += "\r\n";
    pme(s.c_str());
}




//GEODESIC CODE

#define METER_PER_NM 1852
double a = 6378137, f = 1 / 298.257223563; /* WGS84 */
struct geod_geodesic g;

double EndPoint(double lat1, double lon1, double azi1, double s12, double& lat2, double& lon2)  //s12 is in metres
{
    geod_init(&g, a, f);
    double azi2;
    geod_direct(&g, lat1, lon1, azi1, s12, &lat2, &lon2, &azi2);
    //if (bDebug) printf("Direct: Lat:%.15f Lon:%.15f Hdg:%.15f\n", lat2, lon2, azi2);
    return azi2;
}

double Distance(double velInKnots, double travelTimeInSeconds) //vel in knots (1 Kt=1852 m/hr) or 30.8666m/minute or .5144444m per second
{
    return travelTimeInSeconds * velInKnots * METER_PER_NM / 3600;
}



void drawADSB()
{
    ImGui::Begin("ADSB");
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImGui::Checkbox("Pause", &bPause);
    ImGui::Checkbox("Debug", &bDebug);


    char buf[200];
    sprintf_s(buf, "Sending CAT21 on %s:%d", ASXIP, ASXPort);
    ImGui::Text(buf);

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 2); ImGui::DragInt("Send Interval [1-10 sec]", &sendInterval, 0.25f, 1, 10, "%d", ImGuiSliderFlags_None);

    sprintf_s(buf, "Send Period %d secs", sendInterval);
    ImGui::Text(buf);


    sprintf_s(buf, "Aircraft Update Count:%d", updateCounter);
    ImGui::Text(buf);
    sprintf_s(buf, "CAT21 Message Count:%d", Cat21MsgCount);
    ImGui::Text(buf);

    ImGui::Separator();
    sprintf_s(buf, "ACIdent: %s  ICAO: %06X, Mode3: %04o ", ACIdent, ICAO, Mode3);
    ImGui::Text(buf);
    sprintf_s(buf, "Aircraft Pos: %06.4f  %07.4f", latitude,longitude);
    ImGui::Text(buf);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6); ImGui::DragInt("Alt [ft]", &alt, 1.0f, 0, 60000, "%d", ImGuiSliderFlags_None);

    ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4); ImGui::DragInt("Delta Alt [ft per sec]", &deltaAlt, 3.0f, -1000, 1000, "%d", ImGuiSliderFlags_None);


    static int spd_i = speedInKnots;
    int preSpd = spd_i;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4); ImGui::DragInt("Speed [knts]]", &spd_i, 0.75f, 0, 700, "%d", ImGuiSliderFlags_None);
    if (preSpd != spd_i) speedInKnots = (double)spd_i;

    int hdg_i = (int)heading;
    ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::GetFontSize() * 2); ImGui::DragInt("Hdg [deg]", &hdg_i, 0.25f, 0, 359, "%d", ImGuiSliderFlags_None);
    heading = (double)hdg_i;


    static int drag_i = deltaHdg;
    int preDH = drag_i;
    ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::GetFontSize() * 2); ImGui::DragInt("Delta Hdg [deg per sec]", &drag_i, 0.05f, -10, 10, "%d", ImGuiSliderFlags_None);
    if (preDH != drag_i) deltaHdg = (double)drag_i;


    ImGui::Separator();

    ImGui::Checkbox("Modern CAT21", &bMODERN);
    ImGui::SameLine();
    if (bMODERN) {
        ImGui::Text("Current Version ASX CAT21 V2.6 (Modern)");
        SetupModernCAT21();
    }
    else {
        ImGui::Text("Current Version ASX CAT21 V0.26 (SITAWARE Compliant)");
        SetupSITAWARECAT21();
    }

    static bool bShowMANDATORY = false;
    ImGui::Checkbox("Show Mandatory", &bShowMANDATORY);
    ImGui::SameLine();
    static bool bShowNOTIMP = false;
    ImGui::Checkbox("Show Not Implemented", &bShowNOTIMP);
    static bool b[28] = { false, false, false, false, false,
                          false, false, false, false, false,
                          false, false, false, false, false,
                          false, false, false, false, false,
                          false, false, false, false, false,
                          false, false, false };

    std::lock_guard<std::mutex> lck(mtx);
    OptionalItemsToSend.clear();

    if (!bMODERN)
    {
        for (int x = 0; x < NumFRN; x++)
        {
            if ((MsgDetSitaware[x].state == MANDATORY) && bShowMANDATORY)
            {
                ImGui::BeginDisabled();
                b[x] = true;
                ImGui::Checkbox(MsgDetSitaware[x].name, &b[x]);
                ImGui::EndDisabled();
            }
            else if (MsgDetSitaware[x].state == OPT)
            {
                ImGui::Checkbox(MsgDetSitaware[x].name, &b[x]);
                if (b[x] == true)  OptionalItemsToSend.push_back(x); //x is zero index, associated FRN is 1 index
            }

            else if ((MsgDetSitaware[x].state == NOTIMPLEMENTED) && bShowNOTIMP)
            {
                ImGui::BeginDisabled();
                b[x] = false;
                ImGui::Checkbox(MsgDetSitaware[x].name, &b[x]);
                ImGui::EndDisabled();
            }
        }
    }
    else //show modern CAT21 DIs
    {
        for (int x = 0; x < NumFRN; x++)
        {
            if ((MsgDetModern[x].state == MANDATORY) && bShowMANDATORY)
            {
                ImGui::BeginDisabled();
                b[x] = true;
                ImGui::Checkbox(MsgDetModern[x].name, &b[x]);
                ImGui::EndDisabled();
            }
            else if (MsgDetModern[x].state == OPT)
            {
                ImGui::Checkbox(MsgDetModern[x].name, &b[x]);
                if (b[x] == true)  OptionalItemsToSend.push_back(x); //x is zero index, associated FRN is 1 index
            }

            else if ((MsgDetModern[x].state == NOTIMPLEMENTED) && bShowNOTIMP)
            {
                ImGui::BeginDisabled();
                b[x] = false;
                ImGui::Checkbox(MsgDetModern[x].name, &b[x]);
                ImGui::EndDisabled();
            }
        }

    }

        
    ImGui::Separator();
    if (Cat21MsgCount > 0)
    {
        ImGui::Text("Last Message sent [Hex]:");
        std::string s;
        char buf[10];
        for (int x = 0; x < index; x++)
        {
            sprintf_s(buf, "%02X", _MSG[x]);
            s += buf;
            if (!((x+1)%8)) s += " ";
            if (!((x+1) % 32)) s += "\r\n";
        }
        ImGui::Text(s.c_str());            
    }
    
    
    ImGui::End();

    static bool dOpen = true;

    DebugWindow(&dOpen);

}


