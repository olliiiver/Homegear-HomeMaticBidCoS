/* Copyright 2013-2015 Sathya Laufer
 *
 * Homegear is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Homegear is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Homegear.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */

#include "HmCcTc.h"
#include "homegear-base/BaseLib.h"
#include "../GD.h"

namespace BidCoS
{

HmCcTc::HmCcTc(uint32_t parentID, bool centralFeatures, IPeerEventSink* eventHandler) : BidCoSPeer(parentID, centralFeatures, eventHandler)
{
	startDutyCycle(-1); //Peer is newly created
}

HmCcTc::HmCcTc(int32_t id, int32_t address, std::string serialNumber, uint32_t parentID, bool centralFeatures, IPeerEventSink* eventHandler) : BidCoSPeer(id, address, serialNumber, parentID, centralFeatures, eventHandler)
{
}

HmCcTc::~HmCcTc()
{
	dispose();
}

void HmCcTc::dispose()
{
	try
	{
		_stopDutyCycleThread = true;
		if(_dutyCycleThread.joinable()) _dutyCycleThread.join();
		if(_sendDutyCyclePacketThread.joinable()) _sendDutyCyclePacketThread.join();
		BidCoSPeer::dispose();
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HmCcTc::loadVariables(BaseLib::Systems::ICentral* device, std::shared_ptr<BaseLib::Database::DataTable>& rows)
{
	try
	{
		BidCoSPeer::loadVariables(device, rows);
		_databaseMutex.lock();
		for(BaseLib::Database::DataTable::iterator row = rows->begin(); row != rows->end(); ++row)
		{
			_variableDatabaseIDs[row->second.at(2)->intValue] = row->second.at(0)->intValue;
			switch(row->second.at(2)->intValue)
			{
			case 1000:
				_currentDutyCycleDeviceAddress = row->second.at(3)->intValue;
				break;
			case 1004:
				_valveState = row->second.at(3)->intValue;
				break;
			case 1005:
				_newValveState = row->second.at(3)->intValue;
				break;
			case 1006:
				_lastDutyCycleEvent = row->second.at(3)->intValue;
				break;
			case 1007:
				_dutyCycleMessageCounter = (uint8_t)row->second.at(3)->intValue;
				break;
			}
		}
		_databaseMutex.unlock();
		setDeviceType(BaseLib::Systems::LogicalDeviceType(BIDCOS_FAMILY_ID, (uint32_t)DeviceType::HMCCTC));
		startDutyCycle(calculateLastDutyCycleEvent());
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HmCcTc::saveVariables()
{
	try
	{
		BidCoSPeer::saveVariables();
		saveVariable(1000, _currentDutyCycleDeviceAddress);
		saveVariable(1004, _valveState);
		saveVariable(1005, _newValveState);
		saveVariable(1006, _lastDutyCycleEvent);
		saveVariable(1007, (int32_t)_dutyCycleMessageCounter);
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

int32_t HmCcTc::calculateCycleLength(uint8_t messageCounter)
{
	int32_t result = (((_address << 8) | messageCounter) * 1103515245 + 12345) >> 16;
	return (result & 0xFF) + 480;
}

int64_t HmCcTc::calculateLastDutyCycleEvent()
{
	try
	{
		if(_lastDutyCycleEvent < 0) _lastDutyCycleEvent = 0;
		int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		if(now - _lastDutyCycleEvent > 1800000000) return -1; //Duty cycle is out of sync anyway so don't bother to calculate
		int64_t nextDutyCycleEvent = _lastDutyCycleEvent;
		int64_t lastDutyCycleEvent = _lastDutyCycleEvent;
		_dutyCycleMessageCounter--; //The saved message counter is the current one, but the calculation has to use the last one
		while(nextDutyCycleEvent < now + 25000000)
		{
			lastDutyCycleEvent = nextDutyCycleEvent;
			nextDutyCycleEvent = lastDutyCycleEvent + (calculateCycleLength(_dutyCycleMessageCounter) * 250000) + _dutyCycleTimeOffset;
			_dutyCycleMessageCounter++;
		}
		GD::out.printDebug("Debug: Setting last duty cycle event to: " + std::to_string(lastDutyCycleEvent));
		return lastDutyCycleEvent;
	}
    catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return 0;
}

void HmCcTc::setValveState(int32_t valveState)
{
	try
	{
		valveState *= 256;
		//Round up if necessary. I don't use double for calculation, because hopefully this is faster.
		if(valveState % 100 >= 50) valveState = (valveState / 100) + 1; else valveState /= 100;
		_newValveState = valveState;
		if(_newValveState > 255) _newValveState = 255;
		if(_newValveState < 0) _newValveState = 0;
	}
    catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HmCcTc::startDutyCycle(int64_t lastDutyCycleEvent)
{
	try
	{
		if(_dutyCycleThread.joinable())
		{
			GD::out.printCritical("HomeMatic BidCoS peer " + std::to_string(_peerID) + ": Duty cycle thread already started. Something went very wrong.");
			return;
		}
		_dutyCycleThread = std::thread(&HmCcTc::dutyCycleThread, this, lastDutyCycleEvent);
		BaseLib::Threads::setThreadPriority(_bl, _dutyCycleThread.native_handle(), 35);
	}
    catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HmCcTc::dutyCycleThread(int64_t lastDutyCycleEvent)
{
	try
	{
		int64_t nextDutyCycleEvent = (lastDutyCycleEvent < 0) ? std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count() : lastDutyCycleEvent;
		_lastDutyCycleEvent = nextDutyCycleEvent;
		int64_t timePoint;
		int64_t cycleTime;
		uint32_t cycleLength = calculateCycleLength(_dutyCycleMessageCounter - 1); //The calculation has to use the last message counter
		_dutyCycleCounter = (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - _lastDutyCycleEvent) / 250000;
		if(_dutyCycleCounter < 0) _dutyCycleCounter = 0;
		if(_dutyCycleCounter > 0) GD::out.printDebug("Debug: Skipping " + std::to_string(_dutyCycleCounter * 250) + " ms of duty cycle.");
		//_dutyCycleCounter = (_dutyCycleCounter % 8 > 3) ? _dutyCycleCounter + (8 - (_dutyCycleCounter % 8)) : _dutyCycleCounter - (_dutyCycleCounter % 8);
		while(!_stopDutyCycleThread)
		{
			try
			{
				cycleTime = (int64_t)cycleLength * 250000;
				nextDutyCycleEvent += cycleTime + _dutyCycleTimeOffset; //Add offset every cycle. This is very important! Without it, 20% of the packets are sent too early.
				GD::out.printDebug("Next duty cycle: " + std::to_string(nextDutyCycleEvent / 1000) + " (in " + std::to_string(cycleTime / 1000) + " ms) with message counter 0x" + BaseLib::HelperFunctions::getHexString(_dutyCycleMessageCounter));

				std::chrono::milliseconds sleepingTime(250);
				while(!_stopDutyCycleThread && _dutyCycleCounter < (signed)cycleLength - 80)
				{
					std::this_thread::sleep_for(sleepingTime);
					_dutyCycleCounter += 1;
				}
				if(_stopDutyCycleThread) break;

				while(!_stopDutyCycleThread && _dutyCycleCounter < (signed)cycleLength - 40)
				{
					std::this_thread::sleep_for(sleepingTime);
					_dutyCycleCounter += 1;
				}
				if(_stopDutyCycleThread) break;

				setDecalcification();

				timePoint = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
				GD::out.printDebug("Correcting time mismatch of " + std::to_string((nextDutyCycleEvent - 10000000 - timePoint) / 1000) + "ms.");
				std::this_thread::sleep_for(std::chrono::microseconds(nextDutyCycleEvent - timePoint - 5000000));
				if(_stopDutyCycleThread) break;

				timePoint = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
				std::this_thread::sleep_for(std::chrono::microseconds(nextDutyCycleEvent - timePoint - 2000000));
				if(_stopDutyCycleThread) break;

				if(_sendDutyCyclePacketThread.joinable()) _sendDutyCyclePacketThread.join();
				_sendDutyCyclePacketThread = std::thread(&HmCcTc::sendDutyCyclePacket, this, _dutyCycleMessageCounter, nextDutyCycleEvent);
				BaseLib::Threads::setThreadPriority(_bl, _sendDutyCyclePacketThread.native_handle(), 99);

				_lastDutyCycleEvent = nextDutyCycleEvent;
				cycleLength = calculateCycleLength(_dutyCycleMessageCounter);
				_dutyCycleMessageCounter++;
				saveVariable(1006, _lastDutyCycleEvent);

				_dutyCycleCounter = 0;
			}
			catch(const std::exception& ex)
			{
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(BaseLib::Exception& ex)
			{
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(...)
			{
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
			}
		}
	}
    catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void HmCcTc::setDecalcification()
{
	try
	{
		std::time_t time1 = std::time(nullptr);
		std::tm* time2 = std::localtime(&time1);
		if(time2->tm_wday == 6 && time2->tm_hour == 14 && time2->tm_min >= 0 && time2->tm_min <= 3)
		{
			try
			{
				for(std::unordered_map<int32_t, bool>::iterator i = _decalcification.begin(); i != _decalcification.end(); ++i)
				{
					i->second = true;
				}
			}
			catch(const std::exception& ex)
			{
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(BaseLib::Exception& ex)
			{
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(...)
			{
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
			}
		}
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(BaseLib::Exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(...)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
}

void HmCcTc::sendDutyCyclePacket(uint8_t messageCounter, int64_t sendingTime)
{
	try
	{
		if(sendingTime < 0) sendingTime = 2000000;
		if(_stopDutyCycleThread) return;
		int32_t address = getNextDutyCycleDeviceAddress();
		GD::out.printDebug("Debug: HomeMatic BidCoS peer " + std::to_string(_peerID) + ": Next HM-CC-VD is 0x" + BaseLib::HelperFunctions::getHexString(_address));
		if(address < 1)
		{
			GD::out.printDebug("Debug: Not sending duty cycle packet, because no valve drives are paired to me.");
			return;
		}
		std::vector<uint8_t> payload;
		payload.push_back(getAdjustmentCommand(address));
		payload.push_back(_newValveState);
		std::shared_ptr<BidCoSPacket> packet(new BidCoSPacket(messageCounter, 0xA2, 0x58, _address, address, payload));

		int64_t nanoseconds = (sendingTime - std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - 1000000) * 1000;
		int32_t seconds = nanoseconds / 1000000000;
		nanoseconds -= seconds * 1000000000;
		struct timespec timeToSleep;
		timeToSleep.tv_sec = seconds;
		timeToSleep.tv_nsec = nanoseconds;
		nanosleep(&timeToSleep, NULL);
		if(_stopDutyCycleThread) return;

		nanoseconds = (sendingTime - std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - 500000) * 1000;
		timeToSleep.tv_sec = 0;
		timeToSleep.tv_nsec = nanoseconds;
		nanosleep(&timeToSleep, NULL);
		if(_stopDutyCycleThread) return;

		nanoseconds = (sendingTime - std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - 100000) * 1000;
		timeToSleep.tv_nsec = nanoseconds;
		nanosleep(&timeToSleep, NULL);

		nanoseconds = (sendingTime - std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - 30000) * 1000;
		timeToSleep.tv_nsec = nanoseconds;
		nanosleep(&timeToSleep, NULL);

		nanoseconds = (sendingTime - std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()) * 1000;
		timeToSleep.tv_nsec = nanoseconds;
		nanosleep(&timeToSleep, NULL);
		if(_stopDutyCycleThread) return;

		int64_t timePoint = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

		_physicalInterface->sendPacket(packet);
		_valveState = _newValveState;
		int64_t timePassed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - timePoint;
		GD::out.printDebug("Debug: HomeMatic BidCoS peer " + std::to_string(_peerID) + ": Sending took " + std::to_string(timePassed) + "ms.");
	}
    catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

int32_t HmCcTc::getAdjustmentCommand(int32_t peerAddress)
{
	try
	{
		if(_decalcification[peerAddress])
		{
			_decalcification[peerAddress] = false;
			return 4;
		}
		else if(_newValveState == 0) return 2; //OFF
		else if(_newValveState == 255) return 3; //ON
		else
		{
			if(_newValveState != _valveState) return 3; else return 0;
		}
	}
    catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return 0;
}

int32_t HmCcTc::getNextDutyCycleDeviceAddress()
{
	try
	{
		_peersMutex.lock();
		if(_peers.size() == 0)
		{
			_peersMutex.unlock();
			return -1;
		}
		int i = 0;
		std::unordered_map<int32_t, std::vector<std::shared_ptr<BaseLib::Systems::BasicPeer>>>::iterator j = (_currentDutyCycleDeviceAddress == -1) ? _peers.begin() : _peers.find(_currentDutyCycleDeviceAddress);
		if(j == _peers.end()) //_currentDutyCycleDeviceAddress does not exist anymore in peers
		{
			j = _peers.begin();
		}
		while(i <= (signed)_peers.size()) //<= because it might be there is only one HM-CC-VD
		{
			j++;
			if(j == _peers.end())
			{
				j = _peers.begin();
			}
			if(!j->second.empty())
			{
				_currentDutyCycleDeviceAddress = j->first;
				_peersMutex.unlock();
				return _currentDutyCycleDeviceAddress;
			}
		}
	}
    catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
	_peersMutex.unlock();
	return -1;
}

}