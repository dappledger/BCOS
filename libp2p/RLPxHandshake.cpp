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
/** @file RLPXHandshake.cpp
 * @author Alex Leverington <nessence@gmail.com>
 * @date 2015
 */

#include "Host.h"
#include "Session.h"
#include "Peer.h"
#include "RLPxHandshake.h"
#include "libethereum/NodeConnParamsManager.h"
#include "libdevcrypto/Rsa.h"
#include "libdevcore/Hash.h"
#include <fstream>

using namespace std;
using namespace dev;
using namespace dev::p2p;
using namespace dev::crypto;

void RLPXHandshake::writeAuth()
{
	LOG(INFO) << "p2p.connect.egress sending auth to " << m_socket->remoteEndpoint();
	m_auth.resize(Signature::size + h256::size + Public::size + h256::size + 1);
	bytesRef sig(&m_auth[0], Signature::size);
	bytesRef hepubk(&m_auth[Signature::size], h256::size);
	bytesRef pubk(&m_auth[Signature::size + h256::size], Public::size);
	bytesRef nonce(&m_auth[Signature::size + h256::size + Public::size], h256::size);

	// E(remote-pubk, S(ecdhe-random, ecdh-shared-secret^nonce) || H(ecdhe-random-pubk) || pubk || nonce || 0x0)
	Secret staticShared;
	crypto::ecdh::agree(m_host->m_alias.sec(), m_remote, staticShared);//m_remote:nodeid   使用对方节点nodeid加密对称密钥
	sign(m_ecdhe.seckey(), staticShared.makeInsecure() ^ m_nonce).ref().copyTo(sig);
	sha3(m_ecdhe.pubkey().ref(), hepubk);//公钥数据做sha3处理
	m_host->m_alias.pub().ref().copyTo(pubk);//公钥数据
	m_nonce.ref().copyTo(nonce);
	m_auth[m_auth.size() - 1] = 0x0;
	encryptECIES(m_remote, &m_auth, m_authCipher);

	auto self(shared_from_this());
	auto asyncWrite = [this, self](boost::system::error_code ec, std::size_t)
	{
		transition(ec);
	};

	if (m_socket->getSocketType() == SSL_SOCKET)
	{
		ba::async_write(m_socket->sslref(), ba::buffer(m_authCipher), asyncWrite);
	}
	else
	{
		ba::async_write(m_socket->ref(), ba::buffer(m_authCipher), asyncWrite);
	}
}

void RLPXHandshake::writeAck()
{
	LOG(INFO) << "p2p.connect.ingress sending ack to " << m_socket->remoteEndpoint();
	m_ack.resize(Public::size + h256::size + 1);
	bytesRef epubk(&m_ack[0], Public::size);
	bytesRef nonce(&m_ack[Public::size], h256::size);
	m_ecdhe.pubkey().ref().copyTo(epubk);//公钥数据
	m_nonce.ref().copyTo(nonce);//nonce值
	m_ack[m_ack.size() - 1] = 0x0;
	encryptECIES(m_remote, &m_ack, m_ackCipher);

	auto self(shared_from_this());
	auto asyncWrite = [this, self](boost::system::error_code ec, std::size_t)
	{
		transition(ec);
	};

	if (m_socket->getSocketType() == SSL_SOCKET)
	{
		ba::async_write(m_socket->sslref(), ba::buffer(m_ackCipher), asyncWrite);
	}
	else
	{
		ba::async_write(m_socket->ref(), ba::buffer(m_ackCipher), asyncWrite);
	}
}

void RLPXHandshake::writeAckEIP8()
{
	LOG(INFO) << "p2p.connect.ingress sending EIP-8 ack to " << m_socket->remoteEndpoint();

	RLPStream rlp;
	rlp.appendList(3)
	        << m_ecdhe.pubkey()
	        << m_nonce
	        << c_rlpxVersion;
	m_ack = rlp.out();
	int padAmount(rand() % 100 + 100);
	m_ack.resize(m_ack.size() + padAmount, 0);

	bytes prefix(2);
	toBigEndian<uint16_t>(m_ack.size() + c_eciesOverhead, prefix);
	encryptECIES(m_remote, bytesConstRef(&prefix), &m_ack, m_ackCipher);
	m_ackCipher.insert(m_ackCipher.begin(), prefix.begin(), prefix.end());

	auto self(shared_from_this());
	auto asyncWrite = [this, self](boost::system::error_code ec, std::size_t)
	{
		transition(ec);
	};
	if (m_socket->getSocketType() == SSL_SOCKET)
	{
		ba::async_write(m_socket->sslref(), ba::buffer(m_ackCipher), asyncWrite);
	}
	else
	{
		ba::async_write(m_socket->ref(), ba::buffer(m_ackCipher), asyncWrite);
	}
}

void RLPXHandshake::setAuthValues(Signature const& _sig, Public const& _remotePubk, h256 const& _remoteNonce, uint64_t _remoteVersion)
{
	_remotePubk.ref().copyTo(m_remote.ref());
	_remoteNonce.ref().copyTo(m_remoteNonce.ref());
	m_remoteVersion = _remoteVersion;
	Secret sharedSecret;
	crypto::ecdh::agree(m_host->m_alias.sec(), _remotePubk, sharedSecret);
	m_remoteEphemeral = recover(_sig, sharedSecret.makeInsecure() ^ _remoteNonce);
}

void RLPXHandshake::readAuth()
{
	LOG(INFO) << "p2p.connect.ingress receiving auth from " << m_socket->remoteEndpoint();
	m_authCipher.resize(307);
	auto self(shared_from_this());
	auto asyncRead = [this, self](boost::system::error_code ec, std::size_t)
	{
		if (ec)
			transition(ec);
		else if (decryptECIES(m_host->m_alias.sec(), bytesConstRef(&m_authCipher), m_auth))
		{
			bytesConstRef data(&m_auth);
			Signature sig(data.cropped(0, Signature::size));
			Public pubk(data.cropped(Signature::size + h256::size, Public::size));
			h256 nonce(data.cropped(Signature::size + h256::size + Public::size, h256::size));
			setAuthValues(sig, pubk, nonce, 4);
			transition();
		}
		else
			readAuthEIP8();
	};

	if (m_socket->getSocketType() == SSL_SOCKET)
	{
		ba::async_read(m_socket->sslref(), ba::buffer(m_authCipher, 307), asyncRead);
	}
	else
	{
		ba::async_read(m_socket->ref(), ba::buffer(m_authCipher, 307), asyncRead);
	}
}

void RLPXHandshake::readAuthEIP8()
{
	assert(m_authCipher.size() == 307);
	uint16_t size(m_authCipher[0] << 8 | m_authCipher[1]);
	LOG(INFO) << "p2p.connect.ingress receiving " << size << "bytes EIP-8 auth from " << m_socket->remoteEndpoint();
	m_authCipher.resize((size_t)size + 2);
	auto rest = ba::buffer(ba::buffer(m_authCipher) + 307);
	auto self(shared_from_this());
	auto asyncRead = [this, self](boost::system::error_code ec, std::size_t)
	{
		bytesConstRef ct(&m_authCipher);
		if (ec)
			transition(ec);
		else if (decryptECIES(m_host->m_alias.sec(), ct.cropped(0, 2), ct.cropped(2), m_auth))
		{
			RLP rlp(m_auth, RLP::ThrowOnFail | RLP::FailIfTooSmall);
			setAuthValues(
			    rlp[0].toHash<Signature>(),
			    rlp[1].toHash<Public>(),
			    rlp[2].toHash<h256>(),
			    rlp[3].toInt<uint64_t>()
			);
			m_nextState = AckAuthEIP8;
			transition();
		}
		else
		{
			LOG(INFO) << "p2p.connect.ingress auth decrypt failed for" << m_socket->remoteEndpoint();
			m_nextState = Error;
			transition();
		}
	};

	if (m_socket->getSocketType() == SSL_SOCKET)
	{
		ba::async_read(m_socket->sslref(), rest, asyncRead);
	}
	else
	{
		ba::async_read(m_socket->ref(), rest, asyncRead);
	}
}

void RLPXHandshake::readAck()
{
	LOG(INFO) << "p2p.connect.egress receiving ack from " << m_socket->remoteEndpoint();
	m_ackCipher.resize(210);
	auto self(shared_from_this());
	auto asyncRead = [this, self](boost::system::error_code ec, std::size_t)
	{
		if (ec)
			transition(ec);
		else if (decryptECIES(m_host->m_alias.sec(), bytesConstRef(&m_ackCipher), m_ack))
		{
			bytesConstRef(&m_ack).cropped(0, Public::size).copyTo(m_remoteEphemeral.ref());
			bytesConstRef(&m_ack).cropped(Public::size, h256::size).copyTo(m_remoteNonce.ref());
			m_remoteVersion = 4;
			transition();
		}
		else
			readAckEIP8();
	};

	if (m_socket->getSocketType() == SSL_SOCKET)
	{
		ba::async_read(m_socket->sslref(), ba::buffer(m_ackCipher, 210), asyncRead);
	}
	else
	{
		ba::async_read(m_socket->ref(), ba::buffer(m_ackCipher, 210), asyncRead);
	}
}

void RLPXHandshake::readAckEIP8()
{
	assert(m_ackCipher.size() == 210);
	uint16_t size(m_ackCipher[0] << 8 | m_ackCipher[1]);
	LOG(INFO) << "p2p.connect.egress receiving " << size << "bytes EIP-8 ack from " << m_socket->remoteEndpoint();
	m_ackCipher.resize((size_t)size + 2);
	auto rest = ba::buffer(ba::buffer(m_ackCipher) + 210);
	auto self(shared_from_this());
	auto asyncRead = [this, self](boost::system::error_code ec, std::size_t)
	{
		bytesConstRef ct(&m_ackCipher);
		if (ec)
			transition(ec);
		else if (decryptECIES(m_host->m_alias.sec(), ct.cropped(0, 2), ct.cropped(2), m_ack))
		{
			RLP rlp(m_ack, RLP::ThrowOnFail | RLP::FailIfTooSmall);
			m_remoteEphemeral = rlp[0].toHash<Public>();
			m_remoteNonce = rlp[1].toHash<h256>();
			m_remoteVersion = rlp[2].toInt<uint64_t>();
			transition();
		}
		else
		{
			LOG(INFO) << "p2p.connect.egress ack decrypt failed for " << m_socket->remoteEndpoint();
			m_nextState = Error;
			transition();
		}
	};
	if (m_socket->getSocketType() == SSL_SOCKET)
	{
		ba::async_read(m_socket->sslref(), rest, asyncRead);
	}
	else
	{
		ba::async_read(m_socket->ref(), rest, asyncRead);
	}
}

void RLPXHandshake::writeCASeed()
{
	LOG(INFO) << "p2p.connect.ingress sending CA to " << m_socket->remoteEndpoint();
	int seedKey = rand();
	if (seedKey < pow(10, 7))
	{
		//make sure 8 length bigger
		seedKey += pow(10, 7);
	}
	std::string str = std::to_string(seedKey).substr(0, 8);
	auto self(shared_from_this());
	bytes bytesSeed(str.begin(), str.end());
	m_sendBaseData.setSeed(bytesSeed);
	auto asyncWrite = [this, self](boost::system::error_code ec, std::size_t)
	{
		if (ec)
		{
			transition(ec);
		}

	};

	if (m_socket->getSocketType() == SSL_SOCKET)
	{
		ba::async_write(m_socket->sslref(), ba::buffer(bytesSeed), asyncWrite);
	}
	else
	{
		ba::async_write(m_socket->ref(), ba::buffer(bytesSeed), asyncWrite);
	}
}

void RLPXHandshake::readCASeed()
{
	LOG(INFO) << "p2p.connect.egress receiving CA from " << m_socket->remoteEndpoint();
	m_readBaseData.getSeed().resize(8);
	auto self(shared_from_this());
	auto asyncRead = [this, self](boost::system::error_code ec, std::size_t)
	{
		transition(ec);
	};

	if (m_socket->getSocketType() == SSL_SOCKET)
	{
		ba::async_read(m_socket->sslref(), ba::buffer(m_readBaseData.getSeed()), asyncRead);
	}
	else
	{
		ba::async_read(m_socket->ref(), ba::buffer(m_readBaseData.getSeed()), asyncRead);
	}
}

void RLPXHandshake::cancel()
{
	m_cancel = true;
	m_idleTimer.cancel();
	m_socket->close();
	m_io.reset();
}

void RLPXHandshake::error()
{
	auto connected = m_socket->isConnected();
	if (connected && !m_socket->remoteEndpoint().address().is_unspecified())
		LOG(INFO) << "Disconnecting " << m_socket->remoteEndpoint() << " (Handshake Failed)";
	else
		LOG(INFO) << "Handshake Failed (Connection reset by peer)";

	cancel();
}

void RLPXHandshake::transition(boost::system::error_code _ech)
{
	LOG(INFO) << "开始Handshake";
	// reset timeout
	m_idleTimer.cancel();

	if (_ech || m_nextState == Error || m_cancel)
	{
		LOG(ERROR) << "Handshake Failed (I/O Error:" << _ech.message() << ")";
		return error();
	}

	auto self(shared_from_this());
	assert(m_nextState != StartSession);
	m_idleTimer.expires_from_now(c_timeout);
	m_idleTimer.async_wait([this, self](boost::system::error_code const & _ec)
	{
		if (!_ec)
		{
			if (!m_socket->remoteEndpoint().address().is_unspecified())
				LOG(INFO) << "Disconnecting " << m_socket->remoteEndpoint() << " (Handshake Timeout)";
			cancel();
		}
	});

	if (m_nextState == New)//传输对称密钥
	{
		m_nextState = AckAuth;
		if (m_originated)
			writeAuth();
		else
			readAuth();
	}
	else if (m_nextState == AckAuth)//发送公钥信息及nonce值
	{
		m_nextState = WriteHello;
		if (m_originated)
			readAck();
		else
			writeAck();
	}
	else if (m_nextState == AckAuthEIP8)//发送公钥信息及nonce值
	{
		m_nextState = WriteHello;
		if (m_originated)
			readAck();
		else
			writeAckEIP8();
	}
	else if (m_nextState == StartCA)//发送随机数签名
	{
		m_nextState = WriteHello;
		writeCASeed();//发送随机数
		readCASeed();//读取对方随机数
	}
	else if (m_nextState == WriteHello)//验证发送协议数据
	{
		m_nextState = ReadHello;
		LOG(INFO) << (m_originated ? "p2p.connect.egress" : "p2p.connect.ingress") << "sending capabilities handshake";

		/// This pointer will be freed if there is an error otherwise
		/// it will be passed to Host which will take ownership.
		m_io.reset(new RLPXFrameCoder(*this));

		//协议进行修改,按需扩展字段
		RLPStream s;
		// NodeConnManagerSingleton::GetInstance().ConstructHandShakeRLP(s, m_readBaseData);
		h256 hash;
        if (!NodeConnManagerSingleton::GetInstance().nodeInfoHash(hash))
            return;
		s.append((unsigned)HelloPacket).appendList(6)
		        << dev::p2p::c_protocolVersion
		        << m_host->m_clientVersion
		        << m_host->caps()
		        << m_host->listenPort()
		        << m_host->id()
				<< hash;

		bytes packet;
		s.swapOut(packet);
		m_io->writeSingleFramePacket(&packet, m_handshakeOutBuffer);
		auto asyncWrite = [this, self](boost::system::error_code ec, std::size_t)
		{
			transition(ec);
		};
		if (m_socket->getSocketType() == SSL_SOCKET)
		{
			ba::async_write(m_socket->sslref(), ba::buffer(m_handshakeOutBuffer), asyncWrite);
		}
		else
		{
			ba::async_write(m_socket->ref(), ba::buffer(m_handshakeOutBuffer), asyncWrite);
		}
	}
	else if (m_nextState == ReadHello)
	{
		// Authenticate and decrypt initial hello frame with initial RLPXFrameCoder
		// and request m_host to start session.
		m_nextState = StartSession;

		// read frame header
		unsigned const handshakeSize = 32;
		m_handshakeInBuffer.resize(handshakeSize);

		auto asyncRead = [this, self](boost::system::error_code ec, std::size_t)
		{
			if (ec)
				transition(ec);
			else
			{
				if (!m_io)
				{
					LOG(ERROR) << "Internal error in handshake: RLPXFrameCoder disappeared.";
					m_nextState = Error;
					transition();
					return;

				}
				/// authenticate and decrypt header
				if (!m_io->authAndDecryptHeader(bytesRef(m_handshakeInBuffer.data(), m_handshakeInBuffer.size())))
				{
					m_nextState = Error;
					transition();
					return;
				}

				LOG(INFO) << (m_originated ? "p2p.connect.egress" : "p2p.connect.ingress") << "recvd hello header";

				/// check frame size
				bytes& header = m_handshakeInBuffer;
				uint32_t frameSize = (uint32_t)(header[2]) | (uint32_t)(header[1]) << 8 | (uint32_t)(header[0]) << 16;
				if (frameSize > 1024)
				{
					// all future frames: 16777216
					LOG(ERROR) << (m_originated ? "p2p.connect.egress" : "p2p.connect.ingress") << "hello frame is too large" << frameSize;
					m_nextState = Error;
					transition();
					return;
				}

				/// rlp of header has protocol-type, sequence-id[, total-packet-size]
				bytes headerRLP(header.size() - 3 - h128::size);	// this is always 32 - 3 - 16 = 13. wtf?
				bytesConstRef(&header).cropped(3).copyTo(&headerRLP);

				/// read padded frame and mac
				m_handshakeInBuffer.resize(frameSize + ((16 - (frameSize % 16)) % 16) + h128::size);
				auto _asyncRead = [this, self, headerRLP](boost::system::error_code ec, std::size_t)
				{
					m_idleTimer.cancel();

					if (ec)
						transition(ec);
					else
					{
						if (!m_io)
						{
							LOG(ERROR) << "Internal error in handshake: RLPXFrameCoder disappeared.";
							m_nextState = Error;
							transition();
							return;

						}
						bytesRef frame(&m_handshakeInBuffer);
						if (!m_io->authAndDecryptFrame(frame))
						{
							LOG(ERROR) << (m_originated ? "p2p.connect.egress" : "p2p.connect.ingress") << "hello frame: decrypt failed";
							m_nextState = Error;
							transition();
							return;
						}

						PacketType packetType = frame[0] == 0x80 ? HelloPacket : (PacketType)frame[0];
						if (packetType != HelloPacket)
						{
							LOG(ERROR) << (m_originated ? "p2p.connect.egress" : "p2p.connect.ingress") << "hello frame: invalid packet type:" << packetType;
							m_nextState = Error;
							transition();
							return;
						}

						LOG(INFO) << (m_originated ? "p2p.connect.egress" : "p2p.connect.ingress") << "hello frame: success. starting session.";
						try
						{
							RLP rlp(frame.cropped(1), RLP::ThrowOnFail | RLP::FailIfTooSmall);
							m_host->startPeerSession(m_remote, rlp, move(m_io), m_socket, m_sendBaseData);// 回调
						}
						catch (std::exception const& _e)
						{
							LOG(ERROR) << "Handshake causing an exception:" << _e.what();
							m_nextState = Error;
							transition();
						}
					}
				};
				if (m_socket->getSocketType() == SSL_SOCKET)
				{
					ba::async_read(m_socket->sslref(), boost::asio::buffer(m_handshakeInBuffer, m_handshakeInBuffer.size()), _asyncRead);
				}
				else
				{
					ba::async_read(m_socket->ref(), boost::asio::buffer(m_handshakeInBuffer, m_handshakeInBuffer.size()), _asyncRead);
				}
			}
		};

		if (m_socket->getSocketType() == SSL_SOCKET)
		{
			ba::async_read(m_socket->sslref(), boost::asio::buffer(m_handshakeInBuffer, handshakeSize), asyncRead);
		}
		else
		{
			ba::async_read(m_socket->ref(), boost::asio::buffer(m_handshakeInBuffer, handshakeSize), asyncRead);
		}
	}
}
