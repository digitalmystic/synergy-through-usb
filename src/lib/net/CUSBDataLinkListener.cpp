/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2012 Bolton Software Ltd.
 * Copyright (C) 2002 Chris Schoeneman
 * 
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file COPYING that should have accompanied this file.
 * 
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "CLog.h"
#include "CUSBAddress.h"
#include "CUSBDataLink.h"
#include "CUSBDataLinkListener.h"
#include "CMutex.h"
#include "TMethodEventJob.h"

//
// CUSBDataLinkListener
//

CUSBDataLinkListener::CUSBDataLinkListener()
{
	m_mutex = new CMutex;
}

CUSBDataLinkListener::~CUSBDataLinkListener()
{
	close();
	delete m_mutex;
}

void
CUSBDataLinkListener::bind(const CBaseAddress& addr)
{
	CLock lock(m_mutex);
	
	IDataTransfer* dataLink = new CUSBDataLink();
	try
	{
		EVENTQUEUE->adoptHandler(
			dataLink->getInputReadyEvent(),
			dataLink->getEventTarget(),
			new TMethodEventJob<CUSBDataLinkListener>(this, &CUSBDataLinkListener::handleData, dataLink));

		EVENTQUEUE->adoptHandler(
			dataLink->getDisconnectedEvent(),
			dataLink->getEventTarget(),
			new TMethodEventJob<CUSBDataLinkListener>(this, &CUSBDataLinkListener::handleDisconnected, dataLink));

		dataLink->bind(addr);

		m_bindedLinks.insert(dataLink);

		{
			const CUSBAddress& usbAddress = reinterpret_cast<const CUSBAddress&>(addr);
			m_addressMap[dataLink] = usbAddress;
		}
	}
	catch(...)
	{
		EVENTQUEUE->removeHandler(
			dataLink->getInputReadyEvent(),
			dataLink->getEventTarget());

		delete dataLink;
		throw;
	}
}

void
CUSBDataLinkListener::close()
{
	CLock lock(m_mutex);

	for (CUSBLinkSet::const_iterator i=m_bindedLinks.cbegin(); i!=m_bindedLinks.cend(); ++i)
	{
		delete *i;
	}
	m_bindedLinks.clear();

	for (CUSBLinkDeque::const_iterator i=m_waitingLinks.cbegin(); i!=m_waitingLinks.cend(); ++i)
	{
		delete *i;
	}
	m_waitingLinks.clear();
}

void*
CUSBDataLinkListener::getEventTarget() const
{
	return const_cast<void*>(reinterpret_cast<const void*>(this));
}

IDataTransfer*
CUSBDataLinkListener::accept()
{
	CLock lock(m_mutex);

	IDataTransfer* result = m_waitingLinks.front();

	// send kUsbAccept message to client in response to kUsbConnect
	std::string buf(kUsbAccept);
	result->write(buf.c_str(), buf.size());

	m_waitingLinks.pop_front();

	return result;
}

void CUSBDataLinkListener::handleData(const CEvent&, void* ctx)
{
	LOG((CLOG_PRINT "CUSBDataLinkListener::handleData"));

	IDataTransfer* dataLink = reinterpret_cast<IDataTransfer*>(ctx);

	// check if the client sent us kUsbConnect message
	std::string buf("");
		
	buf.resize(dataLink->getSize(), 0);
	dataLink->read((void*)buf.c_str(), buf.size());

	if (buf == kUsbConnect)
	{
		CLock lock(m_mutex);

		EVENTQUEUE->removeHandler(
				dataLink->getInputReadyEvent(),
				dataLink->getEventTarget());

		m_bindedLinks.erase(dataLink);
		m_waitingLinks.push_back(dataLink);

		EVENTQUEUE->addEvent(CEvent(getConnectingEvent(), this, NULL));
	}
	else
	{
		CLock lock(m_mutex);

		std::string buf(kUsbReject);
		dataLink->write(buf.c_str(), buf.size());
	}
}

void CUSBDataLinkListener::handleDisconnected(const CEvent&, void* ctx)
{
	LOG((CLOG_PRINT "CUSBDataLinkListener::handleDisconnected"));

	IDataTransfer* dataLink = reinterpret_cast<IDataTransfer*>(ctx);

	CLock lock(m_mutex);

	// directly pass dataLink as target, because this object is already deleted.
	EVENTQUEUE->removeHandler(
		dataLink->getDisconnectedEvent(),
		//dataLink->getEventTarget())
		dataLink)
		;

	CUSBAddress addr = m_addressMap[dataLink];

	m_addressMap.erase(dataLink);
	//delete dataLink;

	bind(addr);
}
