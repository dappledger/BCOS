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
/** @file TransactionBase.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#pragma once

#include <libdevcore/RLP.h>
#include <libdevcore/SHA3.h>
#include <libdevcore/Guards.h>
#include <libethcore/Common.h>
#include <libweb3jsonrpc/JsonHelper.h>

namespace dev
{
namespace eth
{

struct EVMSchedule;

/// Named-boolean type to encode whether a signature be included in the serialisation process.
enum IncludeSignature
{
	WithoutSignature = 0,	///< Do not include a signature.
	WithSignature = 1,		///< Do include a signature.
};

enum class CheckTransaction
{
	None,
	Cheap,
	Everything
};

/// Encodes a transaction, ready to be exported to or freshly imported from RLP.
class TransactionBase
{
public:
	static u256  maxGas;	//默认交易最大gas
	/// Constructs a null transaction.
	TransactionBase() {}

	/// Constructs a transaction from a transaction skeleton & optional secret.
	TransactionBase(TransactionSkeleton const& _ts, Secret const& _s = Secret());

	/// Constructs a signed message-call transaction.
	TransactionBase(u256 const& _value, u256 const& _gasPrice, u256 const& _gas, Address const& _dest, bytes const& _data, u256 const& _randomid, Secret const& _secret): m_type(MessageCall), m_randomid(_randomid), m_value(_value), m_receiveAddress(_dest), m_gasPrice(_gasPrice), m_gas(_gas), m_data(_data) { sign(_secret); }

	/// Constructs a signed contract-creation transaction.
	TransactionBase(u256 const& _value, u256 const& _gasPrice, u256 const& _gas, bytes const& _data, u256 const& _randomid, Secret const& _secret): m_type(ContractCreation), m_randomid(_randomid), m_value(_value), m_gasPrice(_gasPrice), m_gas(_gas), m_data(_data) { sign(_secret); }

	/// Constructs an unsigned message-call transaction.
	TransactionBase(u256 const& _value, u256 const& _gasPrice, u256 const& _gas, Address const& _dest, bytes const& _data, u256 const& _randomid = 0): m_type(MessageCall), m_randomid(_randomid), m_value(_value), m_receiveAddress(_dest), m_gasPrice(_gasPrice), m_gas(_gas), m_data(_data) {}

	/// Constructs an unsigned contract-creation transaction.
	TransactionBase(u256 const& _value, u256 const& _gasPrice, u256 const& _gas, bytes const& _data, u256 const& _randomid = 0): m_type(ContractCreation), m_randomid(_randomid), m_value(_value), m_gasPrice(_gasPrice), m_gas(_gas), m_data(_data) {}

	/// Constructs a transaction from the given RLP.
	explicit TransactionBase(bytesConstRef _rlp, CheckTransaction _checkSig);

	/// Constructs a transaction from the given RLP.
	explicit TransactionBase(bytes const& _rlp, CheckTransaction _checkSig): TransactionBase(&_rlp, _checkSig) {}

	/// Checks equality of transactions.
	bool operator==(TransactionBase const& _c) const;//{ return m_type == _c.m_type && (m_type == ContractCreation || m_receiveAddress == _c.m_receiveAddress) && m_value == _c.m_value && m_data == _c.m_data; }
	/// Checks inequality of transactions.
	bool operator!=(TransactionBase const& _c) const;// { return !operator==(_c); }

	/// @returns sender of the transaction from the signature (and hash).
	Address const& sender() const;
	/// Like sender() but will never throw. @returns a null Address if the signature is invalid.
	Address const& safeSender() const noexcept;
	/// Force the sender to a particular value. This will result in an invalid transaction RLP.
	void forceSender(Address const& _a) { m_sender = _a; }

	/// @throws InvalidSValue if the signature has an invalid S value.
	void checkLowS() const;

	/// @throws InvalidSValue if the chain id is neither -4 nor equal to @a chainId
	/// Note that "-4" is the chain ID of the pre-155 rules, which should also be considered valid
	/// after EIP155
	void checkChainId(int chainId = -4) const;

	/// @returns true if transaction is non-null.
	explicit operator bool() const { return m_type != NullTransaction; }

	/// @returns true if transaction is contract-creation.
	bool isCreation() const { return m_type == ContractCreation; }

	/// @returns true if transaction is message-call.
	bool isMessageCall() const { return m_type == MessageCall; }

	/// Serialises this transaction to an RLPStream.
	void streamRLP(RLPStream& _s, IncludeSignature _sig = WithSignature, bool _forEip155hash = false) const;
	void streamRLP(std::stringstream& _s, IncludeSignature _sig = WithSignature, bool _forEip155hash = false) const;

	/// @returns the RLP serialisation of this transaction.
	bytes rlp(IncludeSignature _sig = WithSignature) const { RLPStream s; streamRLP(s, _sig); return s.out(); }

	/// @returns the SHA3 hash of the RLP serialisation of this transaction.
	h256 sha3(IncludeSignature _sig = WithSignature) const;
	//h256 sha32(IncludeSignature _sig = WithSignature) const;

	/// @returns the amount of ETH to be transferred by this (message-call) transaction, in Wei. Synonym for endowment().
	u256 value() const { return m_value; }
	/// @returns the amount of ETH to be endowed by this (contract-creation) transaction, in Wei. Synonym for value().
	u256 endowment() const { return m_value; }

	/// @returns the base fee and thus the implied exchange rate of ETH to GAS.
	u256 gasPrice() const { return m_gasPrice; }

	/// @returns the total gas to convert, paid for from sender's account. Any unused gas gets refunded once the contract is ended.
	u256 gas() const
	{
		return TransactionBase::maxGas;//注意
	}

	/// @returns the receiving address of the message-call transaction (undefined for contract-creation transactions).
	Address receiveAddress() const;// { return m_receiveAddress; }

	/// Synonym for receiveAddress().
	Address to() const;// { return m_receiveAddress; }

	/// Synonym for safeSender().
	Address from() const { return safeSender(); }

	/// @returns the data associated with this (message-call) transaction. Synonym for initCode().
	bytes const& data() const;// { return m_data; }
	/// @returns the initialisation code associated with this (contract-creation) transaction. Synonym for data().
	bytes const& initCode() const { return data(); }

	u256 randomid() const { return m_randomid; }

	/// Sets the nonce to the given value. Clears any signature.
	void setRandomid(u256 const& _n) { clearSignature(); m_randomid = _n; }

	u256 blockLimit()const { return m_blockLimit;}

	u256 importTime()const {return m_importtime;}
	void setImportTime(u256 _t) {m_importtime = _t;}
	/// Clears the signature.
	void clearSignature() { m_vrs = SignatureStruct(); }

	/// @returns the signature of the transaction. Encodes the sender.
	SignatureStruct const& signature() const { return m_vrs; }

	void sign(Secret const& _priv);			///< Sign the transaction.

	/// @returns true if the transaction contains enough gas for the basic payment.
	bigint gasRequired(EVMSchedule const& _es, u256 const& _gas = 0) const { return gasRequired(m_type == TransactionBase::ContractCreation, &m_data, _es, _gas); }

	/// Get the fee associated for a transaction with the given data.
	static bigint gasRequired(bool _contractCreation, bytesConstRef _data, EVMSchedule const& _es, u256 const& _gas = 0);

	int importType() const { return m_importType; }
	void setImportType(int t) { m_importType = t; }
protected:
	/// Type of transaction.
	enum Type
	{
		NullTransaction,				///< Null transaction.
		ContractCreation,				///< Transaction to create contracts - receiveAddress() is ignored.
		MessageCall						///< Transaction to invoke a message call - receiveAddress() is used.
	};

	Type m_type = NullTransaction;		///< Is this a contract-creation transaction or a message-call transaction?
	u256 m_randomid;						///< The transaction-count of the sender.
	u256 m_value;						///< The amount of ETH to be transferred by this transaction. Called 'endowment' for contract-creation transactions.
	Address m_receiveAddress;			///< The receiving address of the transaction.
	u256 m_gasPrice;					///< The base fee and thus the implied exchange rate of ETH to GAS.
	u256 m_gas;							///< The total gas to convert, paid for from sender's account. Any unused gas gets refunded once the contract is ended.
	u256 m_blockLimit;					//注意
	bytes m_data;						///< The data associated with the transaction, or the initialiser if it's a creation transaction.
	SignatureStruct m_vrs;				///< The signature of the transaction. Encodes the sender.
	int m_chainId = -4;					///< EIP155 value for calculating transaction hash https://github.com/ethereum/EIPs/issues/155

	//是否是以name方式调用
	bool m_isCalldByName{ false };
	//是否已经获取地址跟data数据
	mutable bool m_isGetAddrAndData{ false };
	//name call方式的参数
	NameCallParams m_params;
	mutable std::pair<Address, bytes> m_nameCallAddrAndData;

public:
	bool bNameCall() const { return m_isCalldByName; }
	NameCallParams   params() const { return m_params; }
	std::pair<Address, bytes> addrAnddata() const;

protected:
	u256 m_importtime = 0;				//入队时间 用来排序
	mutable h256 m_hashWith;			///< Cached hash of transaction with signature.
	mutable Address m_sender;			///< Cached sender, determined from signature.
	mutable bigint m_gasRequired = 0;	///< Memoised amount required for the transaction to run.

	int m_importType = 0; // default: 0 is from client,  1: from p2p
};

/// Nice name for vector of Transaction.
using TransactionBases = std::vector<TransactionBase>;

/// Simple human-readable stream-shift operator.
inline std::ostream& operator<<(std::ostream& _out, TransactionBase const& _t)
{
	_out << _t.sha3().abridged() << "{";
	if (_t.receiveAddress())
		_out << _t.receiveAddress().abridged();
	else
		_out << "[CREATE]";

	_out << "/" << _t.data().size() << "$" << _t.value() << "+" << _t.gas() << "@" << _t.gasPrice();
	_out << "<-" << _t.safeSender().abridged() << " #" << _t.randomid() << "}";
	return _out;
}

}
}
