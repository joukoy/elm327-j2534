/*
**
** Copyright (C) 2009 Drew Technologies Inc.
** Author: Joey Oravec <joravec@drewtech.com>
**
** This library is free software; you can redistribute it and/or modify
** it under the terms of the GNU Lesser General Public License as published
** by the Free Software Foundation, either version 3 of the License, or (at
** your option) any later version.
**
** This library is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Lesser General Public License for more details.
**
** You should have received a copy of the GNU Lesser General Public
** License along with this library; if not, <http://www.gnu.org/licenses/>.
**
*/

#include "stdafx.h"
#include <windows.h>
#include <tchar.h>

#include "j2534_v0404.h"
#include "elm327_debug.h"
#include "elm327_loader.h"
#include "elm327_output.h"
#include "elm327_frontend.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <afxdisp.h>
#include "CMessages.h"
#include <vector>
#include <queue>
#include <stdint.h>
#include "elm327-comm.h"


#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include "isotphandler.h"

const char* hexChar = "0123456789ABCDEF";
using namespace std;
//string message;
int Readreply = 0;
std::string PathOfDll;

void AppendLog(std::string Txt);
BOOL isOpen = false;
BOOL endApp = false;

int DeviceSerial = 12345678;
int CableID = 12345678;

ChannelConfig channels[MAX_CHANNELS];
extern std::vector<PeriodicMsg> periodicmessages1;
extern std::vector<PeriodicMsg> periodicmessages2;
elm327Comm elm327;

string thisDllDirPath()
{
	CStringW thisPath = L"";
	WCHAR path[MAX_PATH];
	HMODULE hm;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPWSTR)&thisDllDirPath, &hm))
	{
		GetModuleFileNameW(hm, path, MAX_PATH);
		PathRemoveFileSpecW(path);
		thisPath = CStringW(path);
		if (!thisPath.IsEmpty() &&
			thisPath.GetAt(thisPath.GetLength() - 1) != '\\')
			thisPath += L"\\";
	}
	//	else if (_DEBUG) std::wcout << L"GetModuleHandle Error: " << GetLastError() << std::endl;

	//	if (_DEBUG) std::wcout << L"thisDllDirPath: [" << CStringW::PCXSTR(thisPath) << L"]" << std::endl;
	return { thisPath.GetString(), thisPath.GetString() + thisPath.GetLength() };
}

vector<string> split(string str, string token) {
	vector<string>result;
	while (str.size()) {
		int index = str.find(token);
		if (index != string::npos) {
			result.push_back(str.substr(0, index));
			str = str.substr(index + token.size());
			if (str.size() == 0)result.push_back(str);
		}
		else {
			result.push_back(str);
			str = "";
		}
	}
	return result;
}


int char2int(char input)
{
	if (input >= '0' && input <= '9')
		return input - '0';
	if (input >= 'A' && input <= 'F')
		return input - 'A' + 10;
	if (input >= 'a' && input <= 'f')
		return input - 'a' + 10;
	throw std::invalid_argument("Invalid input string");
}



#define SHIM_CHECK_DLL() \
{ \
	if (! shim_checkAndAutoload()) \
	{ \
       	shim_setInternalError(_T("PassThruShim has not loaded a J2534 DLL")); \
		dbug_printretval(ERR_FAILED); \
		return ERR_FAILED; \
	} \
}
 

EXTERN_DLL_EXPORT long J2534_API PassThruWriteToLogA(char *szMsg)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	CStringW cstrMsg(szMsg);

	dtDebug(_T("%.3fs ** '%s'\n"), GetTimeSinceInit(), cstrMsg);

	return STATUS_NOERROR;
}

EXTERN_DLL_EXPORT long J2534_API PassThruWriteToLogW(wchar_t *szMsg)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());

	dtDebug(_T("%.3fs ** '%s'\n"), GetTimeSinceInit(), szMsg);

	return STATUS_NOERROR;
}

EXTERN_DLL_EXPORT long J2534_API PassThruSaveLog(char *szFilename)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	auto_lock lock;

	shim_clearInternalError();

	dtDebug(_T("%.3fs ++ PTSaveLog(%s)\n"), GetTimeSinceInit(), (szFilename==NULL)?_T("*NULL*"):_T("")/*pName*/);

	CStringW cstrFilename(szFilename);
	shim_writeLogfile(cstrFilename, false);

	dbug_printretval(STATUS_NOERROR);
	return STATUS_NOERROR;
}

EXTERN_DLL_EXPORT long J2534_API PassThruOpen(void *pName, unsigned long *pDeviceID)
{
	PathOfDll = thisDllDirPath();
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	auto_lock lock;
	unsigned long retval= 0;

	if (!shim_checkAndAutoload())
	{
		shim_setInternalError(_T("PassThruShim has not loaded a J2534 DLL"));
		dbug_printretval(ERR_FAILED);
		return ERR_FAILED;
	}

	retval = elm327.Startelm327Comm();
	if (*pDeviceID > 0x0F)
		*pDeviceID = 1;
	dtDebug(_T("%.3fs ++ PTOpen(%s, Device ID %1d)  "), GetTimeSinceInit(), (pName == NULL) ? _T("*NULL*") : pName, *pDeviceID);
	//	dtDebug(_T("Returning DeviceID: %ld\n"), *pDeviceID);
	isOpen = true;
	dbug_printretval(retval);
	return retval;
}

EXTERN_DLL_EXPORT long J2534_API PassThruClose(unsigned long DeviceID)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	auto_lock lock;
	long retval = 0;
	elm327.Stopelm327Comm();
	shim_clearInternalError();
	dtDebug(_T("%.3fs -- PTClose(%ld)\n"), GetTimeSinceInit(), DeviceID);
	dbug_printretval(retval);
	isOpen = false;
	return retval;
}
uint8_t GetSpeedByte(int baudrate)
{	
	switch (baudrate)
	{
	case 4096:
		return 0;
	case 5000:
		return 1;
	case 10000:
		return 2;
	case 20000:
		return 3;
	case 31250:
		return 4;
	case 33333:
		return 5;
	case 40000:
		return 6;
	case 50000:
		return 7;
	case 80000:
		return 8;
	case 100000:
		return 9;
	case 125000:
		return 10;
	case 200000:
		return 12;
	case 500000:
		return 13;
	case 1000000:
		return 14;
	default:
		return 13;
	}
}
EXTERN_DLL_EXPORT long J2534_API PassThruConnect(unsigned long DeviceID, unsigned long ProtocolID, unsigned long Flags, unsigned long Baudrate, unsigned long *pChannelID)
{
	
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	auto_lock lock;
	long retval;
	
	if (!isOpen)
	{
		return ERR_DEVICE_NOT_CONNECTED;
	}

	shim_clearInternalError();
	dbug_printcflag(Flags);
	retval = 0;
	*pChannelID = 1;
	if (ProtocolID >= 0x8000)
		*pChannelID = 2;
	channels[*pChannelID-1].Protocol = ProtocolID;
	retval = elm327.ConnectProtocol(ProtocolID, Baudrate);

	//ChannelId = *pChannelID;
	dtDebug(_T("%.3fs ++ PTConnect(Decive #%ld, %s, 0x%08X, %ld, Channel #%1d)\n"), GetTimeSinceInit(), DeviceID, dbug_prot(ProtocolID).c_str(), Flags, Baudrate, *pChannelID);
	//*pChannelID =  ChannelId;
	if (pChannelID == NULL)
		dtDebug(_T("  pChannelID was NULL\n"));
	else
		dtDebug(_T("  returning ChannelID: %ld\n"), *pChannelID);
	dbug_printretval(retval);
	return retval;
}

EXTERN_DLL_EXPORT long J2534_API PassThruDisconnect(unsigned long ChannelID)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	auto_lock lock;
	long retval;
	if (!isOpen)
	{
		return ERR_DEVICE_NOT_CONNECTED;
	}
	channels[ChannelID].Protocol = -1;	//Release channel
	dtDebug(_T("%.3fs -- PTDisconnect(%ld)\n"), GetTimeSinceInit(), ChannelID);
	retval = 0;
	canmsg cMsg;
	cMsg.size = (uint8_t)ChannelID;
	elm327.elm327SendMsg(cMsg, 10);

	return retval;
}

EXTERN_DLL_EXPORT long J2534_API PassThruReadMsgs(unsigned long ChannelID, PASSTHRU_MSG * pMsg, unsigned long* pNumMsgs, unsigned long Timeout)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	auto_lock lock;
	long retval = ERR_INVALID_CHANNEL_ID;
	unsigned long reqNumMsgs = 0;
	int msgCount = 0;
	long starttime = GetTickCount();
	if (pNumMsgs != NULL) reqNumMsgs = *pNumMsgs;
	if (!isOpen)
	{
		return ERR_DEVICE_NOT_CONNECTED;
	}

	retval = ERR_INVALID_CHANNEL_ID; // _PassThruReadMsgs(ChannelID, pMsg, pNumMsgs, Timeout);
	retval = 0;
	int msgNum = 0;
	*pNumMsgs = 0;
	for (;;)
	{
		PASSTHRU_MSG Msg = elm327.ReceiveIsoTpMessage(Timeout);
		//PASSTHRU_MSG Msg = elm327.ReceiveIsoTpMessage(1000);
		if (Msg.DataSize == 0)
		{
			if (msgNum == 0)
			{
				*pNumMsgs = 0;
				return ERR_BUFFER_EMPTY;
			}
		}
		else
		{
			pMsg[msgNum].ProtocolID = channels[ChannelID].Protocol;
			pMsg[msgNum].RxStatus = 0;
			pMsg[msgNum].TxFlags = 0;
			pMsg[msgNum].DataSize = Msg.DataSize;
			memcpy(pMsg[msgNum].Data, Msg.Data, Msg.DataSize);
			msgNum++;
			*pNumMsgs = msgNum;
		}
		if (msgNum >= reqNumMsgs)
		{
			break;
		}
		if ((GetTickCount() - starttime) > Timeout)
		{
			if (msgNum == 1)
			{
				retval = ERR_TIMEOUT;
			}
			break;
		}
	}
	return retval;
}

EXTERN_DLL_EXPORT long J2534_API PassThruWriteMsgs(unsigned long ChannelID, PASSTHRU_MSG* pMsg, unsigned long* pNumMsgs, unsigned long Timeout)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	auto_lock lock;
	long retval = STATUS_NOERROR;
	unsigned long reqNumMsgs = *pNumMsgs;
	string Numofmsgs = "Number of messages is:" + std::to_string(reqNumMsgs);
	if (!isOpen)
	{
		return ERR_DEVICE_NOT_CONNECTED;
	}

	shim_clearInternalError();
	dtDebug(_T("%.3fs >> PTWriteMsgs(Channel #%ld,  %1d message(s), %ldms timeout)\n"), GetTimeSinceInit(), ChannelID, *pNumMsgs, Timeout);
	if (pNumMsgs != NULL)
		reqNumMsgs = *pNumMsgs;
	dbug_printmsg(pMsg, _T("Msg"), pNumMsgs, true);
	for (int m = 0;m < reqNumMsgs;m++)
	{
		if (channels[ChannelID].Protocol == CAN && pMsg->DataSize < 13) //Max size: 4 + 8 (id+data)
		{
			//Send raw CAN message
			canmsg cMsg;
			cMsg.MsgId = elm327.ArrayToInt(pMsg[m].Data, 0);
			cMsg.size = pMsg[m].DataSize - 4;
			memcpy(cMsg.data, pMsg[m].Data + 4, 8);
			elm327.elm327SendMsg(cMsg, Timeout);
		}
		else
		{
			elm327.SendPassthruMessage(&pMsg[m], 1);
		}
	}
	return retval;
}

EXTERN_DLL_EXPORT long J2534_API PassThruStartPeriodicMsg(unsigned long ChannelID, PASSTHRU_MSG *pMsg,
                      unsigned long *pMsgID, unsigned long TimeInterval)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	auto_lock lock;
	long retval;
	if (!isOpen)
	{
		return ERR_DEVICE_NOT_CONNECTED;
	}
	if (ChannelID == 1)
	{
		PeriodicMsg pmsg;
		pmsg.interval = TimeInterval;
		pmsg.LastMessageTime = 0;
		pmsg.Id = periodicmessages1.size() + 1;
		periodicmessages1.push_back(pmsg);
		elm327.StartPeriodicMessages();
		*pMsgID = pmsg.Id;
	}
	else
	{
		PeriodicMsg pmsg;
		pmsg.interval = TimeInterval;
		pmsg.LastMessageTime = 0;
		pmsg.Id = periodicmessages2.size() + 1;
		periodicmessages2.push_back(pmsg);
		*pMsgID = pmsg.Id;

	}
	shim_clearInternalError();
	dtDebug(_T("%.3fs ++ PTStartPeriodicMsg(%ld, 0x%08X, 0x%08X, %ld)\n"), GetTimeSinceInit(), ChannelID, pMsg, pMsgID, TimeInterval);
//	SHIM_CHECK_DLL();
// SHIM_CHECK_FUNCTION(_PassThruStartPeriodicMsg);
	dbug_printmsg(pMsg, _T("Msg"), 1, true);
	retval = 0;
	if (pMsgID != NULL)
		dtDebug(_T("  returning PeriodicID: %ld\n"), *pMsgID);

	dbug_printretval(retval);
	return retval;
}

EXTERN_DLL_EXPORT long J2534_API PassThruStopPeriodicMsg(unsigned long ChannelID, unsigned long MsgID)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	auto_lock lock;
	long retval;
	if (!isOpen)
	{
		return ERR_DEVICE_NOT_CONNECTED;
	}

	if (ChannelID == 1)
	{
		for (int i = 0; i < periodicmessages1.size();i++)
		{
			PeriodicMsg pmsg = periodicmessages1.at(i);
			if (pmsg.Id == MsgID)
			{
				periodicmessages1.erase(periodicmessages1.begin() + i);
				break;
			}
		}
	}
	else
	{
		for (int i = 0; i < periodicmessages2.size();i++)
		{
			PeriodicMsg pmsg = periodicmessages2.at(i);
			if (pmsg.Id == MsgID)
			{
				periodicmessages2.erase(periodicmessages2.begin() + i);
				break;
			}
		}
	}
	shim_clearInternalError();
	dtDebug(_T("%.3fs -- PTStopPeriodicMsg(%ld, %ld)\n"), GetTimeSinceInit(), ChannelID, MsgID);

	retval = 0;

	dbug_printretval(retval);
	return retval;
}

EXTERN_DLL_EXPORT long J2534_API PassThruStartMsgFilter(unsigned long ChannelID,
                      unsigned long FilterType, PASSTHRU_MSG *pMaskMsg, PASSTHRU_MSG *pPatternMsg,
					  PASSTHRU_MSG *pFlowControlMsg, unsigned long *pMsgID)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	auto_lock lock;
	long retval = 0;
	if (!isOpen)
	{
		return ERR_DEVICE_NOT_CONNECTED;
	}

	shim_clearInternalError();
	dtDebug(_T("%.3fs ++ PTStartMsgFilter(%ld, %s, 0x%08X, 0x%08X, 0x%08X, 0x%08X)\n"), GetTimeSinceInit(), ChannelID, dbug_filter(FilterType).c_str(),
		pMaskMsg, pPatternMsg, pFlowControlMsg, pMsgID);
//	SHIM_CHECK_DLL();
//	SHIM_CHECK_FUNCTION(_PassThruStartMsgFilter);

	UINT32 filter = elm327.ArrayToInt(pPatternMsg->Data, 0);
	UINT32 mask = elm327.ArrayToInt(pMaskMsg->Data, 0);
	UINT32 flow = 0;
	if (pFlowControlMsg != NULL)
	{
		flow = elm327.ArrayToInt(pFlowControlMsg->Data, 0);
	}
	else
	*pMsgID = elm327.elm327SetFilter(filter,  flow, mask, ChannelID);
	dbug_printmsg(pMaskMsg, _T("Mask"), 1, true);
	dbug_printmsg(pPatternMsg, _T("Pattern"), 1, true);
	dbug_printmsg(pFlowControlMsg, _T("FlowControl"), 1, true);
	//*pMsgID = 1;
	if (pMsgID != NULL)
		dtDebug(_T("  returning FilterID: %ld\n"), *pMsgID);
	dbug_printretval(retval);
	return 0;
}

EXTERN_DLL_EXPORT long J2534_API PassThruStopMsgFilter(unsigned long ChannelID, unsigned long MsgID)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	auto_lock lock;
	long retval;
	if (!isOpen)
	{
		return ERR_DEVICE_NOT_CONNECTED;
	}

	elm327.elm327SetFilter(MsgID, 0,0, ChannelID);

	//elm327RemoveFilters();
	shim_clearInternalError();
	dtDebug(_T("%.3fs -- PTStopMsgFilter(%ld, %ld)\n"), GetTimeSinceInit(), ChannelID, MsgID);
	retval = 0;

	dbug_printretval(retval);
	return retval;
}

EXTERN_DLL_EXPORT long J2534_API PassThruSetProgrammingVoltage(unsigned long DeviceID, unsigned long Pin, unsigned long Voltage)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	auto_lock lock;
	long retval;
	string message;
	if (!isOpen)
	{
		return ERR_DEVICE_NOT_CONNECTED;
	}

	shim_clearInternalError();
	dtDebug(_T("%.3fs ** PTSetProgrammingVoltage(%ld, %ld, %ld)\n"), GetTimeSinceInit(), DeviceID, Pin, Voltage);

	switch (Voltage)
	{
	case VOLTAGE_OFF:
		dtDebug(_T("  Pin %ld remove voltage\n"), Pin);
		message = "Voltage" + std::to_string(Pin) + "OFF";
		break;
	case SHORT_TO_GROUND:
		dtDebug(_T("  Pin %ld short to ground\n"), Pin);
		message = "Voltage" + std::to_string(Pin) + "GND";
		break;
	default:
		dtDebug(_T("  Pin %ld at %f Volts\n"), Pin, Voltage / (float) 1000);
		message = "Voltage" + std::to_string(Pin) + "ON";
		break;
	}
	retval = 0;

	dbug_printretval(retval);
	return retval;
}

EXTERN_DLL_EXPORT long J2534_API PassThruReadVersion(unsigned long DeviceID, char *pFirmwareVersion, char *pDllVersion, char *pApiVersion)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	auto_lock lock;
	long retval;

	sprintf(pFirmwareVersion,"4.04");
	sprintf(pDllVersion, "1.01");
	sprintf(pApiVersion, "1.02");

	retval = 0;
	
	shim_clearInternalError();
	dtDebug(_T("%.3fs ** PTReadVersion(Device #%ld, %1dX, %1d, %1d)\n"), GetTimeSinceInit(), DeviceID, *pFirmwareVersion, *pDllVersion, *pApiVersion);

	dbug_printretval(retval);
	return retval;
}


EXTERN_DLL_EXPORT long J2534_API PassThruGetLastError(char *pErrorDescription)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	auto_lock lock;
	long retval;

	// pErrorDescription returns the text description for an error detected
	// during the last function call (EXCEPT PassThruGetLastError). This
	// function should not modify the last internal error

	dtDebug(_T("%.3fs ** PTGetLastError(0x%08X)\n"), GetTimeSinceInit(), pErrorDescription);

	if (pErrorDescription == NULL)
	{
		dtDebug(_T("  pErrorDescription is NULL\n"));
	}

	retval = 0;

	if (pErrorDescription != NULL)
	{
#ifdef UNICODE
		CStringW cstrErrorDescriptionW(pErrorDescription);
		dtDebug(_T("  %s\n"), (LPCWSTR) cstrErrorDescriptionW);
#else
		dtDebug(_T("  %s\n"), pErrorDescription);
#endif
	}

	// Log the return value for this function without using dbg_printretval().
	// Even if an error occured inside this function, the error text was not
	// updated to describe the error.
	dtDebug(_T("  %s\n"), dbug_return(retval).c_str());
	return retval;
}

EXTERN_DLL_EXPORT long J2534_API PassThruIoctl(unsigned long ChannelID, unsigned long IoctlID, void *pInput, void *pOutput)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	auto_lock lock;
	long retval = 0;
	SCONFIG_LIST *pList;
	shim_clearInternalError();
	dtDebug(_T("%.3fs ** PTIoctl(%ld, %s, 0x%08X, 0x%08X)\n"), GetTimeSinceInit(), ChannelID, dbug_ioctl(IoctlID).c_str(), pInput, pOutput);
	dtDebug(_T("Ioct1ID is %1d , pInput is %1d , pOutput is %1d  \n"), IoctlID, pInput, pOutput);
//	SHIM_CHECK_DLL();
//	SHIM_CHECK_FUNCTION(_PassThruIoctl);

	if (!isOpen)
	{
		return ERR_DEVICE_NOT_CONNECTED;
	}
	retval = 0;

	// Print any relevant info before making the call
	switch (IoctlID)
	{
		case GET_CONFIG:
			retval = 0;
			pList = (SCONFIG_LIST*)pInput;
			break;
		case SET_CONFIG:
			pList = (SCONFIG_LIST*)pInput;
			dbug_printsconfig((SCONFIG_LIST *) pInput);
			retval = 0;
			break;
		case READ_VBATT:
			*(int*)pOutput = elm327.ReadVoltage() * 1000;
			break;
		case FIVE_BAUD_INIT:
			//simResult = GetIOCTLvalue("FIVE_BAUD_INIT",pInput, pOutput, &retval);
			dbug_printsbyte((SBYTE_ARRAY*)pInput, _T("Input"));
			break;
		case FAST_INIT:
			//simResult = GetIOCTLvalue("FAST_INIT",pInput, pOutput, &retval);
			dbug_printmsg((PASSTHRU_MSG *) pInput, _T("Input"), 1, true);
			break;
		case CLEAR_TX_BUFFER:
			break;
		case CLEAR_RX_BUFFER:
			//*(int*)pOutput = 
				elm327.ClearBuffer();
			break;
		case CLEAR_PERIODIC_MSGS:
			break;
		case CLEAR_MSG_FILTERS:
			break;
		case CLEAR_FUNCT_MSG_LOOKUP_TABLE:
			break;
		case ADD_TO_FUNCT_MSG_LOOKUP_TABLE:
			dbug_printsbyte((SBYTE_ARRAY *) pInput, _T("Add"));
			break;
		case DELETE_FROM_FUNCT_MSG_LOOKUP_TABLE:
			dbug_printsbyte((SBYTE_ARRAY *) pInput, _T("Delete"));
			break;
		case READ_PROG_VOLTAGE:
			break;
		case SW_CAN_HS:
			break;
		case SW_CAN_NS:
			break;
		case SET_POLL_RESPONSE:
			break;
		case BECOME_MASTER:
			break;
		case START_REPEAT_MESSAGE:
			break;
		case QUERY_REPEAT_MESSAGE:
			break;
		case STOP_REPEAT_MESSAGE:
			break;
		case GET_DEVICE_CONFIG:
			break;
		case SET_DEVICE_CONFIG:
			break;
		case PROTECT_J1939_ADDR:
			break;
		case CLEAR_LAST_USED_DEVICE:
			break;
		case GET_DEVICE_SERIAL_NUMBER:
			break;
		case READ_CABLE_ID:
			break;
		case J1850PWM_TERMINATION:
			break;
		case DISABLE_POP_UPS:
			break;
	}

	return retval;
}

