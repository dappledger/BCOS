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
/** @file TransactionBase.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include <libdevcore/vector_ref.h>
#include <libdevcore/easylog.h>
#include <libdevcore/CommonIO.h>
#include <libdevcrypto/Common.h>
#include <libevmcore/EVMSchedule.h>
#include <libethcore/Exceptions.h>
#include <libdevcore/easylog.h>
#include <libethcore/CommonJS.h>
#include <libweb3jsonrpc/JsonHelper.h>
#include <abi/ContractAbiMgr.h>
#include "Transaction.h"

using namespace std;
using namespace dev;
using namespace dev::eth;

u256 TransactionBase::maxGas = 30000000; //默认二千万

TransactionBase::TransactionBase(TransactionSkeleton const& _ts, Secret const& _s):
	m_type(_ts.creation ? ContractCreation : MessageCall),
	m_randomid(_ts.randomid),
	m_value(_ts.value),
	m_receiveAddress(_ts.to),
	m_gasPrice(_ts.gasPrice),
	m_gas(_ts.gas),
	m_blockLimit(_ts.blockLimit),
	m_data(_ts.data),
	m_sender(_ts.from)
{
	if (_s)
		sign(_s);
}

TransactionBase::TransactionBase(bytesConstRef _rlpData, CheckTransaction _checkSig)
{
	int field = 0;
	RLP rlp(_rlpData);
	try
	{
		if (!rlp.isList())
			BOOST_THROW_EXCEPTION(InvalidTransactionFormat() << errinfo_comment("transaction RLP must be a list"));

		if (rlp.itemCount() < 10)
		{
			LOG(WARNING) << "to little fields in the tracsaction RLP ,size=" << rlp.itemCount();
			BOOST_THROW_EXCEPTION(InvalidTransactionFormat() << errinfo_comment("to little fields in the transaction RLP"));
		}

		int index = 0;

		m_randomid = rlp[field = (index++)].toInt<u256>(); //0
		m_gasPrice = rlp[field = (index++)].toInt<u256>(); //1
		m_gas = rlp[field = (index++)].toInt<u256>();      //2
		m_blockLimit = rlp[field = (index++)].toInt<u256>();//新加的  //3

		auto tempRLP = rlp[field = (index++)];  //4

		m_receiveAddress = tempRLP.isEmpty() ? Address() : tempRLP.toHash<Address>(RLP::VeryStrict);
		//m_type = rlp[field = 4].isEmpty() ? ContractCreation : MessageCall;
		m_value = rlp[field = (index++)].toInt<u256>(); //5

		//if (!rlp[field = 6].isData())
		//	BOOST_THROW_EXCEPTION(InvalidTransactionFormat() << errinfo_comment("transaction data RLP must be an array"));
		auto dataRLP = rlp[field = (index++)];
		m_data = dataRLP.toBytes();

		if (fromJsonGetParams(dataRLP.toString(), m_params))
		{//判断是否是name方式调用
			//name调用方式
			m_type          = MessageCall;
			m_isCalldByName = true;

			LOG(DEBUG) << "[TransactionBase] name|func|version|params=>"
				<< m_params.strContractName << "|"
				<< m_params.strFunc << "|"
				<< m_params.strVersion << "|"
				<< m_params.jParams.toStyledString()
				;
		}
		else
		{
			//说明data字段不是json字符串，按照一般的调用方式处理
			m_type = (m_receiveAddress == Address() ? ContractCreation : MessageCall);
		}

		byte v = rlp[field = (index++)].toInt<byte>();
		h256 r = rlp[field = (index++)].toInt<u256>();
		h256 s = rlp[field = (index++)].toInt<u256>();

		if (v > 36)
			m_chainId = (v - 35) / 2;
		else if (v == 27 || v == 28)
			m_chainId = -4;
		else
			BOOST_THROW_EXCEPTION(InvalidSignature());

		v = v - (m_chainId * 2 + 35);

		//if (rlp.itemCount() > 10)
		//	BOOST_THROW_EXCEPTION(InvalidTransactionFormat() << errinfo_comment("to many fields in the transaction RLP"));

		m_vrs = SignatureStruct{ r, s, v };
		if (_checkSig >= CheckTransaction::Cheap && !m_vrs.isValid()) {

			BOOST_THROW_EXCEPTION(InvalidSignature());
		}

		if (_checkSig == CheckTransaction::Everything) {
			m_sender = sender();
		}
	}
	catch (Exception& _e)
	{
		_e << errinfo_name("invalid transaction format: " + toString(rlp) + " RLP: " + toHex(rlp.data()));
		throw;
	}
}

Address TransactionBase::receiveAddress() const
{
	if (m_isCalldByName)
	{
		if (!m_isGetAddrAndData)
		{
			m_nameCallAddrAndData = libabi::ContractAbiMgr::getInstance()->getAddrAndDataInfo(m_params.strContractName, m_params.strFunc, m_params.strVersion, m_params.jParams);
			m_isGetAddrAndData = true;

			LOG(INFO) << "[TransactionBase::receiveAddress] addr|data=>"
				<< m_nameCallAddrAndData.first.hex() << "|"
				<< m_nameCallAddrAndData.second.size()
				;
		}

		return m_nameCallAddrAndData.first;
	}

	return m_receiveAddress;
}
							   /// Synonym for receiveAddress().
Address TransactionBase::to() const
{
	if (m_isCalldByName)
	{
		if (!m_isGetAddrAndData)
		{
			m_nameCallAddrAndData = libabi::ContractAbiMgr::getInstance()->getAddrAndDataInfo(m_params.strContractName, m_params.strFunc, m_params.strVersion, m_params.jParams);
			m_isGetAddrAndData = true;

			LOG(INFO) << "[TransactionBase::to] addr|data=>"
				<< m_nameCallAddrAndData.first.hex() << "|"
				<< m_nameCallAddrAndData.second.size()
				;
		}

		return m_nameCallAddrAndData.first;
	}

	return m_receiveAddress;
}

bytes const&  TransactionBase::data() const
{
	if (m_isCalldByName)
	{
		if (!m_isGetAddrAndData)
		{
			m_nameCallAddrAndData = libabi::ContractAbiMgr::getInstance()->getAddrAndDataInfo(m_params.strContractName, m_params.strFunc, m_params.strVersion, m_params.jParams);
			m_isGetAddrAndData = true;

			LOG(INFO) << "[TransactionBase::data] addr|data=>"
				<< m_nameCallAddrAndData.first.hex() << "|"
				<< m_nameCallAddrAndData.second.size()
				;
		}

		return m_nameCallAddrAndData.second;
	}

	return m_data;
}

std::pair<Address, bytes> TransactionBase::addrAnddata() const
{
	if (m_isCalldByName)
	{
		if (!m_isGetAddrAndData)
		{
			m_nameCallAddrAndData = libabi::ContractAbiMgr::getInstance()->getAddrAndDataInfo(m_params.strContractName, m_params.strFunc, m_params.strVersion, m_params.jParams);
			m_isGetAddrAndData = true;

			LOG(INFO) << "[TransactionBase::addrAndData] addr|data=>"
				<< m_nameCallAddrAndData.first.hex() << "|"
				<< m_nameCallAddrAndData.second.size()
				;
		}

		return m_nameCallAddrAndData;
	}

	return std::pair<Address, bytes>();
}

Address const& TransactionBase::safeSender() const noexcept
{
	try
	{
		return sender();
	}
	catch (...)
	{
		return ZeroAddress;
	}
}

Address const& TransactionBase::sender() const
{
	if (!m_sender)
	{
		auto p = recover(m_vrs, sha3(WithoutSignature));

		if (!p)
			BOOST_THROW_EXCEPTION(InvalidSignature());
		m_sender = right160(dev::sha3(bytesConstRef(p.data(), sizeof(p))));

	}

	return m_sender;
}

void TransactionBase::sign(Secret const& _priv)
{
	auto sig = dev::sign(_priv, sha3(WithoutSignature));
	SignatureStruct sigStruct = *(SignatureStruct const*)&sig;
	if (sigStruct.isValid())
		m_vrs = sigStruct;
}

void TransactionBase::streamRLP(RLPStream& _s, IncludeSignature _sig, bool _forEip155hash) const
{
	if (m_type == NullTransaction)
		return;

	_s.appendList((_sig || _forEip155hash ? 3 : 0) + 7);
	_s << m_randomid << m_gasPrice << m_gas << m_blockLimit ; //这里加入新字段
	if (m_receiveAddress==Address())
	{
		_s << "";
	}
	else
	{
		_s << m_receiveAddress;
	}

	_s << m_value;
	_s << m_data;

	if (_sig)
	{
		int vOffset = m_chainId * 2 + 35;
		_s << (m_vrs.v + vOffset) << (u256)m_vrs.r << (u256)m_vrs.s;
	}
	else if (_forEip155hash)
		_s << m_chainId << 0 << 0;
}

void TransactionBase::streamRLP(std::stringstream& _s, IncludeSignature _sig, bool _forEip155hash) const {
	if (m_type == NullTransaction)
		return;

	_s << m_randomid << m_gasPrice << m_gas << m_blockLimit; //这里加入新字段
	if (m_receiveAddress == Address())
	{
		_s << "";
	}
	else
	{
		_s << m_receiveAddress;
	}

	_s << m_value;
	_s << toHex(m_data);

	if (_sig)
	{
		int vOffset = m_chainId * 2 + 35;
		_s << (m_vrs.v + vOffset) << (u256)m_vrs.r << (u256)m_vrs.s;
	}
	else if (_forEip155hash)
		_s << m_chainId << 0 << 0;
}

static const u256 c_secp256k1n("115792089237316195423570985008687907852837564279074904382605163141518161494337");

void TransactionBase::checkLowS() const
{
	if (m_vrs.s > c_secp256k1n / 2)
		BOOST_THROW_EXCEPTION(InvalidSignature());
}

void TransactionBase::checkChainId(int chainId) const
{
	if (m_chainId != chainId && m_chainId != -4)
		BOOST_THROW_EXCEPTION(InvalidSignature());
}

bigint TransactionBase::gasRequired(bool _contractCreation, bytesConstRef _data, EVMSchedule const& _es, u256 const& _gas)
{
	bigint ret = (_contractCreation ? _es.txCreateGas : _es.txGas) + _gas;
	for (auto i : _data)
		ret += i ? _es.txDataNonZeroGas : _es.txDataZeroGas;
	return ret;
}

h256 TransactionBase::sha3(IncludeSignature _sig) const
{
	if (_sig == WithSignature && m_hashWith)
		return m_hashWith;

	RLPStream s;
	//std::stringstream s;
	streamRLP(s, _sig, m_chainId > 0 && _sig == WithoutSignature);

	auto ret = dev::sha3(s.out());
	//auto ret = dev::sha3(s.str());
	if (_sig == WithSignature)
		m_hashWith = ret;
	return ret;
}
/*
h256 TransactionBase::sha32(IncludeSignature _sig) const
{
	RLPStream s;
	streamRLP(s, _sig, m_chainId > 0 && _sig == WithoutSignature);

	auto ret = dev::sha3(s.out());
	return ret;
}
*/
