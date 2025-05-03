//-----------------------------------------------------------------------------
//  Comm.h
//  Brain-dead serial port wrapper for Win32
//  Copyright Eric Honsch.  Free for any one to use as they see fit
//-----------------------------------------------------------------------------


#ifndef COMM_H
#define COMM_H

#ifndef _WINDOWS_
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif
#include "elm327_debug.h"

//extern void ShowDebug(char* debugstr);

//-----------------------------------------------------------------------------
class CommChannel 
{
  public:

    CommChannel(void);
   ~CommChannel();

    bool Open(std::string port, int baudrate, int bits, int parity, int stop);
    void Close(void);

    int  Send(int numbytes, unsigned char *data);
    int  Receive(unsigned char *inbyte);
    int  ReceiveMulti(unsigned char* inbuff, int bytecount);
    int  BytesAvailable();
    void SetDTR(BOOL enable);
    void Purge(void);

  private:

    HANDLE mFile;
};


#endif // COM_H