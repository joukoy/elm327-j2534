#include "stdafx.h"
#include "elm327-comm.h"
#include "Comm.h"
#include <queue>
#include <string>
#include "SafeQueue.hpp"
#include "elm327_frontend.h"
#include <regex>
#include <algorithm>

#define PAD 0xAA
//#define PAD 0x00

std::string ComPort;
int Baudrate;

CommChannel Serial;
extern ChannelConfig channels[MAX_CHANNELS];
std::vector<PeriodicMsg> periodicmessages1;
std::vector<PeriodicMsg> periodicmessages2;
SafeQueue<canmsg> OutgoingMessages;
SafeQueue<elm327Response>Responses;
SafeQueue<PASSTHRU_MSG> ReceivedIsoTpMessages;
bool Connected = false;
int readTimeout = 2000;
int writeTimeout = 1000;
int CurrentProtocol;
std::string currentHeader = "unset";
HANDLE bgTask;

elm327Comm::elm327Comm(void)
{

}
elm327Comm::~elm327Comm(void)
{
    Connected = false;
}
std::vector<std::string> split(const std::string& s, char seperator)
{
    std::vector<std::string> output;

    std::string::size_type prev_pos = 0, pos = 0;

    while ((pos = s.find(seperator, pos)) != std::string::npos)
    {
        std::string substring(s.substr(prev_pos, pos - prev_pos));

        output.push_back(substring);

        prev_pos = ++pos;
    }

    output.push_back(s.substr(prev_pos, pos - prev_pos)); // Last word

    return output;
}
bool Endswith(std::string const& fullString, std::string const& ending) {
    if (fullString.length() >= ending.length()) {
        return (0 == fullString.compare(fullString.length() - ending.length(), ending.length(), ending));
    }
    else {
        return false;
    }
}
bool isHex(std::string const& str)
{
    if (std::all_of(str.begin(), str.end(), ::isxdigit)) {
        return true;
    }
    return false;
}

std::string ToHex(byte * pMsg, int size)
{
    const char* hexChar = "0123456789ABCDEF";
    std::string message;
    for (int i = 0; i < size; ++i)
    {
        message += hexChar[(pMsg[i] >> 4) & 0x0f];
        message += hexChar[(pMsg[i]) & 0x0f];
        message += ' ';
    }
    return message;
}
int charToint(char input)
{
    if (input >= '0' && input <= '9')
        return input - '0';
    if (input >= 'A' && input <= 'F')
        return input - 'A' + 10;
    if (input >= 'a' && input <= 'f')
        return input - 'a' + 10;
    throw std::invalid_argument("Invalid input string");
}

std::vector<byte> HexToBytes(std::string hexdata)
{
    std::vector<byte> bytes;

    for (unsigned int i = 0; i < hexdata.length(); i += 2) {
        std::string byteString = hexdata.substr(i, 2);
        char byte = (char)strtol(byteString.c_str(), NULL, 16);
        bytes.push_back(byte);
    }

    return bytes;
    /*
    std::vector<byte> bytes;
    for (int i = 0; i < (hexdata.length() -1); i = i + 2)
    {
        bytes.push_back(charToint(hexdata.c_str()[i]) * 16 + charToint(hexdata.c_str()[i + 1])); //Int32ToByte(Rmessagebyte, 16);
    }
    return bytes;
    */
}

template <typename I> std::string n2hexstr(I w, size_t hex_len = sizeof(I) << 1) {
    static const char* digits = "0123456789ABCDEF";
    std::string rc(hex_len, '0');
    for (size_t i = 0, j = (hex_len - 1) * 4; i < hex_len; ++i, j -= 4)
        rc[i] = digits[(w >> j) & 0x0f];
    return rc;
}

void elm327Comm::StartPeriodicMessages()
{
    DWORD dwExitCode;

    GetExitCodeThread(bgTask, &dwExitCode);
    if (dwExitCode != STILL_ACTIVE) {
        bgTask = CreateThread(0, 0, &static_SendPeriodcMessages, 0, 0, 0);
    }

}
void elm327Comm::EnqueuePassthruMsg(PASSTHRU_MSG pMsg)
{
    ReceivedIsoTpMessages.Produce(std::move(pMsg));
    //OutputDebugStringA((std::string("Queue message, cmd: ") + n2hexstr(messagecommand) + "\n").c_str());
}

PASSTHRU_MSG elm327Comm::ReceiveIsoTpMessage(int timeout)
{
    PASSTHRU_MSG pMsg;
    pMsg.DataSize = 0;
    uint64_t starttime = current_time_ms();
    for (;;)
    {
        Receive(1, timeout);
        if (ReceivedIsoTpMessages.Size() > 0)
        {
            //OutputDebugStringA((std::string("Receive message, cmd: ") + n2hexstr(messagecommand) + "\n").c_str());
            ReceivedIsoTpMessages.Consume(pMsg);
            break;
        }
        Sleep(1);
        if ((current_time_ms() - starttime) > timeout)
        {
            break;
        }
    }
    return pMsg;
}

int elm327Comm::ClearBuffer()
{
    int retval = ReceivedIsoTpMessages.Size();
    ReceivedIsoTpMessages.Clear();
    return retval;
}

int elm327Comm::ArrayToInt(uint8_t* buf, int offset)
{
    int retval = (uint32_t)buf[offset] << 24;
    retval |= (uint32_t)buf[offset + 1] << 16;
    retval |= (uint32_t)buf[offset + 2] << 8;
    retval |= (uint32_t)buf[offset + 3];
    return retval;
}
void elm327Comm::UintToArray(unsigned long val, uint8_t* buf)
{
    buf[0] = (uint8_t)((unsigned long)val >> 24);
    buf[1] = (uint8_t)((unsigned long)val >> 16);
    buf[2] = (uint8_t)((unsigned long)val >> 8);
    buf[3] = (uint8_t)val;
}

bool elm327Comm::cksumok(uint8_t* buf)
{
    byte cksum = 0;
    for (int b = 0;b < CKSUM_BYTE;b++)
    {
        cksum += buf[b];
    }
    if (buf[CKSUM_BYTE] == cksum)
        return true;
    else
        return false;
}

uint64_t elm327Comm::current_time_ms() 
{
    uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch())
        .count();
    return ms;
}

void elm327Comm::Stopelm327Comm()
{
    Connected = false;
}


double elm327Comm::ReadVoltage()
{
    std::string response = SendRequest("AT RV", true).Data;

    if (response != "")
    {
        std::vector<std::string> parts = split(response, ' ');
        double batVolts = std::stod(parts.back());
        return batVolts;
    }
    return 0;
}


int elm327Comm::elm327RemoveFilter(uint32_t FilterId, uint8_t bus)
{
    return true;
}


uint32_t elm327Comm::elm327SetFilter(UINT32 Filter, UINT32 Flow, UINT32 Mask, uint8_t bus)
{
    return 1;
}
int elm327Comm::elm327RemoveFilters(uint8_t bus)
{
    return 0;
}

bool elm327Comm::elm327SendMsg(canmsg Msg, int timeout)
{
    SetWriteTimeout(timeout);
    PASSTHRU_MSG message;
    UintToArray(Msg.MsgId, message.Data);
    memcpy(message.Data + 4, Msg.data, 8);
    return SendPassthruMessage(&message, 1);
}

void elm327Comm::SendPeriodicMessages()
{
    while (Connected)
    {
        if (periodicmessages1.size() == 0 && periodicmessages2.size() == 0)
        {
            break;
        }
        for (int i = 0; i < periodicmessages1.size();i++)
        {
            PeriodicMsg pmsg = periodicmessages1.at(i);
            if ((current_time_ms() - pmsg.LastMessageTime) > pmsg.interval)
            {
                elm327SendMsg(pmsg.Msg, 0);
                pmsg.LastMessageTime = current_time_ms();
            }
        }
        for (int i = 0; i < periodicmessages2.size();i++)
        {
            PeriodicMsg pmsg = periodicmessages2.at(i);
            if ((current_time_ms() - pmsg.LastMessageTime) > pmsg.interval)
            {
                elm327SendMsg(pmsg.Msg, 0);
                pmsg.LastMessageTime = current_time_ms();
            }
        }
        Sleep(10);
    }
}

bool
isNullOrWhiteSpace(std::string const& str)
{
    return std::find_if(
        str.begin(),
        str.end(),
        [](unsigned char ch) { return !isspace(ch); })
        == str.end();
}
inline void ltrim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
        }));
}

// trim from end (in place)
inline void rtrim(std::string& s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
        }).base(), s.end());
}

inline void trim(std::string& s) {
    rtrim(s);
    ltrim(s);
}


/// <summary>
/// Set the timeout to the device. If this is set too low, the device
/// will return 'No Data'. ELM327 is limited to 1020 milliseconds maximum.
/// </summary>
bool elm327Comm::SetReadTimeout(int milliseconds)
{
    if (readTimeout == milliseconds)
    {
        return true;
    }
    int parameter = (std::min)((std::max)(1, (milliseconds / 4)), 255);
    std::string value = n2hexstr(parameter,2);
    return SendAndVerify("AT ST " + value, "OK");
}

/// <summary>
/// Set the timeout to the device. If this is set too low, the device
/// will return 'No Data'. ELM327 is limited to 1020 milliseconds maximum.
/// </summary>
bool elm327Comm::SetWriteTimeout(int milliseconds)
{
    writeTimeout = milliseconds;
    return true;
}

/// <summary>
/// Reads and filters a line from the device, returns it as a string
/// </summary>
/// <remarks>
/// Strips Non Printable, >, and Line Feeds. Converts Carriage Return to Space. Strips leading and trailing whitespace.
/// </remarks>
SerialString elm327Comm::ReadELMLine(bool MultiLine, int timeout)
{
    // (MaxReceiveSize * 2) + 2 for Checksum + longest possible prompt. Minimum prompt 2 CR + 1 Prompt, +2 extra
    int maxPayload = (MaxReceiveSize * 2) + 7;
    // A buffer to receive a single byte.
    uint8_t b0;
    bool Prompt = false;
    SerialString builtString;
    builtString.TimeStamps.push_back(current_time_ms());
    uint64_t starttime = current_time_ms();
    while (true)
    {
        if ((current_time_ms() - starttime) > timeout)
        {
            builtString.Data = "";
            return builtString;
        }
        // Receive a single byte.
        int byteCount = Serial.Receive(&b0);
        if (byteCount == 0)
        {
            Sleep(1);
            continue;
        }

        // Is it the prompt '>'.
        if (b0 == '>') // || buffer.Data[0] == '?')
        {
            // Prompt found, we're done.
            //Debug.WriteLine("Elm prompt: " + builtString.ToString());
            OutputDebugStringA("Elm promt received\n");
            Prompt = true;
            break;
        }

        // Is it a CR
        if (b0 == 13)
        {
            if (MultiLine)
            {
                //Handle multiple lines as one message
                // CR found, replace with space.
                b0 = 32;
                builtString.TimeStamps.push_back(current_time_ms());
            }
            else if (builtString.Data.size() > 0 && !isNullOrWhiteSpace(builtString.Data))
            {
                break;
            }

        }
        // Printable characters only.
        if (b0 >= 32 && b0 <= 126)
        {
            // Append it to builtString.
            builtString.Data += b0;
        }
    }

    // trim and return
    trim(builtString.Data);
    OutputDebugStringA((std::string("Elm line: ") + builtString.Data + "\n").c_str());
    return builtString;
}

SerialString elm327Comm::SendRequest(std::string request, bool getresponse)
{
    OutputDebugStringA((std::string( "TX: ") + request + std::string(", time: ") + std::to_string(current_time_ms())+"\n").c_str());
    Serial.Purge();
    std::string tmp = request + " \r";
    Serial.Send(tmp.size() , (unsigned char*)tmp.c_str());
    if (getresponse)
    {
        return ReadELMLine(true, writeTimeout);
    }
    else
    {
        SerialString empty;
        return empty;
    }
}
/// <summary>
/// Send a command to the device, confirm that we got the response we expect. 
/// </summary>
/// <remarks>
/// This is primarily for use in the Initialize method, where we're talking to the 
/// interface device rather than the PCM.
/// </remarks>
bool elm327Comm::SendAndVerify(std::string message, std::string expectedResponse)
{
    std::string actualResponse = SendRequest(message, true).Data;

    if (actualResponse == expectedResponse)
    {
        OutputDebugStringA(actualResponse.c_str());
        return true;
    }

    OutputDebugStringA((std::string("Did not recieve expected response. ") + actualResponse + std::string(" does not equal ") + expectedResponse).c_str());
    return false;
}
int elm327Comm::ConnectProtocol(int Protocol, int Bauds)
{
    CurrentProtocol = Protocol;

    OutputDebugStringA(SendRequest("AT E0", true).Data.c_str()); // disable echo
    OutputDebugStringA(SendRequest("AT S0", true).Data.c_str()); // no spaces on responses


    if (Protocol == ISO15765)
    {
        if (!SendAndVerify("AT AL", "OK") ||
            !SendAndVerify("AT SP6", "OK") ||              // Set Protocol 6 (CAN)
            !SendAndVerify("AT DP", "ISO 15765-4 (CAN 11/500)") ||    // Get Protocol (Verify 15765-4)
            !SendAndVerify("AT AR", "OK") ||               // Turn Auto Receive on (default should be on anyway)
            !SendAndVerify("AT AT0", "OK") ||              // Disable adaptive timeouts
            !SendAndVerify("AT SH7E0" , "OK") || // Set header
            !SendAndVerify("AT CF5E0" , "OK") ||     //Set filter id
            !SendAndVerify("AT CM5F0", "OK") ||     //Set filter mask
//            !SendAndVerify("AT CF5E8", "OK") ||     //Set filter id
//            !SendAndVerify("AT CM5FF", "OK") ||     //Set filter mask
            !SendAndVerify("AT H1", "OK") ||               // Send headers
            !SendAndVerify("AT CAF0", "OK") ||               // Don't format isotp messages automatically                       
            !SendAndVerify("AT FCSM0", "OK") ||         //Automatically send flow control messages
            !SendAndVerify("AT CFC1", "OK") ||          //Automatically send flow control messages
            !SendAndVerify("AT ST 20", "OK"))             // Set timeout (will be adjusted later, too)                 
        {
            return ERR_FAILED;
        }
    }
    else if (Protocol == J1850VPW)
    {
        if (!SendAndVerify("AT AL", "OK") ||               // Allow Long packets
            !SendAndVerify("AT SP2", "OK") ||              // Set Protocol 2 (VPW)
            !SendAndVerify("AT DP", "SAE J1850 VPW") ||    // Get Protocol (Verify VPW)
            !SendAndVerify("AT AR", "OK") ||               // Turn Auto Receive on (default should be on anyway)
            !SendAndVerify("AT AT0", "OK") ||              // Disable adaptive timeouts
            !SendAndVerify("AT H1", "OK") ||              // Send headers
            !SendAndVerify("AT ST 20", "OK")               // Set timeout (will be adjusted later, too)                 
            )
        {
            OutputDebugStringA("Initialization error, see debug log for details");
            return ERR_FAILED;
        }
    }
    else
    {
        return ERR_INVALID_PROTOCOL_ID;
    }
    return STATUS_NOERROR;
}

int elm327Comm::Startelm327Comm()
{
    Serial.Close();
    if (!Serial.Open(ComPort, Baudrate, 8, 0, 1))
    {
        return ERR_DEVICE_IN_USE;
    }
    if (SendRequest("ATZ", true).Data == "")
    {
        return ERR_FAILED;
    }
    Connected = true;

    //_beginthread(elm327Comm::static_ReceiveMessages, 0, NULL);
    return 0;
}

bool elm327Comm::isISO15765Frame(uint32_t canId, uint8_t databyte0, uint8_t length) 
{
    if (length < 1 || length > 8) return false;

    if ((canId >= 0x7E0 && canId <= 0x7EF) || (canId >= 0x5E0 && canId <= 0x5EF))
    {
        uint8_t pci = databyte0 & 0xF0;
        if (databyte0 == 0)
            return false;
        switch (pci) {
        case 0x00: // Single Frame
            return (databyte0 & 0x0F) <= 7;
        case 0x10: // First Frame
        case 0x20: // Consecutive Frame
        case 0x30: // Flow Control
            return true;
        default:
            return false;
        }
    }
    else
    {
        return false;
    }
}

/// <summary>
/// Process responses from the ELM/ST devices.
/// </summary>
bool elm327Comm::ProcessResponse(SerialString rawResponse, std::string context, bool allowEmpty)
{
    if (rawResponse.Data == "OK")
    {
        return true;
    }

    if (rawResponse.Prompt && (rawResponse.Data.size() < 3 || rawResponse.Data.find("STOPPED") != std::string::npos))
    {
        //We have received prompt
        //OutputDebugStringA((std::string("Processresponse with prompt: ") + rawResponse.Data +"\n").c_str());
        // OBDMessage response = new OBDMessage(new byte[0]);
        //response.ElmLine = rawResponse.Data;
        //if (rawResponse.Data.StartsWith("6C") || rawResponse.Data.StartsWith("8C"))
          //  response = new OBDMessage(rawResponse.Data.ToBytes());
        //response.TimeStamp = rawResponse.TimeStamp;
        //response.ElmPrompt = true;
        //this.enqueue(response, true);
        return false;
    }

    if (isNullOrWhiteSpace(rawResponse.Data))
    {
        if (allowEmpty)
        {
            return true;
        }
        else
        {
            OutputDebugStringA((std::string( "Empty response to ") + context + std::string(". That's not OK.")).c_str());
            return false;
        }
    }


    //#define SEPARATORS "<"
    // We sent successfully, but the PCM didn't reply immediately.
    if (rawResponse.Data == "NO DATA")
    {
        std::string tmp = "00\r";
        Serial.Send(tmp.size(), (unsigned char*)tmp.c_str());
        return true;
    }
    
    std::string data = std::regex_replace(rawResponse.Data, std::regex("BUFFER FULL"), "");
    data = std::regex_replace(data, std::regex("OUT OF MEMORY"), "");
    data = std::regex_replace(data, std::regex("<RX ERROR"), "");
    std::vector<std::string> segments = split(data, '>');
    for (int i=0; i< segments.size(); i++)
    {
        std::string segment = segments.at(i);
        int s = 0;
        int totalLength = INT32_MAX;
        //string ecuStr = CanParameters.CanPCM.ResID.ToString("X"); //0x7E8
        //string ecuStr2 = CanParameters.CanPCM.DiagID.ToString("X"); //0x5E8
        std::vector<byte> isotpData;

        std::vector<std::string> hexResponses = split(segment, ' ');
        for (int j=0;j<hexResponses.size();j++)
        {
            std::string singleHexResponse = hexResponses.at(j);
            if (!isHex(singleHexResponse))
            {
                continue;
            }
            std::vector<byte> deviceResponseBytes;
            PASSTHRU_MSG pMsg;
            if (CurrentProtocol == ISO15765)
            {
                std::string hexdata = "0" + singleHexResponse;
                deviceResponseBytes = HexToBytes(hexdata);
                if (deviceResponseBytes.size() < 5)
                    continue;
                uint16_t canId = (uint16_t)((deviceResponseBytes.at(0) << 8) | deviceResponseBytes.at(1));
                //if (singleHexResponse.rfind("5E8", 0) == 0 || singleHexResponse.rfind("7E8", 0) == 0 || deviceResponseBytes.size() > 12)
                if (isISO15765Frame(canId, deviceResponseBytes.at(2), deviceResponseBytes.size() - 2))
                {
                    //Parse ISOTP message
                    byte pci = deviceResponseBytes.at(2);
                    if ((pci & 0xF0) == 0x10)  // First frame
                    {
                        totalLength = deviceResponseBytes.at(3);
                        OutputDebugStringA((std::string( "[First Frame]: ") +  hexdata + ", Total data length : " + std::to_string(totalLength) + "\n").c_str());
                        for (int i = 4; i < deviceResponseBytes.size(); i++) 
                            isotpData.push_back(deviceResponseBytes.at(i));
                        continue;
                    }
                    else if ((pci & 0xF0) == 0x20)  // Consecutive frame
                    {
                        OutputDebugStringA((std::string("[Next Frame]: ") + hexdata + "\n").c_str());
                        for (int i = 3; i < deviceResponseBytes.size() && isotpData.size() < totalLength; i++) 
                            isotpData.push_back(deviceResponseBytes.at(i));
                        if (isotpData.size() < totalLength)
                        {
                            //More data coming
                            continue;
                        }
                    }
                    else if ((pci & 0xF0) == 0) // Single frame
                    {
                        OutputDebugStringA((std::string("[Single Frame]: ") + hexdata + "\n").c_str());
                        totalLength = pci & 0x0F;
                        for (int i = 3; i < deviceResponseBytes.size() && isotpData.size() < totalLength; i++) 
                            isotpData.push_back(deviceResponseBytes.at(i));
                    }
                    deviceResponseBytes.clear();
                    deviceResponseBytes.push_back(0);
                    deviceResponseBytes.push_back(0);
                    deviceResponseBytes.push_back((byte)(canId >> 8));
                    deviceResponseBytes.push_back((byte)canId);
                    for (int i = 0; i < isotpData.size(); i++)
                        deviceResponseBytes.push_back(isotpData.at(i));
                }
                else 
                {
                    //Single frame/full isotp message
                    deviceResponseBytes = HexToBytes("00000" + singleHexResponse);
                }
            }
            else if (CurrentProtocol == J1850VPW)
            {
                deviceResponseBytes = HexToBytes(singleHexResponse);
                if (deviceResponseBytes.size() > 0 )
                {
                    deviceResponseBytes.pop_back(); // remove checksum byte
                }
            }
            pMsg.DataSize = deviceResponseBytes.size();
            for (int d = 0;d < deviceResponseBytes.size();d++)
                pMsg.Data[d] = deviceResponseBytes.at(d);
            if (rawResponse.TimeStamps.size() > s)
                pMsg.Timestamp = rawResponse.TimeStamps.at(s);
            else
                pMsg.Timestamp = rawResponse.TimeStamps.back();
            EnqueuePassthruMsg(pMsg);
            OutputDebugStringA((std::string("RX: ") + ToHex(pMsg.Data, pMsg.DataSize) + "\n").c_str());
            //response.ElmPrompt = rawResponse.Prompt;
            s++;
        }
        return true;

        if (Endswith(segment, "OK"))
        {
            OutputDebugStringA("WTF: Response not valid, but ends with OK.\n");
            return true;
        }

        OutputDebugStringA(
            (std::string( "Unexpected response to ") + context + ": " + segment + "\n").c_str());
    }

    return false;
}


std::vector<std::string> elm327Comm::BuildIsoTpFrames(byte *fullMessage, int fullsize)
{
    std::vector<std::string> frames;
    byte *message = fullMessage + 4;
    int size = fullsize - 4;
    OutputDebugStringA((std::string("IsoTP message, length: ") + std::to_string(size) + "\n").c_str());
    if (size <= 7)
    {
        // Single Frame
        byte frame[8];
        frame[0] = (byte)(0x00 | size); // SF | length
        memcpy(frame + 1, message, size);
        for (int i = 1 + size; i < 8; i++)
            frame[i] = PAD; // AA padding
        std::string frameStr = ToHex(frame,8);
        frames.push_back(frameStr);
    }
    else
    {
        // Multi-Frame
        OutputDebugStringA("Multi-frame\n");
        int totalLength = size;
        int numConsecutiveFrames = (totalLength - 6 + 6) / 7;

        // First Frame
        byte firstFrame[8];
        firstFrame[0] = (byte)(0x10 | ((totalLength >> 8) & 0x0F)); // FF | high nibble of length
        firstFrame[1] = (byte)(totalLength & 0xFF); // low byte of length
        memcpy(firstFrame + 2, message, 6);
        std::string frameStr = ToHex(firstFrame, 8);
        frames.push_back(frameStr);

        // Consecutive Frames
        int offset = 6;
        for (int frameNumber = 1; offset < totalLength; frameNumber++)
        {
            int chunkSize = 7;
            if ((totalLength - offset) < 7)
                chunkSize = totalLength - offset;
            //byte[] cf = new byte[chunkSize +1];
            byte cf[8];
            cf[0] = (byte)(0x20 | (frameNumber & 0x0F)); // CF | sequence number
            memcpy(cf + 1, message, chunkSize);
            for (int j = 1 + chunkSize; j < 8; j++)
                cf[j] = PAD; // AA padding
            std::string cfStr = ToHex(cf, 8);
            frames.push_back(cfStr);
            offset += chunkSize;
        }
    }

    return frames;
}
/// <summary>
/// Separate the message into header and payload.
/// </summary>
void elm327Comm::ParseMessage(byte *messageBytes, int messagelen, std::string *header, std::string *payload)
{
    // The incoming byte array needs to separated into header and payload portions,
    // which are sent separately.
    std::string hexRequest = ToHex(messageBytes, messagelen);
    if (CurrentProtocol == J1850VPW)
    {
        if (hexRequest.length() > 9)
        {
            *header = hexRequest.substr(0, 9);
            *payload = hexRequest.substr(9);
        }
        else
        {
            *header = "";
            *payload = "";
        }
    }
    else
    {
        //uint16_t devid = (uint16_t)(messageBytes[2] << 8 | messageBytes[3]);
        //*header = n2hexstr(devid);
        *header = hexRequest.substr(7, 4);
        *payload = hexRequest.substr(12);
    }
}

/// <summary>
/// Send a message, do not expect a response.
/// </summary>
bool elm327Comm::SendPassthruMessage(PASSTHRU_MSG *message, int responses)
{
    std::string header;
    std::string payload;
    ParseMessage(message->Data, message->DataSize, &header, &payload);
    bool getResponse = true;
    if (responses < 1)
        getResponse = false;

    Serial.Purge();
    if (header != currentHeader)
    {
        SerialString setHeaderResponse = SendRequest("AT SH " + header, getResponse);
        OutputDebugStringA((std::string( "Set header response (1): ") + setHeaderResponse.Data + std::string( ", time: ") + std::to_string(current_time_ms())+"\n").c_str());

        for (int retry = 0; retry < 5; retry++)
        {
            if (setHeaderResponse.Data == "OK")
            {
                break;
            }
            else
            {
                // Does it help to retry once?
                setHeaderResponse = SendRequest("AT SH " + header, true);
                OutputDebugStringA((std::string("Set header response (") + std::to_string(retry + 2) + std::string("): ") + setHeaderResponse.Data + std::string(", time: ") + std::to_string(current_time_ms())+"\n").c_str());
            }
        }
        if (!ProcessResponse(setHeaderResponse, "set-header command", !getResponse))
        {
            return false;
        }
        currentHeader = header;
    }

    if (CurrentProtocol == ISO15765)
    {
        std::vector<std::string> frames = BuildIsoTpFrames(message->Data, message->DataSize);

        SerialString sendMessageResponse;
        for (int s=0; s< frames.size();s++)
        {
            std::string msg = frames.at(s);
            payload = std::regex_replace(msg, std::regex(" "), "");
            //Debug.WriteLine("TX: " + payload + ", time: " + DateTime.Now.ToString("HH.mm.ss.ffff"));
            sendMessageResponse = SendRequest(payload, true);
        }
        if (!ProcessResponse(sendMessageResponse, "message content", !getResponse))
        {
            return false;
        }
    }
    else
    {
        payload = std::regex_replace(payload, std::regex(" "), "");
        OutputDebugStringA((std::string("TX: ") + payload + std::string(", time: ") + std::to_string(current_time_ms())+"\n").c_str());
        SerialString sendMessageResponse = SendRequest(payload + " ", getResponse);
        if (!ProcessResponse(sendMessageResponse, "message content", !getResponse))
        {
            return false;
        }
    }
    return true;
}

/// <summary>
/// Try to read an incoming message from the device.
/// </summary>
/// <returns></returns>
void elm327Comm::Receive(int NumMessages, int timeout)
{
    //SetReadTimeout(timeout);

    for (int m = 0; m < NumMessages; m++)
    {
        if (timeout > 0 || Serial.BytesAvailable() > 3)
        {
            SerialString response = ReadELMLine(false, timeout);
            //OutputDebugStringA((std::string("Elm line: ") + response.Data+"\n").c_str());
            ProcessResponse(response, "receive");
        }
        else
        {
            return;
        }
    }
}
