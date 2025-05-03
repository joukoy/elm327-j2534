/**
* @file isotphandler.cpp
* @brief This module will handle isotp communications.
* @details This module sends out isotp requests and processes incoming frames
*          to re-assemble the original message
*/
#include "stdafx.h"
#include "isotphandler.h"
#include "elm327-comm.h"
#include <format>

static IsoTp::ISO_MESSAGE_t isoMessageIncoming; // declare an ISO-TP message
static IsoTp::ISO_MESSAGE_t isoMessageOutgoing; // declare an ISO-TP message
static unsigned long lastMicros;
static uint8_t messagecommand;
SafeQueue<PASSTHRU_MSG> ReceivedIsoTpMessages;

extern elm327Comm elm327;
/**
 * Initializes the isotp subsystem
 * msgcommand: Use this command when sending messages to elm327
 */
void IsoTp::isotp_init(uint8_t msgcommand)
{
	messagecommand = msgcommand;
	isotp_reset();
}

/**
 * Resets the isotp state
 */
void IsoTp::isotp_reset()
{
	isoMessageIncoming.id = isoMessageOutgoing.id = 0xffff;
	isoMessageIncoming.length = isoMessageIncoming.index = isoMessageOutgoing.length = isoMessageOutgoing.index = 0;
}

double micros() {
	uint64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::high_resolution_clock::now().time_since_epoch())
		.count();
	return us;
}

template <typename I> std::string n2hexstr(I w, size_t hex_len = sizeof(I) << 1) {
	static const char* digits = "0123456789ABCDEF";
	std::string rc(hex_len, '0');
	for (size_t i = 0, j = (hex_len - 1) * 4; i < hex_len; ++i, j -= 4)
		rc[i] = digits[(w >> j) & 0x0f];
	return rc;
}
std::string IsoTp::canFrameToString(canmsg* frame)
{
	std::string dataString = n2hexstr(frame->MsgId) + ",";
	for (int i = 0; i < frame->size; i++)
	{
		dataString += n2hexstr(frame->data[i]);
	}
	return dataString;
}

void IsoTp::EnqueueIstopMessage(ISO_MESSAGE_t* Msg)
{
	PASSTHRU_MSG pMsg;
	pMsg.DataSize = Msg->length + 4;
	elm327.UintToArray(Msg->id, pMsg.Data);
	memcpy(pMsg.Data + 4, Msg->data, Msg->length);
	ReceivedIsoTpMessages.Produce(std::move(pMsg));
	//OutputDebugStringA((std::string("Queue message, cmd: ") + n2hexstr(messagecommand) +"\n").c_str());
}

void IsoTp::EnqueuePassthruMsg(PASSTHRU_MSG pMsg)
{
	ReceivedIsoTpMessages.Produce(std::move(pMsg));
	//OutputDebugStringA((std::string("Queue message, cmd: ") + n2hexstr(messagecommand) + "\n").c_str());
}

PASSTHRU_MSG IsoTp::ReceiveIsoTpMessage()
{
	PASSTHRU_MSG pMsg;
	pMsg.DataSize = 0;
	if (ReceivedIsoTpMessages.Size() > 0)
	{
		//OutputDebugStringA((std::string("Receive message, cmd: ") + n2hexstr(messagecommand) + "\n").c_str());
		ReceivedIsoTpMessages.Consume(pMsg);
	}
	return pMsg;
}
/**
 * the ticker is called in the main loop. It handles the sending of NEXT
 * frames asynchronously
 */
void IsoTp::isotp_ticker()
{
	canmsg frame; // build the CAN frame
	if (isoMessageOutgoing.flow_active == 0)
		return; //
	if ((micros() - lastMicros) < isoMessageOutgoing.flow_delay)
		return;

	// Prepare the next frame
	//frame.FIR.B.FF = isoMessageOutgoing.id < 0x800 ? CAN_frame_std : CAN_frame_ext;
	//frame.FIR.B.RTR = CAN_no_RTR;		 // no RTR
	frame.MsgId = isoMessageOutgoing.id; // set the ID
	frame.size = 8;				 //command.requestLength + 1;// set the length. Note some ECU's like DLC 8

	frame.data[0] = 0x20 | (isoMessageOutgoing.next++ & 0x0f);
	int i;
	for (i = 0; i < 7 && isoMessageOutgoing.index < isoMessageOutgoing.length; i++)
	{
		frame.data[i + 1] = isoMessageOutgoing.data[isoMessageOutgoing.index++];
	}
	for (; i < 7; i++)
	{
		frame.data[i + 1] = 0x00;
	}

	// debug
	if (DEBUG_COMMAND_ISO)
	{
		OutputDebugStringA("> can:Sending ISOTP NEXT:");
		OutputDebugStringA(canFrameToString(&frame).c_str());
		OutputDebugStringA("\n");
	}

	// check if we reached the end of the message
	if (isoMessageOutgoing.length == isoMessageOutgoing.index)
	{
		// Done sending the outgoing message, so reset it and cancel further
		// handling by this ticker
		isoMessageOutgoing.length = isoMessageOutgoing.index = 0;
		isoMessageOutgoing.flow_active = 0;

		// At this moment, further sending by the ticker will stop. A new flow control
		// should not come in, but we now expect the answer, so do not invalidate the
		// incoming id
		// isoMessageIncoming.id = 0xffff;
		// the incoming message is full initiaized (id, index)
	}

	if (isoMessageOutgoing.flow_counter != 0)
	{
		if (--isoMessageOutgoing.flow_counter == 0)
		{
			isoMessageOutgoing.flow_active = 0;
		}
	}

	// send the frame
	elm327.elm327SendMsg(frame, 100); 	
	lastMicros = micros();
}

/**
 * Process an incoming ISO-TP frame
 * @param frame Incoming CANbus frame
 * @param bus CANbus (only 0 allowed)* 
 */
void IsoTp::storeIsotpframe(canmsg *frame)
{
	// if there is content and this is the frame we are waiting for
	if (frame->size == 0) //  || frame->MsgID != isoMessageIncoming.id)
	{
		if (DEBUG_BUS_RECEIVE_ISO)
			OutputDebugStringA((std::string( "< can:ISO frame of unrequested id:") + n2hexstr(isoMessageIncoming.id) + "," + n2hexstr(frame->MsgId) + "\n").c_str());
		// new assumption: there is only ONE active ISOTP command going on this means that this condition
		// should reset the ISOTP receiver
		isotp_reset();
		return;
	}

	uint8_t type = frame->data[0] >> 4; // type = first nibble

	// single frame answer ***************************************************
	if (type == 0x0)
	{
		if (DEBUG_BUS_RECEIVE_ISO)
		{
			OutputDebugStringA(std::string( "< can:ISO SING:)" + canFrameToString(frame) + "\n").c_str());
		}
		isoMessageIncoming.id = frame->MsgId;
		uint16_t messageLength = frame->data[0] & 0x0f; // length = second nibble + second byte
		if (messageLength > 7)
		{
			isotp_reset();
			return;
		}
		isoMessageIncoming.length = messageLength;

		// fill up with this initial first-frame data (should always be 6)
		for (int i = 1; i < frame->size && isoMessageIncoming.index < isoMessageIncoming.length; i++)
		{
			isoMessageIncoming.data[isoMessageIncoming.index++] = frame->data[i];
		}
		if (DEBUG_BUS_RECEIVE_ISO)
		{
			std::string dataString = isoMessageToString(&isoMessageIncoming);
			OutputDebugStringA(std::string("> can:ISO MSG:" + dataString + "\n").c_str());
		}
		//if (isotp_config->output_handler)
			//isotp_config->output_handler(isoMessageToString(&isoMessageIncoming) + "\n");
		// isoMessageIncoming.id = 0xffff; // cancel this message so nothing will be added until it is re-initialized
		EnqueueIstopMessage(&isoMessageIncoming);
		isotp_reset();
		return;
	}

	// first frame of a multi-framed message *********************************
	if (type == 0x1)
	{
		if (DEBUG_BUS_RECEIVE_ISO)
		{
			OutputDebugStringA(std::string("< can:ISO FRST:" + canFrameToString(frame)+"\n").c_str());
		}
		isoMessageIncoming.id = frame->MsgId;
		// start by requesting requesing the type Consecutive (0x2) frames by sending a Flow frame
		can_send_flow(isoMessageOutgoing.id, messagecommand);
		//can_send_flow(frame->MsgId, messagecommand); //WRONG!

		uint16_t messageLength = (frame->data[0] & 0x0f) << 8; // length = second nibble + second byte
		messageLength |= frame->data[1];
		if (messageLength > 4096)
		{
			OutputDebugStringA("< can: length FRST > 4096\n");
			isotp_reset();
			return;
		}

		isoMessageIncoming.length = messageLength;
		for (int i = 2; i < 8; i++)
		{
			isoMessageIncoming.data[isoMessageIncoming.index++] = frame->data[i];
		}
		return;
	}

	// consecutive frame(s) **************************************************
	if (type == 0x2)
	{
		if (DEBUG_BUS_RECEIVE_ISO)
		{
			OutputDebugStringA(std::string("< can:ISO NEXT:" + canFrameToString(frame)+"\n").c_str());
		}

		uint8_t sequence = frame->data[0] & 0x0f;
		if (isoMessageIncoming.next != sequence)
		{
			if (DEBUG_BUS_RECEIVE_ISO)
				OutputDebugStringA("< can:ISO Out of sequence, resetting\n");
			isotp_reset();
		}

		for (int i = 1; i < frame->size && isoMessageIncoming.index < isoMessageIncoming.length; i++)
		{
			isoMessageIncoming.data[isoMessageIncoming.index++] = frame->data[i];
		}

		// wait for next message, rollover from 15 to 0
		isoMessageIncoming.next = isoMessageIncoming.next == 15 ? 0 : isoMessageIncoming.next + 1;

		// is this the last part?
		if (isoMessageIncoming.index == isoMessageIncoming.length)
		{
			// output the data
			if (DEBUG_BUS_RECEIVE_ISO)
			{
				std::string dataString = isoMessageToString(&isoMessageIncoming);
				OutputDebugStringA(std::string("< can:ISO MSG:" + dataString + "\n").c_str());
			}
			EnqueueIstopMessage(&isoMessageIncoming);
			isotp_reset();
		}
		return;
	}

	// incoming flow control ***********************************************
	if (type == 0x3)
	{
		if (DEBUG_BUS_RECEIVE_ISO)
			OutputDebugStringA("< can:ISO FLOW\n");

		//uint8_t flag = isoMessageIncoming.data[0] &0x0f;
		isoMessageOutgoing.flow_counter = frame->data[1];
		isoMessageOutgoing.flow_delay = frame->data[2] <= 127 ? frame->data[2] * 1000 : frame->data[2] - 0xf0;
		// to avoid overwhelming the outgoing queue, set minimum to 5 ms
		// this is experimental.
		if (isoMessageOutgoing.flow_delay < 5000)
			isoMessageOutgoing.flow_delay = 5000;
		isoMessageOutgoing.flow_active = 1;
		lastMicros = micros();
		return;
	}

	if (DEBUG_BUS_RECEIVE_ISO)
		OutputDebugStringA(std::string("< can:ISO ignoring unknown frame type: " + std::to_string(type) + "\n").c_str());
	isotp_reset();
	return;
}

/**
 * Send a flow control frame
 * @param id id ID of the message
 * @param bus CANbus (only 0 allowed)
 */
void IsoTp::can_send_flow(uint32_t id, uint8_t msgcommand)
{
	canmsg flow;
	//flow.FIR.B.FF = id < 0x800 ? CAN_frame_std : CAN_frame_ext;
	//flow.FIR.B.RTR = CAN_no_RTR; // no RTR
	flow.MsgId = id;			 // send it to the requestId
	flow.size = 8;			 // length 8 bytes
	flow.data[0] = 0x30;		 // type Flow (3), flag Clear to send (0)
	flow.data[1] = 0x00;		 // instruct to send all remaining frames without flow control
	flow.data[2] = 0x00;		 // delay between frames <=127 = millis, can maybe set to 0
	flow.data[3] = 0x00;		 // fill-up
	flow.data[4] = 0x00;		 // fill-up
	flow.data[5] = 0x00;		 // fill-up
	flow.data[6] = 0x00;		 // fill-up
	flow.data[7] = 0x00;		 // fill-up
	elm327.elm327SendMsg(flow, 0);
}


/**
 * Send an ISO-TP message
 * @param id ID of the message
 * @param length of the payload
 * @param request payload
 * @param bus CANbus (only 0 allowed)
 */
void IsoTp::requestIsotp(uint32_t id, int16_t length, uint8_t *request)
{
	canmsg frame; // build the CAN frame

	isotp_reset(); // cancel possible ongoing IsoTp run

	isoMessageIncoming.id = id;	  // expected ID of answer
	isoMessageIncoming.index = 0; // starting
	isoMessageIncoming.next = 1;
	isoMessageOutgoing.id = id;
	if (isoMessageOutgoing.id  == 0) // ID to send request to
	{
		if (DEBUG_COMMAND)
			OutputDebugStringA(std::string("> can:" + n2hexstr(id) + " has no corresponding request ID\n").c_str());
		isotp_reset();
		return;
	}
	// store request to send
	isoMessageOutgoing.length = length;
	if (isoMessageOutgoing.length > 4096)
	{
		OutputDebugStringA("> can:length request > 4096\n");
		isotp_reset();
		return;
	}

	for (uint16_t i = 0; i < length; i++)
	{
		isoMessageOutgoing.data[i] = request[i];
	}
	isoMessageOutgoing.index = 0; // start at the beginning
	isoMessageOutgoing.next = 1;

	// Prepare the initial frame
	//frame.FIR.B.FF = isoMessageOutgoing.id < 0x800 ? CAN_frame_std : CAN_frame_ext;
	//frame.FIR.B.RTR = CAN_no_RTR;		 // no RTR
	frame.MsgId = isoMessageOutgoing.id; // set the ID
	frame.size = 8;				 //command.requestLength + 1;// set the length. Note some ECU's like DLC 8

	if (isoMessageOutgoing.length <= 7)
	{ // send SING frame
		// prepare the frame
		frame.data[0] = (isoMessageOutgoing.length & 0x0f);
		for (int i = 0; i < isoMessageOutgoing.length; i++)
		{ // fill up the other bytes with the request
			frame.data[i + 1] = isoMessageOutgoing.data[i];
		}
		for (int i = isoMessageOutgoing.length; i < 7; i++)
		{
			frame.data[i + 1] = 0x00; // Pad frame
		}

		// debug
		if (DEBUG_COMMAND_ISO)
		{
			OutputDebugStringA(std::string("> can:Sending ISOTP SING request:" + canFrameToString(&frame) + "\n").c_str());
		}

		// send the frame
		elm327.elm327SendMsg(frame,0, false);

		// --> any incoming frames with the given id will be handled by "storeFrame"
		// and send off if complete. But ensure the ticker doesn't do any flow_block
		// control
		isoMessageOutgoing.length = isoMessageOutgoing.index = 0;
	}

	else
	{ // send a FIRST frame
		// prepare the firt frame
		frame.data[0] = (uint8_t)(0x10 + ((length >> 8) & 0x0f));
		frame.data[1] = (uint8_t)(length & 0xff);
		for (int i = 0; i < 6; i++)
		{ // fill up the other bytes with the first 6 bytes of the request
			frame.data[i + 2] = isoMessageOutgoing.data[isoMessageOutgoing.index++];
		}

		// debug
		if (DEBUG_COMMAND_ISO)
		{
			OutputDebugStringA(std::string("> can:Sending ISOTP FRST request:" + canFrameToString(&frame) + "\n").c_str());
		}

		// send the frame
		elm327.elm327SendMsg(frame, 0, false); //Don't wait for answer, we dont care about it
		// --> any incoming frames with the given id will be handled by "storeFrame" and send off if complete
	}
}

/**
 * Convert a ISO-TP message to readable hex output format, newline terminated
 * @param message Pointer to the ISO_MESSAGE_t struct
 */
std::string IsoTp::isoMessageToString(ISO_MESSAGE_t *message)
{
	std::string dataString = n2hexstr(message->id) + ",";
	for (int i = 0; i < message->length; i++)
	{
		dataString += n2hexstr(message->data[i]);
	}
	return dataString;
}
