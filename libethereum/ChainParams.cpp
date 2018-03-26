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
/** @file ChainParams.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2015
 */

#include "ChainParams.h"
#include <json_spirit/JsonSpiritHeaders.h>
#include <libdevcore/easylog.h>
#include <libdevcore/TrieDB.h>
#include <libethcore/SealEngine.h>
#include <libethcore/BlockHeader.h>
#include <libethcore/Precompiled.h>
#include <libethereum/NodeConnParamsManagerApi.h>
#include "GenesisInfo.h"
#include "State.h"
#include "Account.h"
using namespace std;
using namespace dev;
using namespace eth;
namespace js = json_spirit;


ChainParams::ChainParams()
{
	for (unsigned i = 1; i <= 4; ++i)
		genesisState[Address(i)] = Account(0, 1);
	// Setup default precompiled contracts as equal to genesis of Frontier.
	precompiled.insert(make_pair(Address(1), PrecompiledContract(3000, 0, PrecompiledRegistrar::executor("ecrecover"))));
	precompiled.insert(make_pair(Address(2), PrecompiledContract(60, 12, PrecompiledRegistrar::executor("sha256"))));
	precompiled.insert(make_pair(Address(3), PrecompiledContract(600, 120, PrecompiledRegistrar::executor("ripemd160"))));
	precompiled.insert(make_pair(Address(4), PrecompiledContract(15, 3, PrecompiledRegistrar::executor("identity"))));
}

ChainParams::ChainParams(string const& _json, h256 const& _stateRoot)
{
	*this = loadConfig(_json, _stateRoot);
	ChainParams();
}

ChainParams ChainParams::loadGodMiner(std::string const& _json) const
{
	ChainParams cp(*this);
	js::mValue val;
	json_spirit::read_string(_json, val);
	js::mObject obj = val.get_obj();

	cp.godMinerStart = obj.count("godMinerStart") ? u256(fromBigEndian<u256>(fromHex(obj["godMinerStart"].get_str()))) : 0;
	cp.godMinerEnd = obj.count("godMinerEnd") ? u256(fromBigEndian<u256>(fromHex(obj["godMinerEnd"].get_str()))) : 0;
	if ( obj.count("miners") )
	{
		for (auto node : obj["miners"].get_array())
		{
			NodeConnParams   nodeConnParam;
			nodeConnParam._sNodeId = node.get_obj()["Nodeid"].get_str();
			nodeConnParam._sAgencyInfo = node.get_obj()["Agencyinfo"].get_str();
			nodeConnParam._sIP = node.get_obj()["Peerip"].get_str();
			nodeConnParam._iPort = node.get_obj()["Port"].get_int();
			nodeConnParam._iIdentityType = node.get_obj()["Identitytype"].get_int();
			nodeConnParam._sAgencyDesc = node.get_obj()["Nodedesc"].get_str();
			nodeConnParam._iIdx = u256(node.get_obj()["Idx"].get_int());

			cp.godMinerList[nodeConnParam._sNodeId] = nodeConnParam;
		}
	}

	return cp;
}
ChainParams ChainParams::loadConfig(string const& _json, h256 const& ) const
{
	ChainParams cp(*this);
	js::mValue val;
	json_spirit::read_string(_json, val);
	js::mObject obj = val.get_obj();

	cp.sealEngineName = obj["sealEngine"].get_str();
	//系统代理合约地址
	cp.sysytemProxyAddress = obj.count("systemproxyaddress") ? h160(obj["systemproxyaddress"].get_str()) : h160();
	cp.listenIp = obj.count("listenip") ? obj["listenip"].get_str() : "0.0.0.0";
	cp.cryptoMod = obj.count("cryptomod") ? std::stoi(obj["cryptomod"].get_str()) : 0;//获取加密模式
	cp.cryptoprivatekeyMod = obj.count("cryptoprivatekeymod") ? std::stoi(obj["cryptoprivatekeymod"].get_str()):0;
	cp.ssl = obj.count("ssl") ? std::stoi(obj["ssl"].get_str()):0;
	cp.rpcPort = obj.count("rpcport") ? std::stoi(obj["rpcport"].get_str()) : 6789;
	cp.rpcSSLPort = obj.count("rpcsslport") ? std::stoi(obj["rpcsslport"].get_str()) : 6790;
	cp.channelPort = obj.count("channelPort") ? std::stoi(obj["channelPort"].get_str()) : 0;
	cp.p2pPort = obj.count("p2pport") ? std::stoi(obj["p2pport"].get_str()) : 16789;
	cp.wallet = obj.count("wallet") ? obj["wallet"].get_str() : "/tmp/ethereum/keys.info";
	cp.keystoreDir = obj.count("keystoredir") ? obj["keystoredir"].get_str() : "/tmp/ethereum/keystore/";
	cp.dataDir = obj.count("datadir") ? obj["datadir"].get_str() : "/tmp/ethereum/data/";
	cp.logFileConf = obj.count("logconf") ? obj["logconf"].get_str() : "/tmp/ethereum/data/";
	cp.rateLimitConfig = obj.count("limitconf") ? obj["limitconf"].get_str() : "";
	cp.statsInterval = obj.count("statsInterval") ? std::stoi(obj["statsInterval"].get_str()) : 0;
	
	cp.vmKind = obj.count("vm") ? obj["vm"].get_str() : "interpreter";
	cp.networkId = obj.count("networkid") ? std::stoi(obj["networkid"].get_str()) : (unsigned) - 1;
	cp.logVerbosity = obj.count("logverbosity") ? std::stoi(obj["logverbosity"].get_str()) : 4;
	cp.evmEventLog = obj.count("eventlog") ? ( (obj["eventlog"].get_str() == "ON") ? true : false) : false;
	cp.evmCoverLog = obj.count("coverlog") ? ( (obj["coverlog"].get_str() == "ON") ? true : false) : false;
	//dfs related configure items
	cp.nodeId = obj.count("dfsNode") ? obj["dfsNode"].get_str() : "";
	cp.groupId = obj.count("dfsGroup") ? obj["dfsGroup"].get_str() : "";
	cp.storagePath = obj.count("dfsStorage") ? obj["dfsStorage"].get_str() : "";
	cp.statLog = obj.count("statlog") ? ( (obj["statlog"].get_str() == "ON") ? true : false) : false;
	cp.broadcastToNormalNode = obj.count("broadcastToNormalNode") ? ( (obj["broadcastToNormalNode"].get_str() == "ON") ? true : false) : false;
	// params
	js::mObject params = obj["params"].get_obj();
	cp.accountStartNonce = u256(fromBigEndian<u256>(fromHex(params["accountStartNonce"].get_str())));
	cp.maximumExtraDataSize = u256(fromBigEndian<u256>(fromHex(params["maximumExtraDataSize"].get_str())));
	cp.tieBreakingGas = params.count("tieBreakingGas") ? params["tieBreakingGas"].get_bool() : true;
	cp.blockReward = u256(fromBigEndian<u256>(fromHex(params["blockReward"].get_str())));
	for (auto i : params)
		if (i.first != "accountStartNonce" && i.first != "maximumExtraDataSize" && i.first != "blockReward" && i.first != "tieBreakingGas")
			cp.otherParams[i.first] = i.second.get_str();
	/*
	// genesis
	string genesisStr = json_spirit::write_string(obj["genesis"], false);
	cp = cp.loadGenesis(genesisStr, _stateRoot);
	// genesis state
	string genesisStateStr = json_spirit::write_string(obj["accounts"], false);
	cp = cp.loadGenesisState(genesisStateStr, _stateRoot);
	*/
	return cp;
}

ChainParams ChainParams::loadGenesisState(string const& _json, h256 const& _stateRoot) const
{
	ChainParams cp(*this);
	cp.genesisState = jsonToAccountMap(_json, cp.accountStartNonce, nullptr, &cp.precompiled);//创世块合约的account数据这里load
	cp.stateRoot = _stateRoot ? _stateRoot : cp.calculateStateRoot(true);
	return cp;
}

ChainParams ChainParams::loadGenesis(string const& _json, h256 const& _stateRoot) const
{
	ChainParams cp(*this);

	js::mValue val;
	json_spirit::read_string(_json, val);
	js::mObject genesis = val.get_obj();

	cp.parentHash = h256(genesis["parentHash"].get_str());
	cp.author = h160();
	cp.difficulty = genesis.count("difficulty") ? u256(fromBigEndian<u256>(fromHex(genesis["difficulty"].get_str()))) : 1;
	cp.gasLimit = u256(fromBigEndian<u256>(fromHex(genesis["gasLimit"].get_str())));
	cp.gasUsed = 0;//genesis.count("gasUsed") ? u256(fromBigEndian<u256>(fromHex(genesis["gasUsed"].get_str()))) : 0;
	cp.timestamp = u256(fromBigEndian<u256>(fromHex(genesis["timestamp"].get_str())));
	cp.extraData = bytes(fromHex(genesis["extraData"].get_str()));

	//从创世块读取上帝帐号，按理说下面的genesisBlock 也要加上这个字段
	cp.god = genesis.count("god") ? h160(genesis["god"].get_str()) : h160();

	cp.author = cp.god; //在计算创世块的genesisBlock时候有用

	// magic code for handling ethash stuff:
	if ((genesis.count("mixhash") || genesis.count("mixHash")) && genesis.count("nonce"))
	{
		h256 mixHash(genesis[genesis.count("mixhash") ? "mixhash" : "mixHash"].get_str());
		h64 nonce(genesis["nonce"].get_str());
		cp.sealFields = 2;
		cp.sealRLP = rlp(mixHash) + rlp(nonce);
	}

	cp.genesisState = jsonToAccountMap(_json, cp.accountStartNonce, nullptr, &cp.precompiled);//创世块合约的account数据这里load

	//读取initNodes
	if (genesis.count("initMinerNodes"))
	{
		for (auto initNode : genesis["initMinerNodes"].get_array())
		{
			cp._vInitIdentityNodes.push_back(initNode.get_str());
			LOG(INFO) << "initNodes size : " << cp._vInitIdentityNodes.size() << "| node :" << initNode.get_str() << "\n";
		}
	}

	cp.stateRoot = _stateRoot ? _stateRoot : cp.calculateStateRoot();
	cout << "loadGenesis: stateRoot=" << cp.stateRoot;
	return cp;
}

SealEngineFace* ChainParams::createSealEngine()
{
	SealEngineFace* ret = SealEngineRegistrar::create(sealEngineName);
	assert(ret && "Seal engine not found");
	if (!ret)
		return nullptr;
	ret->setChainParams(*this);
	if (sealRLP.empty())
	{
		sealFields = ret->sealFields();
		sealRLP = ret->sealRLP();
	}
	return ret;
}

void ChainParams::populateFromGenesis(bytes const& _genesisRLP, AccountMap const& _state)
{
	BlockHeader bi(_genesisRLP, RLP(&_genesisRLP)[0].isList() ? BlockData : HeaderData);
	parentHash = bi.parentHash();
	author = bi.author();
	difficulty = bi.difficulty();
	gasLimit = bi.gasLimit();
	gasUsed = bi.gasUsed();
	timestamp = bi.timestamp();
	extraData = bi.extraData();
	genesisState = _state;
	RLP r(_genesisRLP);
	sealFields = r[0].itemCount() - BlockHeader::BasicFields;
	sealRLP.clear();
	for (unsigned i = BlockHeader::BasicFields; i < r[0].itemCount(); ++i)
		sealRLP += r[0][i].data();

	calculateStateRoot(true);

	auto b = genesisBlock();
	if (b != _genesisRLP)
	{
		LOG(DEBUG) << "Block passed:" << bi.hash() << bi.hash(WithoutSeal);
		LOG(DEBUG) << "Genesis now:" << BlockHeader::headerHashFromBlock(b);
		LOG(DEBUG) << RLP(b);
		LOG(DEBUG) << RLP(_genesisRLP);
		throw 0;
	}
}

h256 ChainParams::calculateStateRoot(bool _force) const
{
	MemoryDB db;
	SecureTrieDB<Address, MemoryDB> state(&db);
	state.init();
	if (!stateRoot || _force)
	{
		// TODO: use hash256
		//stateRoot = hash256(toBytesMap(gs));
		dev::eth::commit(genesisState, state);
		stateRoot = state.root();
	}
	return stateRoot;
}

bytes ChainParams::genesisBlock() const
{
	RLPStream block(3);

	calculateStateRoot();

	std::vector<h512> node_list; // 此处去获取创世块的公钥列表
	RLPStream node_rs;
	for (size_t i = 0; i < node_list.size(); ++i) {
		node_rs << node_list[i];
	}

	block.appendList(BlockHeader::BasicFields + sealFields)
	        << parentHash
	        << EmptyListSHA3	// sha3(uncles)
	        << author
	        << stateRoot
	        << EmptyTrie		// transactions
	        << EmptyTrie		// receipts
	        << LogBloom()
	        << difficulty
	        << 0				// number
	        << gasLimit
	        << gasUsed			// gasUsed
	        << timestamp
	        << extraData
	        << 0
	        << node_rs.out();
	block.appendRaw(sealRLP, sealFields);
	block.appendRaw(RLPEmptyList);
	block.appendRaw(RLPEmptyList);
	return block.out();
}
