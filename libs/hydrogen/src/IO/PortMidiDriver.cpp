/*
 * Hydrogen
 * Copyright(c) 2002-2007 by Alex >Comix< Cominu [comix@users.sourceforge.net]
 *
 * http://www.hydrogen-music.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "PortMidiDriver.h"

#include <hydrogen/Preferences.h>

#ifdef WIN32
	#include <windows.h>
#endif

#ifdef PORTMIDI_SUPPORT

#include <porttime.h>
#define TIME_PROC ((long (*)(void *)) Pt_Time)
#define TIME_START Pt_Start(1, 0, 0) /* timer started w/millisecond accuracy */

#include <pthread.h>

namespace H2Core {

pthread_t PortMidiDriverThread;

void* PortMidiDriver_thread(void* param)
{
	PortMidiDriver *instance = ( PortMidiDriver* )param;
	_INFOLOG("PortMidiDriver_thread starting");

	PmError status;
	PmError length;
	PmEvent buffer[1];
	while (instance->m_bRunning) {
		status = Pm_Poll(instance->m_pMidiIn);
		if (status == TRUE) {
			length = Pm_Read( instance->m_pMidiIn, buffer, 1 );
			if (length > 0) {
				MidiMessage msg;

				int nEventType = Pm_MessageStatus(buffer[0].message);
				if ( ( nEventType >= 128 ) && ( nEventType < 144 ) ) {	// note off
					msg.m_nChannel = nEventType - 128;
					msg.m_type = MidiMessage::NOTE_OFF;
				}
				else if ( ( nEventType >= 144 ) && ( nEventType < 160 ) ) {	// note on
					msg.m_nChannel = nEventType - 144;
					msg.m_type = MidiMessage::NOTE_ON;
				}
				else if ( ( nEventType >= 160 ) && ( nEventType < 176 ) ) {	// Polyphonic Key Pressure (After-touch)
					msg.m_nChannel = nEventType - 160;
					msg.m_type = MidiMessage::POLYPHONIC_KEY_PRESSURE;
				}
				else if ( ( nEventType >= 176 ) && ( nEventType < 192 ) ) {	// Control Change
					msg.m_nChannel = nEventType - 176;
					msg.m_type = MidiMessage::CONTROL_CHANGE;
				}
				else if ( ( nEventType >= 192 ) && ( nEventType < 208 ) ) {	// Program Change
					msg.m_nChannel = nEventType - 192;
					msg.m_type = MidiMessage::PROGRAM_CHANGE;
				}
				else if ( ( nEventType >= 208 ) && ( nEventType < 224 ) ) {	// Channel Pressure (After-touch)
					msg.m_nChannel = nEventType - 208;
					msg.m_type = MidiMessage::CHANNEL_PRESSURE;
				}
				else if ( ( nEventType >= 224 ) && ( nEventType < 240 ) ) {	// Pitch Wheel Change
					msg.m_nChannel = nEventType - 224;
					msg.m_type = MidiMessage::PITCH_WHEEL;
				}
				else if ( ( nEventType >= 240 ) && ( nEventType < 256 ) ) {	// System Exclusive
					msg.m_nChannel = nEventType - 240;
					msg.m_type = MidiMessage::SYSTEM_EXCLUSIVE;
				}
				else {
					_ERRORLOG( "Unhandled midi message type: " + toString( nEventType ) );
					_INFOLOG( "MIDI msg: " );
					_INFOLOG( toString( buffer[0].timestamp ) );
					_INFOLOG( toString( Pm_MessageStatus(buffer[0].message) ) );
					_INFOLOG( toString( Pm_MessageData1(buffer[0].message) ) );
					_INFOLOG( toString( Pm_MessageData2(buffer[0].message) ) );
				}

				msg.m_nData1 = Pm_MessageData1(buffer[0].message);
				msg.m_nData2 = Pm_MessageData2(buffer[0].message);

				instance->handleMidiMessage( msg );
			}
		}
		else {
#ifdef WIN32
			Sleep( 1 );
#else
			usleep( 100 );
#endif
		}
	}



	_INFOLOG("MIDI Thread DESTROY");
	pthread_exit(NULL);
	return NULL;
}


PortMidiDriver::PortMidiDriver()
: MidiInput( "PortMidiDriver" )
, m_bRunning( false )
{
	Pm_Initialize();
}


PortMidiDriver::~PortMidiDriver()
{
	Pm_Terminate();
}




void PortMidiDriver::open()
{
	INFOLOG( "[open]" );

	int nInputBufferSize = 100;

	int nDeviceId = -1;
	string sMidiPortName = Preferences::getInstance()->m_sMidiPortName;
	int nDevices = Pm_CountDevices();
	for ( int i = 0; i < nDevices; i++) {
		const PmDeviceInfo *info = Pm_GetDeviceInfo(i);
		if (info == NULL) {
			ERRORLOG( "Could not open input device" );
		}

		if (info->input == TRUE ) {
			if ( info->name == sMidiPortName ) {
				nDeviceId = i;
			}
		}
	}

	if (nDeviceId == -1) {
		INFOLOG( "Midi input device not found." );
		return;
	}

	const PmDeviceInfo *info = Pm_GetDeviceInfo(nDeviceId);
	if (info == NULL) {
		ERRORLOG( "Error opening midi input device" );
	}

	INFOLOG( string( "Device: ").append( info->interf ).append( " " ).append( info->name ) );
	TIME_START;

	PmError err = Pm_OpenInput(
			&m_pMidiIn,
			nDeviceId,
			NULL,
			nInputBufferSize,
			TIME_PROC,
			NULL
	);

	if ( err != pmNoError ) {
		ERRORLOG( "Error in Pm_OpenInput" );
	}

	m_bRunning = true;

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_create(&PortMidiDriverThread, &attr, PortMidiDriver_thread, (void*)this);
}


void PortMidiDriver::close()
{
	INFOLOG( "[close]" );
	if (m_bRunning) {
		m_bRunning = false;
		pthread_join(PortMidiDriverThread, NULL);
		PmError err = Pm_Close( m_pMidiIn );
		if ( err != pmNoError ) {
			ERRORLOG( "Error in Pm_OpenInput" );
		}
	}
}



std::vector<std::string> PortMidiDriver::getOutputPortList()
{
	std::vector<std::string> portList;

	int nDevices = Pm_CountDevices();
	for ( int i = 0; i < nDevices; i++) {
		const PmDeviceInfo *info = Pm_GetDeviceInfo(i);
		if (info == NULL) {
			ERRORLOG( "Could not open input device" );
		}

		if (info->input == TRUE ) {
			INFOLOG( info->name );
			portList.push_back( info->name );
		}
	}

	return portList;
}

};

#endif	// PORTMIDI_SUPPORT
