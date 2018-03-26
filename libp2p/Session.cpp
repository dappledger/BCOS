/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Session.cpp
 * @author Gav Wood <i@gavwood.com>
 * @author Alex Leverington <nessence@gmail.com>
 * @date 2014
 */

#include "Session.h"

#include <chrono>
#include <libdevcore/Common.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/Exceptions.h>
#include "Host.h"
#include "Capability.h"
#include <libdevcore/easylog.h>
using namespace std;
using namespace dev;
using namespace dev::p2p;

Session::Session(Host* _h, unique_ptr<RLPXFrameCoder>&& _io, std::shared_ptr<RLPXSocket> const& _s, std::shared_ptr<Peer> const& _n, PeerSessionInfo _info):
	m_server(_h),
	m_io(move(_io)),
	m_socket(_s),
	m_peer(_n),
	m_info(_info),
	m_ping(chrono::steady_clock::time_point::max())
{
	registerFraming(0);
	m_peer->m_lastDisconnect = NoDisconnect;
	m_lastReceived = m_connect = chrono::steady_clock::now();
	DEV_GUARDED(x_info)
	m_info.socketId = m_socket->ref().native_handle();
}

Session::~Session()
{
	ThreadContext tc(info().id.abridged());
	ThreadContext tc2(info().clientVersion);
	LOG(INFO) << "Closing peer session :-(";
	m_peer->m_lastConnected = m_peer->m_lastAttempted - chrono::seconds(1);

	// Read-chain finished for one reason or another.
	for (auto& i : m_capabilities)
		i.second.reset();

	try
	{
		bi::tcp::socket& socket = m_socket->ref();
		if (socket.is_open())
		{
			boost::system::error_code ec;
			socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
			socket.close();
		}
	}
	catch (...) {}

	if (m_CABaseData)
	{
		delete m_CABaseData;
		m_CABaseData = nullptr;
	}
}

ReputationManager& Session::repMan()
{
	return m_server->repMan();
}

NodeID Session::id() const
{
	return m_peer ? m_peer->id : NodeID();
}

void Session::addRating(int _r)
{
	if (m_peer)
	{
		m_peer->m_rating += _r;
		m_peer->m_score += _r;
		if (_r >= 0)
			m_peer->noteSessionGood();
	}
}

int Session::rating() const
{
	return m_peer->m_rating;
}

template <class T> vector<T> randomSelection(vector<T> const& _t, unsigned _n)
{
	if (_t.size() <= _n)
		return _t;
	vector<T> ret = _t;
	while (ret.size() > _n)
	{
		auto i = ret.begin();
		advance(i, rand() % ret.size());
		ret.erase(i);
	}
	return ret;
}

// 收到数据包
bool Session::readPacket(uint16_t _capId, PacketType _t, RLP const& _r)
{
	m_lastReceived = chrono::steady_clock::now();
	//LOG(INFO) << _t << _r;
	try // Generic try-catch block designed to capture RLP format errors - TODO: give decent diagnostics, make a bit more specific over what is caught.
	{
		// v4 frame headers are useless, offset packet type used
		// v5 protocol type is in header, packet type not offset
		if (_capId == 0 && _t < UserPacket)
			return interpret(_t, _r);

		if (isFramingEnabled())
		{
			for (auto const& i : m_capabilities)
				if (i.second->c_protocolID == _capId)
					return i.second->m_enabled ? i.second->interpret(_t, _r) : true;
		}
		else
		{
			for (auto const& i : m_capabilities)
				if (_t >= (int)i.second->m_idOffset && _t - i.second->m_idOffset < i.second->hostCapability()->messageCount())
					return i.second->m_enabled ? i.second->interpret(_t - i.second->m_idOffset, _r) : true;
		}

		return false;
	}
	catch (std::exception const& _e)
	{
		LOG(WARNING) << "Exception caught in p2p::Session::interpret(): " << _e.what() << ". PacketType: " << _t << ". RLP: " << _r;
		disconnect(BadProtocol);
		return true;
	}
	return true;
}

bool Session::interpret(PacketType _t, RLP const& _r)
{
	switch (_t)
	{
	case DisconnectPacket:
	{
		string reason = "Unspecified";
		auto r = (DisconnectReason)_r[0].toInt<int>();
		if (!_r[0].isInt())
			drop(BadProtocol);
		else
		{
			reason = reasonOf(r);
			LOG(WARNING) << "Disconnect (reason: " << reason << ")";
			drop(DisconnectRequested);
		}
		break;
	}
	case PingPacket:
	{
		LOG(INFO) << "Recv Ping " << m_info.id;
		RLPStream s;
		sealAndSend(prep(s, PongPacket), 0);
		break;
	}
	case PongPacket:
		DEV_GUARDED(x_info)
		{
			m_info.lastPing = std::chrono::steady_clock::now() - m_ping;
			LOG(INFO) << "Recv Pong Latency: " << chrono::duration_cast<chrono::milliseconds>(m_info.lastPing).count() << " ms" << m_info.id;
		}
		break;
	case GetPeersPacket:
	case PeersPacket:
		break;
	default:
		return false;
	}
	return true;
}

void Session::ping()
{
	RLPStream s;
	sealAndSend(prep(s, PingPacket), 0);
	m_ping = std::chrono::steady_clock::now();
}

RLPStream& Session::prep(RLPStream& _s, PacketType _id, unsigned _args)
{
	return _s.append((unsigned)_id).appendList(_args);
}

void Session::sealAndSend(RLPStream& _s, uint16_t _protocolID)
{
	bytes b;
	_s.swapOut(b);
	send(move(b), _protocolID);
}

bool Session::checkPacket(bytesConstRef _msg)
{
	if (_msg[0] > 0x7f || _msg.size() < 2)
		return false;
	if (RLP(_msg.cropped(1)).actualSize() + 1 != _msg.size())
		return false;
	return true;
}

void Session::send(bytes&& _msg, uint16_t _protocolID)
{
	bytesConstRef msg(&_msg);
	//LOG(TRACE) << RLP(msg.cropped(1));
	if (!checkPacket(msg))
		LOG(WARNING) << "INVALID PACKET CONSTRUCTED!";

	if (!m_socket->ref().is_open())
		return;

	bool doWrite = false;
	if (isFramingEnabled())
	{
		DEV_GUARDED(x_framing)
		{
			doWrite = m_encFrames.empty();
			auto f = getFraming(_protocolID);
			if (!f)
				return;

			f->writer.enque(RLPXPacket(_protocolID, msg));
			multiplexAll();
		}

		if (doWrite)
			writeFrames();
	}
	else
	{
		DEV_GUARDED(x_framing)
		{
			m_writeQueue.push_back(std::move(_msg));
			m_writeTimeQueue.push_back(utcTime());
			doWrite = (m_writeQueue.size() == 1);
		}

		if (doWrite)
			write();
	}
}

void Session::write()
{
	bytes const* out = nullptr;
	u256 enter_time = 0;
	DEV_GUARDED(x_framing)
	{
		m_io->writeSingleFramePacket(&m_writeQueue[0], m_writeQueue[0]);
		out = &m_writeQueue[0];
		enter_time = m_writeTimeQueue[0];
	}
	auto self(shared_from_this());
	m_start_t = utcTime();
	unsigned queue_elapsed = (unsigned)(m_start_t - enter_time);
	if (queue_elapsed > 10) {
		LOG(WARNING) << "Session::write queue-time=" << queue_elapsed;
	}

	auto asyncWrite = [this, self](boost::system::error_code ec, std::size_t length)
	{

		unsigned elapsed = (unsigned)(utcTime() - m_start_t);
		if (elapsed >= 10) {
			LOG(WARNING) << "ba::async_write write-time=" << elapsed << ",len=" << length << ",id=" << id();
		}
		ThreadContext tc(info().id.abridged());
		ThreadContext tc2(info().clientVersion);
		// must check queue, as write callback can occur following dropped()
		if (ec)
		{
			LOG(WARNING) << "Error sending: " << ec.message();
			drop(TCPError);
			return;
		}

		DEV_GUARDED(x_framing)
		{
			m_writeQueue.pop_front();
			m_writeTimeQueue.pop_front();
			if (m_writeQueue.empty())
				return;
		}
		write();
	};

	if (m_socket->getSocketType() == SSL_SOCKET)
	{
		ba::async_write(m_socket->sslref(), ba::buffer(*out), asyncWrite);
	}
	else
	{
		ba::async_write(m_socket->ref(), ba::buffer(*out), asyncWrite);
	}
}

void Session::writeFrames()
{
	bytes const* out = nullptr;
	DEV_GUARDED(x_framing)
	{
		if (m_encFrames.empty())
			return;
		else
			out = &m_encFrames[0];
	}

	m_start_t = utcTime();
	auto self(shared_from_this());

	auto asyncWrite = [this, self](boost::system::error_code ec, std::size_t length)
	{
		unsigned elapsed = (unsigned)(utcTime() - m_start_t);
		if (elapsed >= 10) {
			LOG(WARNING) << "ba::async_write write-time=" << elapsed << ",len=" << length << ",id=" << id();
		}
		ThreadContext tc(info().id.abridged());
		ThreadContext tc2(info().clientVersion);
		// must check queue, as write callback can occur following dropped()
		if (ec)
		{
			LOG(WARNING) << "Error sending: " << ec.message();
			drop(TCPError);
			return;
		}

		DEV_GUARDED(x_framing)
		{
			if (!m_encFrames.empty())
				m_encFrames.pop_front();

			multiplexAll();
			if (m_encFrames.empty())
				return;
		}

		writeFrames();
	};

	if (m_socket->getSocketType() == SSL_SOCKET)
	{
		ba::async_write(m_socket->sslref(), ba::buffer(*out), asyncWrite);
	}
	else
	{
		ba::async_write(m_socket->ref(), ba::buffer(*out), asyncWrite);
	}
}

void Session::drop(DisconnectReason _reason)
{
	if (m_dropped)
		return;
	bi::tcp::socket& socket = m_socket->ref();
	if (socket.is_open())
		try
		{
			boost::system::error_code ec;
			LOG(WARNING) << "Closing " << socket.remote_endpoint(ec) << "(" << reasonOf(_reason) << ")";
			socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
			socket.close();
		}
		catch (...) {}

	m_peer->m_lastDisconnect = _reason;
	if (_reason == BadProtocol)
	{
		m_peer->m_rating /= 2;
		m_peer->m_score /= 2;
	}
	m_dropped = true;
}

void Session::disconnect(DisconnectReason _reason)
{
	LOG(WARNING) << "Disconnecting (our reason:" << reasonOf(_reason) << ")";

	if (m_socket->ref().is_open())
	{
		RLPStream s;
		prep(s, DisconnectPacket, 1) << (int)_reason;
		sealAndSend(s, 0);
	}
	drop(_reason);
}

void Session::start()
{
	ping();

	if (isFramingEnabled())
		doReadFrames();
	else
		doRead();
}

void Session::doRead()
{
	// ignore packets received while waiting to disconnect.
	if (m_dropped)
		return;
	auto self(shared_from_this());
	m_data.resize(h256::size);

	auto asyncRead = [this, self](boost::system::error_code ec, std::size_t length)
	{
		ThreadContext tc(info().id.abridged());
		ThreadContext tc2(info().clientVersion);
		if (!checkRead(h256::size, ec, length))
			return;
		else if (!m_io->authAndDecryptHeader(bytesRef(m_data.data(), length)))
		{
			LOG(WARNING) << "header decrypt failed";
			drop(BadProtocol); // todo: better error
			return;
		}

		uint16_t hProtocolId;
		uint32_t hLength;
		uint8_t hPadding;
		try
		{
			RLPXFrameInfo header(bytesConstRef(m_data.data(), length));
			hProtocolId = header.protocolId;
			hLength = header.length;
			hPadding = header.padding;
		}
		catch (std::exception const& _e)
		{
			//LOG(WARNING) << "Exception decoding frame header RLP:" << _e.what() << bytesConstRef(m_data.data(), h128::size).cropped(3);
			LOG(WARNING) << "Exception decoding frame header RLP:" << _e.what() << bytesConstRef(m_data.data(), h128::size).cropped(3).toString();
			drop(BadProtocol);
			return;
		}

		/// read padded frame and mac
		auto tlen = hLength + hPadding + h128::size;
		m_data.resize(tlen);

		auto _asyncRead = [this, self, hLength, hProtocolId, tlen](boost::system::error_code ec, std::size_t length)
		{
			ThreadContext tc(info().id.abridged());
			ThreadContext tc2(info().clientVersion);
			if (!checkRead(tlen, ec, length))
				return;
			else if (!m_io->authAndDecryptFrame(bytesRef(m_data.data(), tlen)))
			{
				LOG(WARNING) << "frame decrypt failed";
				drop(BadProtocol); // todo: better error
				return;
			}

			bytesConstRef frame(m_data.data(), hLength);
			if (!checkPacket(frame))
			{
				LOG(WARNING) << "Received " << frame.size() << ": " << toHex(frame) << "\n";
				LOG(WARNING) << "INVALID MESSAGE RECEIVED";
				disconnect(BadProtocol);
				return;
			}
			else
			{
				auto packetType = (PacketType)RLP(frame.cropped(0, 1)).toInt<unsigned>();
				RLP r(frame.cropped(1));
				bool ok = readPacket(hProtocolId, packetType, r);
				(void)ok;
#if ETH_DEBUG
				if (!ok)
					LOG(WARNING) << "Couldn't interpret packet." << RLP(r);
#endif
			}
			doRead();
		};

		if (m_socket->getSocketType() == SSL_SOCKET)
		{
			ba::async_read(m_socket->sslref(), boost::asio::buffer(m_data, tlen), _asyncRead);
		}
		else
		{
			ba::async_read(m_socket->ref(), boost::asio::buffer(m_data, tlen), _asyncRead);
		}
	};

	if (m_socket->getSocketType() == SSL_SOCKET)
	{
		ba::async_read(m_socket->sslref(), boost::asio::buffer(m_data, h256::size), asyncRead);
	}
	else
	{
		ba::async_read(m_socket->ref(), boost::asio::buffer(m_data, h256::size), asyncRead);
	}
}

bool Session::checkRead(std::size_t _expected, boost::system::error_code _ec, std::size_t _length)
{
	if (_ec && _ec.category() != boost::asio::error::get_misc_category() && _ec.value() != boost::asio::error::eof)
	{
		LOG(WARNING) << "Error reading: " << _ec.message();
		drop(TCPError);
		return false;
	}
	else if (_ec && _length < _expected)
	{
		LOG(WARNING) << "Error reading - Abrupt peer disconnect: " << _ec.message();
		repMan().noteRude(*this);
		drop(TCPError);
		return false;
	}
	else if (_length != _expected)
	{
		// with static m_data-sized buffer this shouldn't happen unless there's a regression
		// sec recommends checking anyways (instead of assert)
		LOG(WARNING) << "Error reading - TCP read buffer length differs from expected frame size.";
		disconnect(UserReason);
		return false;
	}

	return true;
}

void Session::doReadFrames()
{
	if (m_dropped)
		return; // ignore packets received while waiting to disconnect

	auto self(shared_from_this());
	m_data.resize(h256::size);

	auto asyncRead = [this, self](boost::system::error_code ec, std::size_t length)
	{
		ThreadContext tc(info().id.abridged());
		ThreadContext tc2(info().clientVersion);
		if (!checkRead(h256::size, ec, length))
			return;

		DEV_GUARDED(x_framing)
		{
			if (!m_io->authAndDecryptHeader(bytesRef(m_data.data(), length)))
			{
				LOG(WARNING) << "header decrypt failed";
				drop(BadProtocol); // todo: better error
				return;
			}
		}

		bytesConstRef rawHeader(m_data.data(), length);
		try
		{
			RLPXFrameInfo tmpHeader(rawHeader);
		}
		catch (std::exception const& _e)
		{
			LOG(WARNING) << "Exception decoding frame header RLP:" << _e.what() ;//<< bytesConstRef(m_data.data(), h128::size).cropped(3);
			drop(BadProtocol);
			return;
		}

		RLPXFrameInfo header(rawHeader);
		auto tlen = header.length + header.padding + h128::size; // padded frame and mac
		m_data.resize(tlen);
		auto _asyncRead = [this, self, tlen, header](boost::system::error_code ec, std::size_t length)
		{
			ThreadContext tc(info().id.abridged());
			ThreadContext tc2(info().clientVersion);
			if (!checkRead(tlen, ec, length))
				return;

			bytesRef frame(m_data.data(), tlen);
			vector<RLPXPacket> px;
			DEV_GUARDED(x_framing)
			{
				auto f = getFraming(header.protocolId);
				if (!f)
				{
					LOG(WARNING) << "Unknown subprotocol " << header.protocolId;
					drop(BadProtocol);
					return;
				}

				auto v = f->reader.demux(*m_io, header, frame);
				px.swap(v);
			}

			for (RLPXPacket& p : px)
			{
				PacketType packetType = (PacketType)RLP(p.type()).toInt<unsigned>(RLP::AllowNonCanon);
				bool ok = readPacket(header.protocolId, packetType, RLP(p.data()));
// #if ETH_DEBUG
// 				if (!ok)
// 					LOG(el::Level::Warning) << "Couldn't interpret packet." << RLP(p.data());
// #endif
				ok = true;
				(void)ok;
			}
			doReadFrames();
		};

		if (m_socket->getSocketType() == SSL_SOCKET)
		{
			ba::async_read(m_socket->sslref(), boost::asio::buffer(m_data, tlen), _asyncRead);
		}
		else
		{
			ba::async_read(m_socket->ref(), boost::asio::buffer(m_data, tlen), _asyncRead);
		}
	};
	if (m_socket->getSocketType() == SSL_SOCKET)
	{
		ba::async_read(m_socket->sslref(), boost::asio::buffer(m_data, h256::size), asyncRead);
	}
	else
	{
		ba::async_read(m_socket->ref(), boost::asio::buffer(m_data, h256::size), asyncRead);
	}
}

std::shared_ptr<Session::Framing> Session::getFraming(uint16_t _protocolID)
{
	if (m_framing.find(_protocolID) == m_framing.end())
		return nullptr;
	else
		return m_framing[_protocolID];
}

void Session::registerCapability(CapDesc const& _desc, std::shared_ptr<Capability> _p)
{
	DEV_GUARDED(x_framing)
	{
		m_capabilities[_desc] = _p;
	}
}

void Session::registerFraming(uint16_t _id)
{
	DEV_GUARDED(x_framing)
	{
		if (m_framing.find(_id) == m_framing.end())
		{
			std::shared_ptr<Session::Framing> f(new Session::Framing(_id));
			m_framing[_id] = f;
		}
	}
}

void Session::multiplexAll()
{
	for (auto& f : m_framing)
		f.second->writer.mux(*m_io, maxFrameSize(), m_encFrames);
}

CABaseData* Session::getCABaseData()
{
	return m_CABaseData;
}

void Session::saveCABaseData(CABaseData* baseData)
{
	m_CABaseData = baseData;
}

bool Session::setStatistics(dev::InterfaceStatistics *stats)
{
		if (stats && m_statistics.get() == nullptr)
		{
			m_statistics.reset(stats);
			return true;
		}
		return false;
}
