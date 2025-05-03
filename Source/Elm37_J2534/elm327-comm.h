#pragma once
//#include <cstdint>
#include <windows.h> 
#include "elm327_debug.h"
#include "elm327_output.h"
#include <vector>

#define CANMSGSIZE 15
#define CKSUM_BYTE 14

#define CMD_FILTER_SET 0xF0
#define CMD_FILTER_CLEAR 0xF1
#define CMD_FILTER_CLEAR_SET 0xF2
#define CMD_FILTER_REMOVE 0xF3
#define CMD_CONNECT 0xF4
#define CMD_MSG_SENT 0xF5
#define CMD_READVOLTS 0xF6
#define CMD_DISCONNECT 0xF7

#define CMD_MSG1 0xFD
#define CMD_MSG2 0xFE

#define MaxReceiveSize  200

struct canmsg   
{
	uint8_t size; 
	uint32_t MsgId; 
	uint8_t data[8];
};

struct elm327Response
{
	time_t ReceiveTime;
	uint8_t Cmd;
	UINT32 Value;
	uint8_t Result;
};

struct PeriodicMsg
{
	canmsg Msg;
	int interval = 1000;
	uint32_t LastMessageTime;
	int Id;
};

struct SerialString
{
	std::string Data;
	long TimeStamp;
	bool Prompt;
		//If one string have multiple messages, store timestamps here
	std::vector<long> TimeStamps;
};


class elm327Comm
{
public:

	elm327Comm(void);
	~elm327Comm(void);
	int Startelm327Comm();
	void Stopelm327Comm();
	bool elm327SendMsg(canmsg Msg, int timeout);
	uint32_t elm327SetFilter(UINT32 Filter, UINT32 Flow, UINT32 Mask, uint8_t bus);
	int elm327RemoveFilter(uint32_t FilterId, uint8_t bus);
	int elm327RemoveFilters(uint8_t bus);
	double ReadVoltage();
	void UintToArray(unsigned long val, uint8_t* buf);
	int ArrayToInt(uint8_t* buf, int offset);
	int ConnectProtocol(int Protocol, int Bauds);
	PASSTHRU_MSG ReceiveIsoTpMessage(int timeout);
	bool SendPassthruMessage(PASSTHRU_MSG* message, int responses);
	int ClearBuffer();
	void StartPeriodicMessages();

private:
	bool cksumok(uint8_t* buf);
	uint64_t current_time_ms();
	void SendPeriodicMessages();
	bool isISO15765Frame(uint32_t canId, uint8_t databyte0, uint8_t length);
	SerialString SendRequest(std::string request, bool getresponse);
	SerialString ReadELMLine(bool MultiLine, int timeout);
	bool SendAndVerify(std::string message, std::string expectedResponse);
	bool ProcessResponse(SerialString rawResponse, std::string context, bool allowEmpty = false);
	std::vector<std::string> BuildIsoTpFrames(byte* fullMessage, int fullsize);
	void ParseMessage(byte* messageBytes, int messagelen, std::string* header, std::string* payload);
	void Receive(int NumMessages, int timeout);
	void EnqueuePassthruMsg(PASSTHRU_MSG pMsg);

	static DWORD WINAPI static_SendPeriodcMessages(void* args)
	{
		static_cast<elm327Comm*>(args)->SendPeriodicMessages();
		return 0;
	}

};

