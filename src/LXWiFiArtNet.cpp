/**************************************************************************/
/*!
    @file     LXWiFiArtNet.cpp
    @author   Claude Heintz
    @license  BSD (see LXDMXWiFi.h)
    @copyright 2015 by Claude Heintz All Rights Reserved
    
    Art-Net(TM) Designed by and Copyright Artistic Licence (UK) Ltd

    Supports sending and receiving Art-Net via ESP8266 WiFi connection.

    @section  HISTORY

    v1.0 - First release
*/
/**************************************************************************/

#include "LXWiFiArtNet.h"

LXWiFiArtNet::LXWiFiArtNet ( IPAddress address )
{
	//zero buffer including _dmx_data[0] which is start code
    for (int n=0; n<ARTNET_BUFFER_MAX; n++) {
    	_packet_buffer[n] = 0;
    	if ( n < DMX_UNIVERSE_SIZE ) {
    	   _dmx_buffer_a[n] = 0;
	   	_dmx_buffer_b[n] = 0;
	   	_dmx_buffer_c[n] = 0;
    	}
    }
    
    _dmx_slots = 0;
    _dmx_slots_a = 0;
    _dmx_slots_b = 0;
    _universe = 0;
    _my_address = address;
    _broadcast_address = INADDR_NONE;
    _dmx_sender_a = INADDR_NONE;
    _dmx_sender_b = INADDR_NONE;
    _sequence = 1;
}

LXWiFiArtNet::LXWiFiArtNet ( IPAddress address, IPAddress subnet_mask )
{
	//zero buffer including _dmx_data[0] which is start code
    for (int n=0; n<ARTNET_BUFFER_MAX; n++) {
    	_packet_buffer[n] = 0;
    	if ( n < DMX_UNIVERSE_SIZE ) {
    	   _dmx_buffer_a[n] = 0;
	   	_dmx_buffer_b[n] = 0;
	   	_dmx_buffer_c[n] = 0;
    	}
    }
    
    _dmx_slots = 0;
    _dmx_slots_a = 0;
    _dmx_slots_b = 0;
    _universe = 0;
    _my_address = address;
    uint32_t a = (uint32_t) address;
    uint32_t s = (uint32_t) subnet_mask;
    _broadcast_address = IPAddress(a | ~s);
    _dmx_sender_a = INADDR_NONE;
    _dmx_sender_b = INADDR_NONE;
    _sequence = 1;
}

LXWiFiArtNet::~LXWiFiArtNet ( void )
{
    //no need for specific destructor
}

uint8_t  LXWiFiArtNet::universe ( void ) {
	return _universe;
}

void LXWiFiArtNet::setUniverse ( uint8_t u ) {
	_universe = u;
}

void LXWiFiArtNet::setSubnetUniverse ( uint8_t s, uint8_t u ) {
   _universe = ((s & 0x0f) << 4) | ( u & 0x0f);
}

void LXWiFiArtNet::setUniverseAddress ( uint8_t u ) {
	if ( u != 0x7f ) {
	   if ( u & 0x80 ) {
	     _universe = (_universe & 0xf0) | (u & 0x07);
	   }
	}
}

void LXWiFiArtNet::setSubnetAddress ( uint8_t u ) {
	if ( u != 0x7f ) {
	   if ( u & 0x80 ) {
	     _universe = (_universe & 0x0f) | ((u & 0x07) << 4);
	   }
	}
}

int  LXWiFiArtNet::numberOfSlots ( void ) {
	return _dmx_slots;
}

void LXWiFiArtNet::setNumberOfSlots ( int n ) {
	_dmx_slots = n;
}

uint8_t LXWiFiArtNet::getSlot ( int slot ) {
	return _dmx_buffer_c[slot-1];
}

void LXWiFiArtNet::setSlot ( int slot, uint8_t level ) {
	_packet_buffer[ARTNET_ADDRESS_OFFSET+slot] = level;
}

uint8_t* LXWiFiArtNet::dmxData( void ) {
	return &_packet_buffer[ARTNET_ADDRESS_OFFSET+1];
}

uint8_t LXWiFiArtNet::readDMXPacket ( WiFiUDP wUDP ) {
	//digitalWrite(4,HIGH);
   uint16_t opcode = readArtNetPacket(wUDP);
   //digitalWrite(4,LOW);
   return ( opcode == ARTNET_ART_DMX );
}

/*
  attempts to read a packet from the supplied EthernetUDP object
  returns opcode
  sends ArtPollReply with IPAddress if packet is ArtPoll
  replies directly to sender unless reply_ip != INADDR_NONE allowing specification of broadcast
  only returns ARTNET_ART_DMX if packet contained dmx data for this universe
  Packet size checks that packet is >= expected size to allow zero termination or padding
*/
uint16_t LXWiFiArtNet::readArtNetPacket ( WiFiUDP wUDP ) {
   uint16_t packetSize = wUDP.parsePacket();
   uint16_t opcode = ARTNET_NOP;
   if ( packetSize ) {
      packetSize = wUDP.read(_packet_buffer, ARTNET_BUFFER_MAX);
      _dmx_slots = 0;
      /* Buffer now may not contain dmx data for desired universe.
         After reading the packet into the buffer, check to make sure
         that it is an Art-Net packet and retrieve the opcode that
         tells what kind of message it is.                            */
      opcode = parse_header();
      switch ( opcode ) {
		   case ARTNET_ART_DMX:
		   	// ignore sequence[12], physical[13] and subnet/universe hi byte[15]
				if (( _packet_buffer[14] == _universe ) && ( _packet_buffer[11] >= 14 )) { //protocol version [10] hi byte [11] lo byte 
					packetSize -= 18;
					uint16_t slots = _packet_buffer[17];
					slots += _packet_buffer[16] << 8;
				   if ( packetSize >= slots ) {
						if ( (uint32_t)_dmx_sender_a == 0 ) {		//if first sender, remember address
							_dmx_sender_a = wUDP.remoteIP();
						}
						if ( _dmx_sender_a == wUDP.remoteIP() ) {
						   _dmx_slots_a  = slots;
						   if ( _dmx_slots_a > _dmx_slots_b ) {
						   	_dmx_slots = _dmx_slots_a;
						   } else {
						      _dmx_slots = _dmx_slots_b;
						   }
						   int di;
						   int dc = _dmx_slots;
						   int dt = ARTNET_ADDRESS_OFFSET + 1;
						     for (di=0; di<dc; di++) {
						       _dmx_buffer_a[di] = _packet_buffer[dt+di];
						   	if ( _dmx_buffer_a[di] > _dmx_buffer_b[di] ) {
						   	   _dmx_buffer_c[di] = _dmx_buffer_a[di];
						   	} else {
						   	   _dmx_buffer_c[di] = _dmx_buffer_b[di];
						   	}
						   }
						} else { // matched sender a
							if ( (uint32_t)_dmx_sender_b == 0 ) {		//if first sender, remember address
								_dmx_sender_b = wUDP.remoteIP();
							}
							if ( _dmx_sender_b == wUDP.remoteIP() ) {
						     _dmx_slots_b  = slots;
						      if ( _dmx_slots_a > _dmx_slots_b ) {
						   	   _dmx_slots = _dmx_slots_a;
						      } else {
						         _dmx_slots = _dmx_slots_b;
						      }
						     int di;
						     int dc = _dmx_slots;
						     int dt = ARTNET_ADDRESS_OFFSET + 1;
						     for (di=0; di<dc; di++) {
						       _dmx_buffer_b[di] = _packet_buffer[dt+di];
						       if ( _dmx_buffer_a[di] > _dmx_buffer_b[di] ) {
						   	   _dmx_buffer_c[di] = _dmx_buffer_a[di];
						   	 } else {
						   	   _dmx_buffer_c[di] = _dmx_buffer_b[di];
						   	 }
						     }
						   }  // matched sender b
						}     // did not match sender a
					}		   // matched size
				}			   // matched universe
				if ( _dmx_slots == 0 ) {	//only set >0 if all of above matched
					opcode = ARTNET_NOP;
				}
				break;
			case ARTNET_ART_ADDRESS:
				if (( packetSize >= 107 ) && ( _packet_buffer[11] >= 14 )) {  //protocol version [10] hi byte [11] lo byte
		   	   opcode = parse_art_address();
		   	   send_art_poll_reply( wUDP );
		   	}
		   	break;
			case ARTNET_ART_POLL:
				if (( packetSize >= 14 ) && ( _packet_buffer[11] >= 14 )) {
				   send_art_poll_reply( wUDP );
				}
				break;
		}
   }
   return opcode;
}

void LXWiFiArtNet::sendDMX ( WiFiUDP wUDP, IPAddress to_ip, IPAddress interfaceAddr ) {
   _packet_buffer[0] = 'A';
   _packet_buffer[1] = 'r';
   _packet_buffer[2] = 't';
   _packet_buffer[3] = '-';
   _packet_buffer[4] = 'N';
   _packet_buffer[5] = 'e';
   _packet_buffer[6] = 't';
   _packet_buffer[7] = 0;
   _packet_buffer[8] = 0;        //op code lo-hi
   _packet_buffer[9] = 0x50;
   _packet_buffer[10] = 0;
   _packet_buffer[11] = 14;
   if ( _sequence == 0 ) {
     _sequence = 1;
   } else {
     _sequence++;
   }
   _packet_buffer[12] = _sequence;
   _packet_buffer[13] = 0;
   _packet_buffer[14] = _universe;
   _packet_buffer[15] = 0;
   _packet_buffer[16] = _dmx_slots >> 8;
   _packet_buffer[17] = _dmx_slots & 0xFF;
   //assume dmx data has been set
  
   wUDP.beginPacket(to_ip, ARTNET_PORT);
   wUDP.write(_packet_buffer, _dmx_slots+18);
   wUDP.endPacket();
}

/*
  sends ArtDMX packet to EthernetUDP object's remoteIP if to_ip is not specified
  ( remoteIP is set when parsePacket() is called )
  includes my_ip as address of this node
*/
void LXWiFiArtNet::send_art_poll_reply( WiFiUDP wUDP ) {
  unsigned char  replyBuffer[ARTNET_REPLY_SIZE];
  int i;
  for ( i = 0; i < ARTNET_REPLY_SIZE; i++ ) {
    replyBuffer[i] = 0;
  }
  replyBuffer[0] = 'A';
  replyBuffer[1] = 'r';
  replyBuffer[2] = 't';
  replyBuffer[3] = '-';
  replyBuffer[4] = 'N';
  replyBuffer[5] = 'e';
  replyBuffer[6] = 't';
  replyBuffer[7] = 0;
  replyBuffer[8] = 0;        // op code lo-hi
  replyBuffer[9] = 0x21;
  replyBuffer[10] = ((uint32_t)_my_address) & 0xff;      //ip address
  replyBuffer[11] = ((uint32_t)_my_address) >> 8;
  replyBuffer[12] = ((uint32_t)_my_address) >> 16;
  replyBuffer[13] = ((uint32_t)_my_address) >>24;
  replyBuffer[14] = 0x36;    // port lo first always 0x1936
  replyBuffer[15] = 0x19;
  replyBuffer[16] = 0;       // firmware hi-lo
  replyBuffer[17] = 0;
  replyBuffer[18] = 0;       // subnet hi-lo
  replyBuffer[19] = 0;
  replyBuffer[20] = 0;       // oem hi-lo
  replyBuffer[21] = 0;
  replyBuffer[22] = 0;       // ubea
  replyBuffer[23] = 0;       // status
  replyBuffer[24] = 0x50;    //     Mfg Code
  replyBuffer[25] = 0x12;    //     seems DMX workshop reads these bytes backwards
  replyBuffer[26] = 'A';     // short name
  replyBuffer[27] = 'r';
  replyBuffer[28] = 'd';
  replyBuffer[29] = 'u';
  replyBuffer[30] = 'i';
  replyBuffer[31] = 'n';
  replyBuffer[32] = 'o';
  replyBuffer[33] =  0;
  replyBuffer[44] = 'A';     // long name
  replyBuffer[45] = 'r';
  replyBuffer[46] = 'd';
  replyBuffer[47] = 'u';
  replyBuffer[48] = 'i';
  replyBuffer[49] = 'n';
  replyBuffer[50] = 'o';
  replyBuffer[51] =  0;
  replyBuffer[173] = 1;    // number of ports
  replyBuffer[174] = 128;  // can output from network
  replyBuffer[182] = 128;  //  good output... change if error
  replyBuffer[190] = _universe;
  
  IPAddress a = _broadcast_address;
  if ( a == INADDR_NONE ) {
    a = wUDP.remoteIP();   // reply directly if no broadcast address is supplied
  }
  wUDP.beginPacket(a, ARTNET_PORT);
  wUDP.write(replyBuffer, ARTNET_REPLY_SIZE);
  wUDP.endPacket();
}

uint16_t LXWiFiArtNet::parse_header( void ) {
  if ( strcmp((const char*)_packet_buffer, "Art-Net") == 0 ) {
    return _packet_buffer[9] * 256 + _packet_buffer[8];  //opcode lo byte first
  }
  return ARTNET_NOP;
}

/*
  reads an ARTNET_ART_ADDRESS packet
  can set output universe
  can cancel merge which resets address of dmx sender
     (after first ArtDmx packet, only packets from the same sender are accepted
     until a cancel merge command is received)
*/
uint16_t LXWiFiArtNet::parse_art_address( void ) {
	//[14] to [31] short name <= 18 bytes
	//[32] to [95] long name  <= 64 bytes
	//[96][97][98][99]                  input universe   ch 1 to 4
	//[100][101][102][103]               output universe   ch 1 to 4
	setUniverseAddress(_packet_buffer[100]);
	//[104]                              subnet
	setSubnetAddress(_packet_buffer[104]);
	//[105]                                   reserved
	uint8_t command = _packet_buffer[106]; // command
	switch ( command ) {
	   case 0x01:	//cancel merge: resets ip address used to identify dmx sender
	   	_dmx_sender_a = INADDR_NONE;
	   	_dmx_sender_b = INADDR_NONE;
	   	break;
	   case 0x90:	//clear buffer
	   	_dmx_sender_a = INADDR_NONE;
	   	_dmx_sender_b = INADDR_NONE;
	   	for(int j=0; j<DMX_UNIVERSE_SIZE; j++) {
	   	   _dmx_buffer_a[j] = 0;
	   	   _dmx_buffer_b[j] = 0;
	   	   _dmx_buffer_c[j] = 0;
	   	}
	   	_dmx_slots = 512;
	   	return ARTNET_ART_DMX;	// return ARTNET_ART_DMX so function calling readPacket
	   	   						   // knows there has been a change in levels
	   	break;
	}
	return ARTNET_ART_ADDRESS;
}