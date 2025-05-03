//-----------------------------------------------------------------------------
//  Comm.cpp
//  Brain-dead serial port wrapper for Win32
//  Copyright Eric Honsch.  Free for any one to use as they see fit
//-----------------------------------------------------------------------------
#include "stdafx.h"
#include "Comm.h"

#include <stdio.h>

//-----------------------------------------------------------------------------
CommChannel::CommChannel(void)
{
    mFile = NULL;
}

//-----------------------------------------------------------------------------
CommChannel::~CommChannel()
{
    Close();
}

//-----------------------------------------------------------------------------
bool CommChannel::Open(std::string port, int baudrate, int bits, int parity, int stop)
{

    // Communications
    //SetComm  ...

    char fname[11];
    sprintf_s(fname, sizeof(fname), "\\\\.\\%s", port.c_str());
    //sprintf_s(fname, sizeof(fname), "COM%d", port);
    mFile = CreateFileA(fname, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (mFile == INVALID_HANDLE_VALUE)
    {
        unsigned long err = GetLastError();
        char errorstr[100];
        sprintf(errorstr, "Error %d opening COM port: %s\n", err, port.c_str());
        OutputDebugStringA(errorstr);
        mFile = NULL;
        return false;
    } 

//    COMMCONFIG cc;
    // DCB
    DCB dcb;
    memset(&dcb, 0, sizeof(DCB));
    bool x = GetCommState(mFile, &dcb);

    dcb.DCBlength = sizeof(dcb);
    dcb.BaudRate = baudrate;             // current baud rate 
    dcb.fBinary = TRUE;                 // binary mode, no EOF check 
    dcb.fParity = FALSE;                // enable parity checking 
    dcb.fOutxCtsFlow = FALSE;                // CTS output flow control 
    dcb.fOutxDsrFlow = FALSE;                // DSR output flow control 
    dcb.fDtrControl = DTR_CONTROL_ENABLE;  // DTR flow control type 
//    dcb.fDsrSensitivity     = FALSE;                // DSR sensitivity 
//    dcb.fTXContinueOnXoff   = FALSE;                // XOFF continues Tx 
//    dcb.fOutX               = FALSE;                // XON/XOFF out flow control 
//    dcb.fInX                = FALSE;                // XON/XOFF in flow control 
//    dcb.fErrorChar          = FALSE;                // enable error replacement 
//    dcb.fNull               = FALSE;                // enable null stripping 
    dcb.fRtsControl = RTS_CONTROL_DISABLE;                // RTS flow control 
//    dcb.fAbortOnError       = FALSE;                // abort reads/writes on error 
 //   dcb.XonLim              = 100;                 // transmit XON threshold 
//    dcb.XoffLim             = 100;                 // transmit XOFF threshold 
    dcb.ByteSize = bits;                 // number of bits/byte, 4-8 
    dcb.Parity = PARITY_NONE;               // 0-4=no,odd,even,mark,space 
    dcb.StopBits = ONESTOPBIT;           // 0,1,2 = 1, 1.5, 2 
//    dcb.XonChar             = 0;                    // Tx and Rx XON character 
 //   dcb.XoffChar            = 0;                    // Tx and Rx XOFF character 
//    dcb.ErrorChar           = 0;                    // error replacement character 
//    dcb.EofChar             = 0;                    // end of input character 
//    dcb.EvtChar             = 0;                    // received event character 


    if (!SetCommState(mFile, &dcb))
    {
        DWORD err = GetLastError();
        char debugstr[100];

        sprintf_s(debugstr, sizeof(debugstr), "SetComm error: %d:\n", err);
        OutputDebugStringA(debugstr);
        return false;
    }
    COMMTIMEOUTS ct = { 0 };
    ct.ReadIntervalTimeout = MAXDWORD;
    ct.ReadTotalTimeoutMultiplier = 0;
    ct.ReadTotalTimeoutConstant = 0;
    ct.WriteTotalTimeoutMultiplier = 0;
    ct.WriteTotalTimeoutConstant = 0;


  
    SetCommTimeouts(mFile, &ct);
    // flush the port
    PurgeComm(mFile, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT);
    EscapeCommFunction(mFile, SETDTR);
    EscapeCommFunction(mFile, SETRTS);
    return true;
}

//-----------------------------------------------------------------------------
void CommChannel::Close(void)
{
    if (mFile == NULL) return;

    CloseHandle(mFile);
    mFile = NULL;
}

//-----------------------------------------------------------------------------
void CommChannel::Purge(void)
{
    if (mFile == NULL) return;

    if (PurgeComm(mFile, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT))
        return; 
    DWORD err = GetLastError();
    char debugstr[100];

    sprintf_s(debugstr, sizeof(debugstr), "Purgecomm error: %d:\n", err);
    OutputDebugStringA(debugstr);

}

//-----------------------------------------------------------------------------
int CommChannel::Send(int numbytes, unsigned char *data)
{
    if (mFile == NULL) return -1;

    DWORD dwError = 0;
    COMSTAT comstat;
    if (!ClearCommError(mFile, &dwError, &comstat))
    {
        DWORD err = GetLastError();
        char debugstr[100];

        sprintf_s(debugstr, sizeof(debugstr), "Send ClearCommError: %d:\n", err);
        OutputDebugStringA(debugstr);
        // if there was an error return nothing written
        return -1;

    }

    unsigned long written = 0;
    if (!WriteFile(mFile, data, numbytes, &written, NULL))
    {
        DWORD err = GetLastError();
        char debugstr[100];

        sprintf_s(debugstr, sizeof(debugstr), "Send error: %d:\n", err);
        OutputDebugStringA(debugstr);
        // if there was an error return nothing written
        return -1;
    }
    return (int) written;
}

//-----------------------------------------------------------------------------
int CommChannel::BytesAvailable()
{
    if (mFile == NULL) return -1;

     DWORD dwError = 0;
    COMSTAT comstat;
    if (!ClearCommError(mFile, &dwError, &comstat))
    {
        DWORD err = GetLastError();
        char debugstr[100];

        sprintf_s(debugstr, sizeof(debugstr), "BytesAvailable ClearCommError: %d:\n", err);
        OutputDebugStringA(debugstr);
        // if there was an error return nothing available
        return 0;
    }
    return comstat.cbInQue;
}


//-----------------------------------------------------------------------------
int CommChannel::Receive(unsigned char *inbyte)
{
    (*inbyte) = 0;    
    if (mFile == NULL) return -1;

    
    DWORD dwError = 0;
    COMSTAT comstat;
    if (!ClearCommError(mFile, &dwError, &comstat))
    {
        DWORD err = GetLastError();
        char debugstr[100];

        sprintf_s(debugstr, sizeof(debugstr), "Read ClearCommError: %d:\n", err);
        OutputDebugStringA(debugstr);
        // if there was an error return nothing read
        return 0;
    }
    if (comstat.cbInQue < 1)
        return 0;   //No data in buffer
    
    // Communications
    unsigned long read = 0;
    if (!ReadFile(mFile, inbyte, 1, &read, NULL))
    {
        DWORD err = GetLastError();
        char debugstr[100];

        sprintf_s(debugstr, sizeof(debugstr), "Read error: %d:\n", err);
        OutputDebugStringA(debugstr);
        // if there was an error return nothing written
        return 0;
    }
    return (int) read;
}
//-----------------------------------------------------------------------------
//Receive multiple bytes at time. Not faster than 1 byte/time but more complicated
//-----------------------------------------------------------------------------

int CommChannel::ReceiveMulti(unsigned char *inbuff, int bytecount)
{
    if (mFile == NULL) return -1;

    DWORD dwError = 0;
    COMSTAT comstat;
    ClearCommError(mFile, &dwError, &comstat);
    int quelen = comstat.cbInQue;
    if (quelen <1)
        return 0;   //No data in buffer
    // Communications

    int getbytes = min(quelen, bytecount);
    unsigned long read = 0;
    int err = ReadFile(mFile, inbuff, getbytes, &read, NULL);

    // if there is an error return nothing read
    if (err == 0) return 0;

    return (int)read;
}

//-----------------------------------------------------------------------------
void CommChannel::SetDTR(BOOL enable)
{
    if (enable) EscapeCommFunction(mFile, SETDTR);
    else        EscapeCommFunction(mFile, CLRDTR);
}

