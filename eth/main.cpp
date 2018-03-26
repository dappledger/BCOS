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
/**
 * @file main.cpp
 * @author Gav Wood <i@gavwood.com>
 * @author Tasha Carl <tasha@carl.pro> - I here by place all my contributions in this file under MIT licence, as specified by http://opensource.org/licenses/MIT.
 * @date 2014
 * Ethereum client.

 */

#include <thread>
#include <chrono>
#include <fstream>
#include <iostream>
#include <clocale>
#include <signal.h>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/trim_all.hpp>
#include <boost/filesystem.hpp>

#include <libdevcore/FileSystem.h>
#include <libdevcore/easylog.h>

//#include <libethashseal/EthashAux.h>
#include <libevm/VM.h>
#include <libevm/VMFactory.h>
#include <libethcore/KeyManager.h>
#include <libethcore/ICAP.h>
#include <libethereum/All.h>
#include <libethereum/BlockChainSync.h>
#include <libethereum/NodeConnParamsManagerApi.h>
//#include <libethashseal/EthashClient.h>
#include <libpbftseal/PBFT.h>
#include <libsinglepoint/SinglePointClient.h>
#include <libsinglepoint/SinglePoint.h>
#include <libraftseal/Raft.h>
//#include <libethashseal/GenesisInfo.h>
#include <libwebthree/WebThree.h>

#include <libweb3jsonrpc/AccountHolder.h>
#include <libweb3jsonrpc/Eth.h>
#include <libweb3jsonrpc/SafeHttpServer.h>
#include <jsonrpccpp/client/connectors/httpclient.h>
#include <libweb3jsonrpc/ModularServer.h>
#include <libweb3jsonrpc/IpcServer.h>
#include <libweb3jsonrpc/LevelDB.h>
#include <libweb3jsonrpc/Whisper.h>
#include <libweb3jsonrpc/Net.h>
#include <libweb3jsonrpc/Web3.h>
#include <libweb3jsonrpc/SessionManager.h>
#include <libweb3jsonrpc/AdminNet.h>
#include <libweb3jsonrpc/AdminEth.h>
#include <libweb3jsonrpc/AdminUtils.h>
#include <libweb3jsonrpc/Personal.h>
#include <libweb3jsonrpc/Debug.h>
#include <libweb3jsonrpc/Test.h>
#include "Farm.h"

//#include <ethminer/MinerAux.h>
#include "AccountManager.h"
#include <libdevcore/easylog.h>

#include <libdiskencryption/CryptoParam.h>//读取加密方式配置文件
#include <libdiskencryption/GenKey.h>//生成nodekey及datakey文件
#include <libweb3jsonrpc/ChannelRPCServer.h>
#include <libweb3jsonrpc/RPCallback.h>

INITIALIZE_EASYLOGGINGPP

using namespace std;
using namespace dev;
using namespace dev::p2p;
using namespace dev::eth;
using namespace boost::algorithm;

static std::atomic<bool> g_silence = {false};

#include "genesisInfoForWe.h"
#include "GenesisInfo.h"

void help()
{
	cout
	        << "Usage eth [OPTIONS]" << "\n"
	        << "Options:" << "\n" << "\n"
	        << "Wallet usage:" << "\n";
	AccountManager::streamAccountHelp(cout);
	AccountManager::streamWalletHelp(cout);
	cout
	        << "\n";
	cout
	        << "Client mode (default):" << "\n"
	        << "    --mainnet  Use the main network protocol." << "\n"
	        << "    --ropsten  Use the Ropsten testnet." << "\n"
	        << "    --private <name>  Use a private chain." << "\n"
	        << "    --test  Testing mode: Disable PoW and provide test rpc interface." << "\n"
	        << "    --config <file>  Configure specialised blockchain using given JSON information." << "\n"
	        << "    --oppose-dao-fork  Ignore DAO hard fork (default is to participate)." << "\n"
	        << "\n"
	        << "    -o,--mode <full/peer>  Start a full node or a peer node (default: full)." << "\n"
	        << "\n"
	        << "    -j,--json-rpc  Enable JSON-RPC server (default: off)." << "\n"
	        << "    --ipc  Enable IPC server (default: on)." << "\n"
	        << "    --ipcpath Set .ipc socket path (default: data directory)" << "\n"
	        << "    --admin-via-http  Expose admin interface via http - UNSAFE! (default: off)." << "\n"
	        << "    --no-ipc  Disable IPC server." << "\n"
	        << "    --json-rpc-port <n>  Specify JSON-RPC server port (implies '-j', default: " << SensibleHttpPort << ")." << "\n"
	        << "    --rpccorsdomain <domain>  Domain on which to send Access-Control-Allow-Origin header." << "\n"
	        << "    --admin <password>  Specify admin session key for JSON-RPC (default: auto-generated and printed at start-up)." << "\n"
	        << "    -K,--kill  Kill the blockchain first." << "\n"
	        << "    -R,--rebuild  Rebuild the blockchain from the existing database." << "\n"
	        << "    --rescue  Attempt to rescue a corrupt database." << "\n"
	        << "\n"
	        << "    --import-presale <file>  Import a pre-sale key; you'll need to specify the password to this key." << "\n"
	        << "    -s,--import-secret <secret>  Import a secret key into the key store." << "\n"
	        << "    --master <password>  Give the master password for the key store. Use --master \"\" to show a prompt." << "\n"
	        << "    --password <password>  Give a password for a private key." << "\n"
	        << "\n"
	        << "Client transacting:" << "\n"
	        /*<< "    -B,--block-fees <n>  Set the block fee profit in the reference unit, e.g. ¢ (default: 15)." << "\n"
	        << "    -e,--ether-price <n>  Set the ether price in the reference unit, e.g. ¢ (default: 30.679)." << "\n"
	        << "    -P,--priority <0 - 100>  Set the default priority percentage (%) of a transaction (default: 50)." << "\n"*/
	        << "    --ask <wei>  Set the minimum ask gas price under which no transaction will be mined (default " << toString(DefaultGasPrice) << " )." << "\n"
	        << "    --bid <wei>  Set the bid gas price to pay for transactions (default " << toString(DefaultGasPrice) << " )." << "\n"
	        << "    --unsafe-transactions  Allow all transactions to proceed without verification. EXTREMELY UNSAFE."
	        << "\n"
	        << "Client mining:" << "\n"
	        << "    -a,--address <addr>  Set the author (mining payout) address to given address (default: auto)." << "\n"
	        << "    -m,--mining <on/off/number>  Enable mining, optionally for a specified number of blocks (default: off)." << "\n"
	        << "    -f,--force-mining  Mine even when there are no transactions to mine (default: off)." << "\n"
	        << "    -C,--cpu  When mining, use the CPU." << "\n"
	        << "    -t, --mining-threads <n>  Limit number of CPU/GPU miners to n (default: use everything available on selected platform)." << "\n"
	        << "\n"
	        << "Client networking:" << "\n"
	        << "    --client-name <name>  Add a name to your client's version string (default: blank)." << "\n"
	        << "    --bootstrap  Connect to the default Ethereum peer servers (default unless --no-discovery used)." << "\n"
	        << "    --no-bootstrap  Do not connect to the default Ethereum peer servers (default only when --no-discovery is used)." << "\n"
	        << "    -x,--peers <number>  Attempt to connect to a given number of peers (default: 11)." << "\n"
	        << "    --peer-stretch <number>  Give the accepted connection multiplier (default: 7)." << "\n"

	        << "    --public-ip <ip>  Force advertised public IP to the given IP (default: auto)." << "\n"
	        << "    --listen-ip <ip>(:<port>)  Listen on the given IP for incoming connections (default: 0.0.0.0)." << "\n"
	        << "    --listen <port>  Listen on the given port for incoming connections (default: 30303)." << "\n"
	        << "    -r,--remote <host>(:<port>)  Connect to the given remote host (default: none)." << "\n"
	        << "    --port <port>  Connect to the given remote port (default: 30303)." << "\n"
	        << "    --network-id <n>  Only connect to other hosts with this network id." << "\n"
	        << "    --upnp <on/off>  Use UPnP for NAT (default: on)." << "\n"

	        << "    --peerset <list>  Space delimited list of peers; element format: type:publickey@ipAddress[:port]." << "\n"
	        << "        Types:" << "\n"
	        << "        default		Attempt connection when no other peers are available and pinning is disabled." << "\n"
	        << "        required		Keep connected at all times." << "\n"
// TODO:
//		<< "	--trust-peers <filename>  Space delimited list of publickeys." << "\n"

	        << "    --no-discovery  Disable node discovery, implies --no-bootstrap." << "\n"
	        << "    --pin  Only accept or connect to trusted peers." << "\n"
	        << "    --hermit  Equivalent to --no-discovery --pin." << "\n"
	        << "    --sociable  Force discovery and no pinning." << "\n"
	        << "\n";
	//MinerCLI::streamHelp(cout);
	cout
	        << "Import/export modes:" << "\n"
	        << "    --from <n>  Export only from block n; n may be a decimal, a '0x' prefixed hash, or 'latest'." << "\n"
	        << "    --to <n>  Export only to block n (inclusive); n may be a decimal, a '0x' prefixed hash, or 'latest'." << "\n"
	        << "    --only <n>  Equivalent to --export-from n --export-to n." << "\n"
	        << "    --dont-check  Prevent checking some block aspects. Faster importing, but to apply only when the data is known to be valid." << "\n"
	        << "\n"
	        << "General Options:" << "\n"
	        << "    -d,--db-path,--datadir <path>  Load database from path (default: " << getDataDir() << ")." << "\n"
#if ETH_EVMJIT
	        << "    --vm <vm-kind>  Select VM; options are: interpreter, jit or smart (default: interpreter)." << "\n"
#endif // ETH_EVMJIT
	        << "    -v,--verbosity <0 - 9>  Set the log verbosity from 0 to 9 (default: 8)." << "\n"
	        << "    -V,--version  Show the version and exit." << "\n"
	        << "    -h,--help  Show this help message and exit." << "\n"
	        << "\n"
	        << "Experimental / Proof of Concept:" << "\n"
	        << "    --shh  Enable Whisper." << "\n"
	        << "    --singlepoint  Enable singlepoint." << "\n"
	        << "\n"
	        ;
	exit(0);
}

string ethCredits(bool _interactive = false)
{
	std::ostringstream cout;
	cout
		<< "FISCO-BCOS " << dev::Version << "\n"
		<< dev::Copyright << "\n"
		<< "  See the README for contributors and credits."
		<< "\n";

	if (_interactive)
		cout << "Type 'exit' to quit"
			 << "\n"
			 << "\n";
	return cout.str();
}

void version()
{
	cout << "FISCO-BCOS version " << dev::Version << "\n";
	cout << "FISCO-BCOS network protocol version: " << dev::eth::c_protocolVersion << "\n";
	cout << "Client database version: " << dev::eth::c_databaseVersion << "\n";
	cout << "Build: " << DEV_QUOTED(ETH_BUILD_PLATFORM) << "/" << DEV_QUOTED(ETH_BUILD_TYPE) << "\n";
	exit(0);
}

void generateNetworkRlp(string sFilePath)
{
	KeyPair kp = KeyPair::create();
	RLPStream netData(3);
	netData << dev::p2p::c_protocolVersion << kp.secret().ref();
	int count = 0;
	netData.appendList(count);

	writeFile(sFilePath, netData.out());
	writeFile(sFilePath + ".pub", kp.pub().hex());

	cout << "eth generate network.rlp. " << "\n";
	cout << "eth public id is :[" << kp.pub().hex() << "]" << "\n";
	cout << "write into file [" << sFilePath << "]" << "\n";
	exit(0);
}

/*
The equivalent of setlocale(LC_ALL, “C”) is called before any user code is run.
If the user has an invalid environment setting then it is possible for the call
to set locale to fail, so there are only two possible actions, the first is to
throw a runtime exception and cause the program to quit (default behaviour),
or the second is to modify the environment to something sensible (least
surprising behaviour).

The follow code produces the least surprising behaviour. It will use the user
specified default locale if it is valid, and if not then it will modify the
environment the process is running in to use a sensible default. This also means
that users do not need to install language packs for their OS.
*/
void setDefaultOrCLocale()
{
#if __unix__
	if (!std::setlocale(LC_ALL, ""))
	{
		setenv("LC_ALL", "C", 1);
	}
#endif
}

void importPresale(KeyManager& _km, string const& _file, function<string()> _pass)
{
	KeyPair k = _km.presaleSecret(contentsString(_file), [&](bool) { return _pass(); });
	_km.import(k.secret(), "Presale wallet" + _file + " (insecure)");
}

Address c_config = Address("ccdeac59d35627b7de09332e819d5159e7bb7250");
string pretty(h160 _a, dev::eth::State const& _st)
{
	string ns;
	h256 n;
	if (h160 nameReg = (u160)_st.storage(c_config, 0))
		n = _st.storage(nameReg, (u160)(_a));
	if (n)
	{
		std::string s((char const*)n.data(), 32);
		if (s.find_first_of('\0') != string::npos)
			s.resize(s.find_first_of('\0'));
		ns = " " + s;
	}
	return ns;
}

inline bool isPrime(unsigned _number)
{
	if (((!(_number & 1)) && _number != 2 ) || (_number < 2) || (_number % 3 == 0 && _number != 3))
		return false;
	for (unsigned k = 1; 36 * k * k - 12 * k < _number; ++k)
		if ((_number % (6 * k + 1) == 0) || (_number % (6 * k - 1) == 0))
			return false;
	return true;
}

enum class NodeMode
{
	PeerServer,
	Full
};

enum class OperationMode
{
	Node,
	Import,
	Export,
	ExportGenesis
};

enum class Format
{
	Binary,
	Hex,
	Human
};

void stopSealingAfterXBlocks(eth::Client* _c, unsigned _start, unsigned& io_mining)
{
	try
	{
		if (io_mining != ~(unsigned)0 && io_mining && _c->isMining() && _c->blockChain().details().number - _start == io_mining)
		{
			_c->stopSealing();

			io_mining = ~(unsigned)0;
		}
	}
	catch (InvalidSealEngine&)
	{
	}

	this_thread::sleep_for(chrono::milliseconds(100));
}

class ExitHandler: public SystemManager
{
public:
	void exit() { exitHandler(0); }
	static void exitHandler(int) { s_shouldExit = true; }
	bool shouldExit() const { return s_shouldExit; }

private:
	static bool s_shouldExit;
};

bool ExitHandler::s_shouldExit = false;

static map<string, unsigned int> s_mlogIndex;

void rolloutHandler(const char* filename, std::size_t )
{
	std::stringstream stream;

	map<string, unsigned int>::iterator iter = s_mlogIndex.find(filename);
	if (iter != s_mlogIndex.end())
	{
		stream << filename << "." << iter->second++;
		s_mlogIndex[filename] = iter->second++;
	}
	else
	{
		stream << filename << "." << 0;
		s_mlogIndex[filename] = 0;
	}
	boost::filesystem::rename(filename, stream.str().c_str());
}

void logRotateByTime()
{	
	const std::chrono::seconds wakeUpDelta = std::chrono::seconds(20);
	static auto nextWakeUp = std::chrono::system_clock::now();
	
	if(std::chrono::system_clock::now() > nextWakeUp)
	{
		nextWakeUp = std::chrono::system_clock::now() + wakeUpDelta;
	}
	else
	{
		return;
	}

	auto L = el::Loggers::getLogger("default");
	if(L == nullptr)
	{
		LOG(ERROR)<<"Oops, it is not called default!";
	}
	else
	{
		L->reconfigure();	
	}

	L = el::Loggers::getLogger("fileLogger");
	if(L == nullptr)
	{
		LOG(ERROR)<<"Oops, it is not called fileLogger!";
	}
	else
	{
		L->reconfigure();	
	} 

	L = el::Loggers::getLogger("statLogger");
	if(L == nullptr)
	{
		LOG(ERROR)<<"Oops, it is not called fileLogger!";
	}
	else
	{
		L->reconfigure();	
	} 
}
//日志配置文件放到log目录
void initEasylogging(const ChainParams& chainParams)
{
	string logconf = chainParams.logFileConf;

	el::Loggers::addFlag(el::LoggingFlag::MultiLoggerSupport); // Enables support for multiple loggers
	el::Loggers::addFlag(el::LoggingFlag::StrictLogFileSizeCheck);
	el::Loggers::setVerboseLevel(chainParams.logVerbosity);
	if (el::base::utils::File::pathExists(logconf.c_str(), true))
	{
		el::Logger* fileLogger = el::Loggers::getLogger("fileLogger"); // Register new logger
		el::Configurations conf(logconf.c_str());
		el::Configurations allConf;
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::Enabled));
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::ToFile));
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::ToStandardOutput));
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::Format));
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::Filename));
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::SubsecondPrecision));
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::MillisecondsWidth));
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::PerformanceTracking));
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::MaxLogFileSize));
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::LogFlushThreshold));
		allConf.set(conf.get(el::Level::Trace, el::ConfigurationType::Enabled));
		allConf.set(conf.get(el::Level::Debug, el::ConfigurationType::Enabled));
		allConf.set(conf.get(el::Level::Fatal, el::ConfigurationType::Enabled));
		allConf.set(conf.get(el::Level::Error, el::ConfigurationType::Enabled));
		allConf.set(conf.get(el::Level::Warning, el::ConfigurationType::Enabled));
		allConf.set(conf.get(el::Level::Verbose, el::ConfigurationType::Enabled));
		allConf.set(conf.get(el::Level::Info, el::ConfigurationType::Enabled));
		el::Loggers::reconfigureLogger("default", allConf);
		el::Loggers::reconfigureLogger(fileLogger, conf);

		// stat log，依托于 default 的配置
		el::Logger* statLogger = el::Loggers::getLogger("statLogger");
		el::Configurations statConf = allConf;
		el::Configuration* fileConf = conf.get(el::Level::Global, el::ConfigurationType::Filename);
		
		string stat_prefix = "stat_";
		string stat_path = fileConf->value();
		if (!stat_path.empty()) 
		{
			size_t pos = stat_path.find_last_of("/"); // not support for windows
			if ((int)pos == -1) // log file just has name not path
				stat_path = stat_prefix + stat_path;
			else
				stat_path.insert(pos + 1, stat_prefix);
		} 
		else 
		{
			stat_path = stat_prefix + "log_%datetime{%Y%M%d%H}.log";
		}
		statConf.set(el::Level::Global, el::ConfigurationType::Filename, stat_path);
		if (!chainParams.statLog) 
		{
			statConf.set(el::Level::Global, el::ConfigurationType::Enabled, "false");
			statConf.set(el::Level::Global, el::ConfigurationType::ToFile, "false");
			statConf.set(el::Level::Global, el::ConfigurationType::ToStandardOutput, "false");
		}	

		el::Loggers::reconfigureLogger("statLogger", statConf);
	}
	el::Helpers::installPreRollOutCallback(rolloutHandler);
}

bool isTrue(std::string const& _m)
{
	return _m == "on" || _m == "yes" || _m == "true" || _m == "1";
}

bool isFalse(std::string const& _m)
{
	return _m == "off" || _m == "no" || _m == "false" || _m == "0";
}


int main(int argc, char** argv)
{
	setDefaultOrCLocale();
	// Init defaults
	Defaults::get();
	//Ethash::init();
	NoProof::init();
	PBFT::init();
	Raft::init();
	SinglePoint::init();


	/// Operating mode.
	OperationMode mode = OperationMode::Node;
//	unsigned prime = 0;
//	bool yesIReallyKnowWhatImDoing = false;
	strings scripts;

	/// File name for import/export.
	string filename;
	bool safeImport = false;

	/// Hashes/numbers for export range.
	string exportFrom = "1";
	string exportTo = "latest";
	Format exportFormat = Format::Binary;

	/// General params for Node operation
	NodeMode nodeMode = NodeMode::Full;
	/// bool interactive = false;

	int jsonRPCURL = -1;
	int jsonRPCSSLURL = -1;
	bool adminViaHttp = true;
	bool ipc = true;
	std::string rpcCorsDomain = "";

	string jsonAdmin;
	ChainParams chainParams(genesisInfo(eth::Network::MainNetwork), genesisStateRoot(eth::Network::MainNetwork));
	u256 gasFloor = Invalid256;
	string privateChain;

	bool upnp = true;
	WithExisting withExisting = WithExisting::Trust;

	/// Networking params.
	string clientName;
	string listenIP;
	unsigned short listenPort = 16789;
	string publicIP;
	string remoteHost;
	unsigned short remotePort = 16789;

	unsigned peers = 128;
	unsigned peerStretch = 7;
	std::map<NodeID, pair<NodeIPEndpoint, bool>> preferredNodes;
	bool bootstrap = true;
	bool disableDiscovery = true;
	bool pinning = false;
	bool enableDiscovery = false;
	bool noPinning = false;
	static const unsigned NoNetworkID = (unsigned) - 1;
	unsigned networkID = NoNetworkID;

	/// Mining params
	unsigned mining = ~(unsigned)0;
	Address author;
	LOG(TRACE) << " Main:: author: " << author;
	strings presaleImports;
	bytes extraData;

	u256 askPrice = DefaultGasPrice;
	u256 bidPrice = DefaultGasPrice;
	bool alwaysConfirm = true;

	/// Wallet password stuff
	string masterPassword;
	bool masterSet = false;

	/// Whisper
	bool useWhisper = false;
	bool testingMode = false;

	bool singlepoint = false;

	strings passwordsToNote;
	Secrets toImport;


	if (argc > 1 && (string(argv[1]) == "wallet" || string(argv[1]) == "account"))
	{
		if ( (string(argv[1]) == "account") && ( string(argv[2]) == "new") && (4 < argc) ) //eth account new dir pass
		{
			SecretStore::defaultpath = string(argv[3]);
			AccountManager accountm;
			return !accountm.execute(argc, argv);
		}
		else
		{
			string ret;
			cout << "请输入keystore 保存路径：";
			getline(cin, ret);
			SecretStore::defaultpath = ret;
			cout << "keystoredir:" << SecretStore::defaultPath() << "\n";
			AccountManager accountm;
			return !accountm.execute(argc, argv);
		}

	}


	bool listenSet = false;
	string configJSON;
	string genesisJSON;
	string godminerJSON;

	int blockNumber = 0;
	std::string contracts;
	//dfs related parameters
	string strNodeId;
	string strGroupId;
	string strStoragePath;
	Address fileContractAddr;
	Address fileServerContractAddr;
	for (int i = 1; i < argc; ++i)
	{
		string arg = argv[i];
		/*if (m.interpretOption(i, argc, argv))
		{
		}
		else */if (arg == "--listen-ip" && i + 1 < argc)
		{
			listenIP = argv[++i];
			listenSet = true;
		}
		else if ((arg == "--listen" || arg == "--listen-port") && i + 1 < argc)
		{
			listenPort = (short)atoi(argv[++i]);
			listenSet = true;
		}
		else if ((arg == "--public-ip" || arg == "--public") && i + 1 < argc)
		{
			publicIP = argv[++i];
		}
		else if ((arg == "-r" || arg == "--remote") && i + 1 < argc)
		{
			string host = argv[++i];
			string::size_type found = host.find_first_of(':');
			if (found != std::string::npos)
			{
				remoteHost = host.substr(0, found);
				remotePort = (short)atoi(host.substr(found + 1, host.length()).c_str());
			}
			else
				remoteHost = host;
		}
		else if (arg == "--port" && i + 1 < argc)
		{
			remotePort = (short)atoi(argv[++i]);
		}
		else if (arg == "--password" && i + 1 < argc)
			passwordsToNote.push_back(argv[++i]);
		else if (arg == "--master" && i + 1 < argc)
		{
			masterPassword = argv[++i];
			masterSet = true;
		}
		//创世块合约列表
		else if ((arg == "--contracts") && i + 1 < argc) {
			contracts = argv[++i];
		}
		//创世块blocknumber
		else if ((arg == "--blocknumber") && i + 1 < argc) {
			blockNumber = atoi(argv[++i]);
		}
		//创世块路径
		else if ((arg == "--export-genesis") && i + 1 < argc) {
			mode = OperationMode::ExportGenesis;
			filename = argv[++i];
		}
		else if ((arg == "-I" || arg == "--import" || arg == "import") && i + 1 < argc)
		{
			mode = OperationMode::Import;
			filename = argv[++i];
		}
		else if (arg == "--dont-check")
			safeImport = true;
		else if ((arg == "-E" || arg == "--export" || arg == "export") && i + 1 < argc)
		{
			mode = OperationMode::Export;
			filename = argv[++i];
		}
		else if (arg == "--script" && i + 1 < argc)
			scripts.push_back(argv[++i]);
		else if (arg == "--format" && i + 1 < argc)
		{
			string m = argv[++i];
			if (m == "binary")
				exportFormat = Format::Binary;
			else if (m == "hex")
				exportFormat = Format::Hex;
			else if (m == "human")
				exportFormat = Format::Human;
			else
			{
				LOG(ERROR) << "Bad " << arg << " option: " << m << "\n";
				return -1;
			}
		}
		else if (arg == "--to" && i + 1 < argc)
			exportTo = argv[++i];
		else if (arg == "--from" && i + 1 < argc)
			exportFrom = argv[++i];
		else if (arg == "--only" && i + 1 < argc)
			exportTo = exportFrom = argv[++i];
		else if (arg == "--upnp" && i + 1 < argc)
		{
			string m = argv[++i];
			if (isTrue(m))
				upnp = true;
			else if (isFalse(m))
				upnp = false;
			else
			{
				LOG(ERROR) << "Bad " << arg << " option: " << m << "\n";
				return -1;
			}
		}
		else if (arg == "--network-id" && i + 1 < argc)
			try {
				networkID = stol(argv[++i]);
			}
			catch (...)
			{
				LOG(ERROR) << "Bad " << arg << " option: " << argv[i] << "\n";
				return -1;
			}
		else if (arg == "--private" && i + 1 < argc)
			try {
				privateChain = argv[++i];
			}
			catch (...)
			{
				LOG(ERROR) << "Bad " << arg << " option: " << argv[i] << "\n";
				return -1;
			}
		else if (arg == "--independent" && i + 1 < argc)
			try {
				privateChain = argv[++i];
				noPinning = enableDiscovery = true;
			}
			catch (...)
			{
				LOG(ERROR) << "Bad " << arg << " option: " << argv[i] << "\n";
				return -1;
			}
		else if (arg == "-K" || arg == "--kill-blockchain" || arg == "--kill")
			withExisting = WithExisting::Kill;
		else if (arg == "-R" || arg == "--rebuild")
			withExisting = WithExisting::Verify;
		else if (arg == "-R" || arg == "--rescue")
			withExisting = WithExisting::Rescue;
		else if (arg == "--client-name" && i + 1 < argc)
			clientName = argv[++i];
		else if ((arg == "-a" || arg == "--address" || arg == "--author") && i + 1 < argc)
			try {
				author = h160(fromHex(argv[++i], WhenError::Throw));

			}
			catch (BadHexCharacter&)
			{
				LOG(ERROR) << "Bad hex in " << arg << " option: " << argv[i] << "\n";
				return -1;
			}
			catch (...)
			{
				LOG(ERROR) << "Bad " << arg << " option: " << argv[i] << "\n";
				return -1;
			}
		else if ((arg == "-s" || arg == "--import-secret") && i + 1 < argc)
		{
			Secret s(fromHex(argv[++i]));
			toImport.emplace_back(s);
		}
		else if ((arg == "-S" || arg == "--import-session-secret") && i + 1 < argc)
		{
			Secret s(fromHex(argv[++i]));
			toImport.emplace_back(s);
		}
		else if ((arg == "-d" || arg == "--path" || arg == "--db-path" || arg == "--datadir") && i + 1 < argc)
			setDataDir(argv[++i]);
		else if (arg == "--ipcpath" && i + 1 < argc )
			setIpcPath(argv[++i]);
		else if ((arg == "--genesis-json" || arg == "--genesis") && i + 1 < argc)
		{
			try
			{
				genesisJSON = contentsString(argv[++i]);
			}
			catch (...)
			{
				LOG(ERROR) << "Bad " << arg << " option: " << argv[i] << "\n";
				return -1;
			}
		}
		else if (arg == "--config" && i + 1 < argc)
		{
			try
			{
				setConfigPath(argv[++i]);
				configJSON = contentsString(getConfigPath());
			}
			catch (...)
			{
				LOG(ERROR) << "Bad " << arg << " option: " << argv[i] << "\n";
				return -1;
			}
		}
		else if (arg == "--extra-data" && i + 1 < argc)
		{
			try
			{
				extraData = fromHex(argv[++i]);
			}
			catch (...)
			{
				LOG(ERROR) << "Bad " << arg << " option: " << argv[i] << "\n";
				return -1;
			}
		}
		else if (arg == "--gas-floor" && i + 1 < argc)
			gasFloor = u256(argv[++i]);
		else if (arg == "--mainnet")
			chainParams = ChainParams(genesisInfo(eth::Network::MainNetwork), genesisStateRoot(eth::Network::MainNetwork));
		else if (arg == "--ropsten" || arg == "--testnet")
			chainParams = ChainParams(genesisInfo(eth::Network::Ropsten), genesisStateRoot(eth::Network::Ropsten));
		else if (arg == "--oppose-dao-fork")
		{
			chainParams = ChainParams(genesisInfo(eth::Network::MainNetwork), genesisStateRoot(eth::Network::MainNetwork));
			chainParams.otherParams["daoHardforkBlock"] = toHex(u256(-1) - 10, HexPrefix::Add);
		}
		else if (arg == "--bob")
		{
			cout << "Asking Bob for blocks (this should work in theoreum)..." << "\n";
			while (true)
			{
				u256 x(h256::random());
				u256 c;
				for (; x != 1; ++c)
				{
					x = (x & 1) == 0 ? x / 2 : 3 * x + 1;
					cout << toHex(x) << "\n";
					this_thread::sleep_for(chrono::seconds(1));
				}
				cout << "Block number: " << hex << c << "\n";
				exit(0);
			}
		}
		else if (arg == "--ask" && i + 1 < argc)
		{
			try
			{
				askPrice = u256(argv[++i]);
			}
			catch (...)
			{
				LOG(ERROR) << "Bad " << arg << " option: " << argv[i] << "\n";
				return -1;
			}
		}
		else if (arg == "--bid" && i + 1 < argc)
		{
			try
			{
				bidPrice = u256(argv[++i]);
			}
			catch (...)
			{
				LOG(ERROR) << "Bad " << arg << " option: " << argv[i] << "\n";
				return -1;
			}
		}
		else if ((arg == "-m" || arg == "--mining") && i + 1 < argc)
		{
			string m = argv[++i];
			if (isTrue(m))
				mining = ~(unsigned)0;
			else if (isFalse(m))
				mining = 0;
			else
				try {
					mining = stoi(m);
				}
				catch (...) {
					LOG(ERROR) << "Unknown " << arg << " option: " << m << "\n";
					return -1;
				}
		}
		else if (arg == "-b" || arg == "--bootstrap")
			bootstrap = true;
		else if (arg == "--no-bootstrap")
			bootstrap = false;
		else if (arg == "--no-discovery")
		{
			disableDiscovery = true;
			bootstrap = false;
		}
		else if (arg == "--pin")
			pinning = true;
		else if (arg == "--hermit")
			pinning = disableDiscovery = true;
		else if (arg == "--sociable")
			noPinning = enableDiscovery = true;
		else if (arg == "--unsafe-transactions")
			alwaysConfirm = false;
		else if (arg == "--import-presale" && i + 1 < argc)
			presaleImports.push_back(argv[++i]);
		/*else if (arg == "--old-interactive")
			interactive = true;*/

		else if ((arg == "-j" || arg == "--json-rpc"))
			jsonRPCURL = jsonRPCURL == -1 ? SensibleHttpPort : jsonRPCURL;
		else if (arg == "--admin-via-http")
			adminViaHttp = true;
		else if (arg == "--json-rpc-port" && i + 1 < argc)
			jsonRPCURL = atoi(argv[++i]);
		else if (arg == "--rpccorsdomain" && i + 1 < argc)
			rpcCorsDomain = argv[++i];
		else if (arg == "--json-admin" && i + 1 < argc)
			jsonAdmin = argv[++i];
		else if (arg == "--ipc")
			ipc = true;
		else if (arg == "--no-ipc")
			ipc = false;
		else if ((arg == "-x" || arg == "--peers") && i + 1 < argc)
			peers = atoi(argv[++i]);
		else if (arg == "--peer-stretch" && i + 1 < argc)
			peerStretch = atoi(argv[++i]);
		else if (arg == "--peerset" && i + 1 < argc)
		{
			string peerset = argv[++i];
			if (peerset.empty())
			{
				LOG(ERROR) << "--peerset argument must not be empty";
				return -1;
			}

			vector<string> each;
			boost::split(each, peerset, boost::is_any_of("\t "));
			for (auto const& p : each)
			{
				string type;
				string pubk;
				string hostIP;
				unsigned short port = c_defaultListenPort;

				// type:key@ip[:port]
				vector<string> typeAndKeyAtHostAndPort;
				boost::split(typeAndKeyAtHostAndPort, p, boost::is_any_of(":"));
				if (typeAndKeyAtHostAndPort.size() < 2 || typeAndKeyAtHostAndPort.size() > 3)
					continue;

				type = typeAndKeyAtHostAndPort[0];
				if (typeAndKeyAtHostAndPort.size() == 3)
					port = (uint16_t)atoi(typeAndKeyAtHostAndPort[2].c_str());

				vector<string> keyAndHost;
				boost::split(keyAndHost, typeAndKeyAtHostAndPort[1], boost::is_any_of("@"));
				if (keyAndHost.size() != 2)
					continue;
				pubk = keyAndHost[0];
				if (pubk.size() != 128)
					continue;
				hostIP = keyAndHost[1];

				// todo: use Network::resolveHost()
				if (hostIP.size() < 4 /* g.it */)
					continue;

				bool required = type == "required";
				if (!required && type != "default")
					continue;

				Public publicKey(fromHex(pubk));
				try
				{
					preferredNodes[publicKey] = make_pair(NodeIPEndpoint(bi::address::from_string(hostIP), port, port), required);
				}
				catch (...)
				{
					LOG(ERROR) << "Unrecognized peerset: " << peerset << "\n";
					return -1;
				}
			}
		}
		else if ((arg == "-o" || arg == "--mode") && i + 1 < argc)
		{
			string m = argv[++i];
			if (m == "full")
				nodeMode = NodeMode::Full;
			else if (m == "peer")
				nodeMode = NodeMode::PeerServer;
			else
			{
				LOG(ERROR) << "Unknown mode: " << m << "\n";
				return -1;
			}
		}
#if ETH_EVMJIT
		else if (arg == "--vm" && i + 1 < argc)
		{
			string vmKind = argv[++i];
			if (vmKind == "interpreter")
				VMFactory::setKind(VMKind::Interpreter);
			else if (vmKind == "jit")
				VMFactory::setKind(VMKind::JIT);
			else if (vmKind == "smart")
				VMFactory::setKind(VMKind::Smart);
			else
			{
				LOG(ERROR) << "Unknown VM kind: " << vmKind << "\n";
				return -1;
			}
		}
#endif
		else if (arg == "--shh")
			useWhisper = true;
		else if (arg == "-h" || arg == "--help")
			help();
		else if (arg == "-V" || arg == "--version")
			version();
		else if (arg == "--cainittype")
		{
			setCaInitType(argv[++i]);
		}
		else if (arg == "--gennetworkrlp")
		{
			//string sFilePath = argv[++i];
			//generateNetworkRlp(sFilePath);
			string sFilePath = argv[++i];
			cout << "sFilePath:" << sFilePath << "\n"; //获取文件路径
			LOG(DEBUG) << "sFilePath:" << sFilePath;
			if (!sFilePath.empty())
			{
				CryptoParam cryptoParam;
				cryptoParam = cryptoParam.loadDataCryptoConfig(sFilePath);

				cout << "cryptoMod:" << cryptoParam.m_cryptoMod << "\n"; //加密模式
				cout << "kcUrl:" << cryptoParam.m_kcUrl << "\n"; //keycenter加密时访问地址
				cout << "nodekeyPath:" << cryptoParam.m_nodekeyPath << "\n"; //nodekey生成路径
				cout << "datakeyPath:" << cryptoParam.m_datakeyPath << "\n"; //datakey生成路径

				LOG(DEBUG) << "cryptoMod:" << cryptoParam.m_cryptoMod;
				LOG(DEBUG) << "kcUrl:" << cryptoParam.m_kcUrl;
				LOG(DEBUG) << "nodekeyPath:" << cryptoParam.m_nodekeyPath;
				LOG(DEBUG) << "datakeyPath:" << cryptoParam.m_datakeyPath;

				//根据加密模式  可选配
				GenKey genKey;
				genKey.setCryptoMod(cryptoParam.m_cryptoMod);
				genKey.setKcUrl(cryptoParam.m_kcUrl);
				genKey.setSuperKey(cryptoParam.m_superKey);
				genKey.setNodeKeyPath(cryptoParam.m_nodekeyPath);
				genKey.setDataKeyPath(cryptoParam.m_datakeyPath);
				genKey.setKeyData();//生成datakey及nodekey文件
				exit(0);
			}
			else
			{
				cout << "--gennetworkrlp parameter err" << "\n";
				exit(-1);
			}
		}
		else if (arg == "--enprivatekey")
		{
			string sFilePath = argv[++i];
			cout << "sFilePath:" << sFilePath << "\n"; //获取文件路径
			LOG(DEBUG) << "sFilePath:" << sFilePath;

			if (!sFilePath.empty())
			{
				CryptoParam cryptoParam;
				cryptoParam = cryptoParam.loadDataCryptoConfig(sFilePath);
				cout << "privatekeyPath:" << cryptoParam.m_privatekeyPath << "\n"; //私钥文件路径
				cout << "kcUrl:" << cryptoParam.m_kcUrl << "\n"; //keycenter加密时访问地址
				cout << "enprivatekeyPath:" << cryptoParam.m_enprivatekeyPath << "\n";
				//根据加密模式  可选配
				GenKey genKey;
				genKey.setPrivateKeyPath(cryptoParam.m_privatekeyPath);
				genKey.setEnPrivateKeyPath(cryptoParam.m_enprivatekeyPath);
				genKey.setKcUrl(cryptoParam.m_kcUrl);
				genKey.setPrivateKey();
				exit(0);
			}
			else
			{
				cout << "--enprivatekey parameter err" << "\n";
				exit(-1);
			}
		}
		else if (arg == "--test")
		{
			testingMode = true;
			enableDiscovery = false;
			disableDiscovery = true;
			noPinning = true;
			bootstrap = false;
		}
		else if ( arg == "--singlepoint" )
		{
			singlepoint = true;
			enableDiscovery = false;
			disableDiscovery = true;
			noPinning = true;
			bootstrap = false;
		}
		else if ( arg == "--godminer" )
		{
			try
			{
				godminerJSON = contentsString(argv[++i]);
			}
			catch (...)
			{
				LOG(ERROR) << "上帝模式参数文件异常！ " << arg << "  option: " << argv[i] << "\n";
				exit(-1);
			}

		}
		else
		{
			LOG(ERROR) << "Invalid argument: " << arg << "\n";
			exit(-1);
		}
	}

	if (!configJSON.empty())
	{
		try
		{
			chainParams = chainParams.loadConfig(configJSON);
			//初始化日志
			initEasylogging(chainParams);
		}
		catch (std::exception &e)
		{
			LOG(ERROR) << "provided configuration is not well formatted" << e.what() << "\n";
			LOG(ERROR) << "sample: " << "\n" << c_configJsonForWe << "\n";
			exit(-1);
		}
	}
	else
	{
		LOG(ERROR) << "请指定配置文件 --config xxx" << "\n";
		LOG(ERROR) << "sample: " << "\n" << c_configJsonForWe << "\n";
		exit(-1);
	}

	if (!godminerJSON.empty())
	{
		try
		{
			chainParams = chainParams.loadGodMiner(godminerJSON);

			if ( chainParams.godMinerStart < 1 )
			{
				LOG(ERROR) << "上帝模式配置异常 godMinerStart不能小于0 " << "\n";
				exit(-1);
			}
			if ( chainParams.godMinerEnd <= chainParams.godMinerStart  )
			{
				LOG(ERROR) << "上帝模式配置异常 godMinerEnd<=godMinerStart " << "\n";
				exit(-1);
			}
			if (  chainParams.godMinerList.size() < 1 )
			{
				LOG(ERROR) << "上帝模式配置异常 godMinerList不能为空 " << "\n";
				exit(-1);
			}

			cout << "开启上帝模式！！！！！ " << "godMinerStart=" << chainParams.godMinerStart << ",godMinerEnd=" << chainParams.godMinerEnd << ",godMinerList.size()=" << chainParams.godMinerList.size() << "\n";

		}
		catch (std::exception &e)
		{
			LOG(ERROR) << "上帝模式配置格式错误" << e.what() << "\n";
			exit(-1);
		}
	}

	if (!genesisJSON.empty())
	{
		try
		{
			chainParams = chainParams.loadGenesis(genesisJSON);
		}
		catch (...)
		{
			LOG(ERROR) << "provided genesis block description is not well formatted" << "\n";
			LOG(ERROR) << "sample: " << "\n" << c_genesisJsonForWe << "\n";
			return 0;
		}
	}
	else
	{
		LOG(ERROR) << "请指定创世块文件 --genesis xxx" << "\n";
		LOG(ERROR) << "sample: " << "\n" << c_genesisJsonForWe << "\n";
		return 0;
	}


	if (!privateChain.empty())
	{
		chainParams.extraData = sha3(privateChain).asBytes();
		chainParams.difficulty = chainParams.u256Param("minimumDifficulty");
		chainParams.gasLimit = u256(1) << 32;
	}
	// TODO: Open some other API path
//	if (gasFloor != Invalid256)
//		c_gasFloorTarget = gasFloor;


	cout << EthGrayBold "---------------cpp-ethereum, a C++ Ethereum client--------------" EthReset << "\n";

	chainParams.otherParams["allowFutureBlocks"] = "1";//默认打开
	if (testingMode)
	{
		chainParams.sealEngineName = "NoProof";
	}
	else if ( singlepoint )
	{
		chainParams.sealEngineName = "SinglePoint";
	}
	if (  "SinglePoint" == chainParams.sealEngineName )
	{
		enableDiscovery = false;
		disableDiscovery = true;
		noPinning = true;
		bootstrap = false;
	}

	//以配置文件中为准

	cout << "RPCPORT:" << chainParams.rpcPort << "\n";
	cout << "SSLRPCPORT" << chainParams.rpcSSLPort << "\n";
	cout << "CHANNELPORT:" << chainParams.channelPort << "\n";
	cout << "LISTENIP:" << chainParams.listenIp << "\n";
	cout << "P2PPORT:" << chainParams.p2pPort << "\n";
	cout << "WALLET:" << chainParams.wallet << "\n";
	cout << "KEYSTOREDIR:" << chainParams.keystoreDir << "\n";
	cout << "DATADIR:" << chainParams.dataDir << "\n";
	cout << "VM:" << chainParams.vmKind << "\n";
	cout << "SEALENGINE:" << chainParams.sealEngineName << "\n";
	cout << "NETWORKID:" << chainParams.networkId << "\n";
	cout << "SYSTEMPROXYADDRESS:" << chainParams.sysytemProxyAddress << "\n";
	cout << "GOD:" << chainParams.god << "\n";
	cout << "LOGVERBOSITY:" << chainParams.logVerbosity << "\n";
	cout << "EVENTLOG:" << (chainParams.evmEventLog ? "ON" : "OFF") << "\n";
	cout << "COVERLOG:" << (chainParams.evmCoverLog ? "ON" : "OFF") << "\n";

	jsonRPCURL = chainParams.rpcPort;
	jsonRPCSSLURL = chainParams.rpcSSLPort;
	setDataDir(chainParams.dataDir);
	listenIP = chainParams.listenIp;
	if ( !listenIP.empty() )
		listenSet = true;
	listenPort = chainParams.p2pPort;
	remotePort = chainParams.p2pPort;
	c_defaultListenPort = chainParams.p2pPort;
	c_defaultIPPort = chainParams.p2pPort;
	SecretStore::defaultpath = chainParams.keystoreDir;
	KeyManager::defaultpath = chainParams.wallet;
	networkID = chainParams.networkId;

	strNodeId = chainParams.nodeId;
	strGroupId = chainParams.groupId;
	strStoragePath = chainParams.storagePath;


	if (chainParams.vmKind == "interpreter")
		VMFactory::setKind(VMKind::Interpreter);
	else if (chainParams.vmKind == "jit")
		VMFactory::setKind(VMKind::JIT);
	else if (chainParams.vmKind == "smart")
		VMFactory::setKind(VMKind::Smart);
	else if (chainParams.vmKind == "dual")
		VMFactory::setKind(VMKind::Dual);
	else
	{
		LOG(ERROR) << "Error :Unknown VM kind " << chainParams.vmKind << "\n";
		return -1;
	}

	cout << EthGrayBold "---------------------------------------------------------------" EthReset << "\n";


	//m.execute();

	std::string secretsPath;
	if (testingMode)
		secretsPath = chainParams.keystoreDir; // (boost::filesystem::path(getDataDir()) / "keystore").string();
	else
		secretsPath = SecretStore::defaultPath();

	KeyManager keyManager(KeyManager::defaultPath(), secretsPath);
	for (auto const& s : passwordsToNote)
		keyManager.notePassword(s);

	// the first value is deprecated (never used)
	//writeFile(configFile, rlpList(author, author));

	if (!clientName.empty())
		clientName += "/";

	string logbuf;
	std::string additional;


	auto getPassword = [&](string const & prompt) {
		bool s = g_silence;
		g_silence = true;
		cout << "\n";
		string ret = dev::getPassword(prompt);
		g_silence = s;
		return ret;
	};
	/*auto getResponse = [&](string const& prompt, unordered_set<string> const& acceptable) {
		bool s = g_silence;
		g_silence = true;
		cout << "\n";
		string ret;
		while (true)
		{
			cout << prompt;
			getline(cin, ret);
			if (acceptable.count(ret))
				break;
			cout << "Invalid response: " << ret << "\n";
		}
		g_silence = s;
		return ret;
	};*/
	auto getAccountPassword = [&](Address const & ) {

		cout << "！！！！！！请通过web3解锁帐号！！！！！" << "\n";
		return string();

		//return getPassword("Enter password for address " + keyManager.accountName(a) + " (" + a.abridged() + "; hint:" + keyManager.passwordHint(a) + "): ");
	};

	auto netPrefs = publicIP.empty() ? NetworkPreferences(listenIP, listenPort, upnp) : NetworkPreferences(publicIP, listenIP , listenPort, upnp);
	netPrefs.discovery = (privateChain.empty() && !disableDiscovery) || enableDiscovery;
	netPrefs.pin = (pinning || !privateChain.empty()) && !noPinning;

	//底层SSL连接
	dev::setSSL(chainParams.ssl);//设置是否启用SSL功能
	dev::setCryptoPrivateKeyMod(chainParams.cryptoprivatekeyMod);//设置私钥加密方式

	GenKey genKey;
	CryptoParam cryptoParam;
	if (chainParams.cryptoprivatekeyMod != 0)//私钥是否使用keycenter加密
	{
		cryptoParam = cryptoParam.loadDataCryptoConfig(getDataDir() + "/cryptomod.json");//获取加密配置文件
		genKey.setKcUrl(cryptoParam.m_kcUrl);//keycenter连接地址
	}
	if (chainParams.ssl != 0)
	{
		string privateKey = genKey.getPrivateKey();//获取证书私钥文件
		if (privateKey.empty())
		{
			cout << "server.key file err......................" << "\n";
			exit(-1);
		}
	}

	//auto nodesState = contents(getDataDir() + "/network.rlp");
	//添加落盘加密代码
	bytes nodesState;
	if (chainParams.cryptoMod != 0)
	{
		GenKey genKey;
		CryptoParam cryptoParam;
		cryptoParam = cryptoParam.loadDataCryptoConfig(getDataDir() + "/cryptomod.json");//获取加密配置文件

		genKey.setCryptoMod(cryptoParam.m_cryptoMod);
		genKey.setKcUrl(cryptoParam.m_kcUrl);
		genKey.setSuperKey(cryptoParam.m_superKey);
		genKey.setNodeKeyPath(cryptoParam.m_nodekeyPath);
		genKey.setDataKeyPath(cryptoParam.m_datakeyPath);
		nodesState = genKey.getKeyData();//获取明文nodekey数据并把datakey数据存放在内存中
		LOG(DEBUG) << "Begin Crypto Data With Mode:" << chainParams.cryptoMod;
	}
	else
	{
		dev::setCryptoMod(chainParams.cryptoMod);
		nodesState = contents(getDataDir() + "/network.rlp");
	}

	auto caps = useWhisper ? set<string> {"eth", "shh"} : set<string> {"eth"};

	//启动channelServer
	ChannelRPCServer::Ptr channelServer = std::make_shared<ChannelRPCServer>();

	//建立web3网络
	dev::WebThreeDirect web3(
	    WebThreeDirect::composeClientVersion("eth"),
	    getDataDir(),
	    chainParams,
	    withExisting,
	    nodeMode == NodeMode::Full ? caps : set<string>(),
	    netPrefs,
	    &nodesState,
	    testingMode
	);

	channelServer->setHost(web3.ethereum()->host());
	web3.ethereum()->host().lock()->setWeb3Observer(channelServer->buildObserver());

	cout << "NodeID=" << toString(web3.id()) << "\n";

	if (!extraData.empty())
		web3.ethereum()->setExtraData(extraData);

	auto toNumber = [&](string const & s) -> unsigned {
		if (s == "latest")
			return web3.ethereum()->number();
		if (s.size() == 64 || (s.size() == 66 && s.substr(0, 2) == "0x"))
			return web3.ethereum()->blockChain().number(h256(s));
		try {
			return stol(s);
		}
		catch (...)
		{
			LOG(ERROR) << "Bad block number/hash option: " << s << "\n";
			exit(-1);
		}
	};

	//生成创世块
	if (mode == OperationMode::ExportGenesis) {
		LOG(INFO) << "生成创世块到:" << filename;

		ofstream fout(filename, std::ofstream::binary);
		ostream& out = (filename.empty() || filename == "--") ? cout : fout;

		auto state = web3.ethereum()->block(
		                 web3.ethereum()->blockChain().currentHash()).state();

		if (blockNumber > 0) {
			state = web3.ethereum()->block(blockNumber).state();
		}

		std::vector<std::string> splitContract;
		boost::split(splitContract, contracts, boost::is_any_of(";"));

		std::vector<Address> contractList;
		for (auto it = splitContract.begin(); it != splitContract.end(); ++it) {
			if (!it->empty()) {
				contractList.push_back(Address(*it));
			}
		}

		if (contractList.empty()) {
			LOG(INFO) << "未指定合约地址列表，导出所有合约";
			unordered_map<Address, u256> contractMap = state.addresses();

			for (auto it = contractMap.begin(); it != contractMap.end(); ++it) {
				contractList.push_back(it->first);
			}
		}

		Json::Value genesis;
		Json::Reader reader;
		reader.parse(genesisJSON, genesis, false);

		//写入json
		Json::Value root;
		Json::FastWriter writer;

		root["nonce"] = genesis["nonce"];
		root["difficulty"] = genesis["difficulty"];
		root["mixhash"] = genesis["mixhash"];
		root["coinbase"] = genesis["coinbase"];
		root["timestamp"] = genesis["timestamp"];
		root["parentHash"] = genesis["parentHash"];
		root["extraData"] = genesis["extraData"];
		root["gasLimit"] = genesis["gasLimit"];
		root["god"] = genesis["god"];
		root["initMinerNodes"] = genesis["initMinerNodes"];

		Json::Value alloc;
		auto allocFlag = false;
		for (auto it = contractList.begin(); it != contractList.end(); ++it) {
			LOG(INFO) << "导出合约:" << *it;

			u256 balance = state.balance(*it);
			bytes code = state.code(*it);
			u256 nonce = state.getNonce(*it);

			auto storage = state.storage(*it);

			Json::Value contract;
			contract["balance"] = toString(balance);
			contract["code"] = toHex(code, 2, dev::HexPrefix::Add);
			contract["nonce"] = toString(nonce);

			Json::Value storageJson;
			auto flag = false;
			for (auto storageIt = storage.begin(); storageIt != storage.end(); ++storageIt) {
				storageJson[toString(storageIt->second.first)] = toString(storageIt->second.second);
				flag = true;
			}
			if (flag)
			{
				contract["storage"] = storageJson;
			}
			alloc[it->hex()] = contract;
			allocFlag = true;
		}
		if (allocFlag)
		{
			root["alloc"] = alloc;
		}
		else
		{
			root["alloc"] = "{}";
		}

		out << writer.write(root);

		out.flush();
		exit(0);
		return 0;
	}

	if (mode == OperationMode::Export)
	{
		ofstream fout(filename, std::ofstream::binary);
		ostream& out = (filename.empty() || filename == "--") ? cout : fout;

		unsigned last = toNumber(exportTo);
		for (unsigned i = toNumber(exportFrom); i <= last; ++i)
		{
			bytes block = web3.ethereum()->blockChain().block(web3.ethereum()->blockChain().numberHash(i));
			switch (exportFormat)
			{
			case Format::Binary: out.write((char const*)block.data(), block.size()); break;
			case Format::Hex: out << toHex(block) << "\n"; break;
			case Format::Human: out << RLP(block) << "\n"; break;
			default:;
			}
		}
		return 0;
	}

	if (mode == OperationMode::Import)
	{
		ifstream fin(filename, std::ifstream::binary);
		istream& in = (filename.empty() || filename == "--") ? cin : fin;
		unsigned alreadyHave = 0;
		unsigned good = 0;
		unsigned futureTime = 0;
		unsigned unknownParent = 0;
		unsigned bad = 0;
		chrono::steady_clock::time_point t = chrono::steady_clock::now();
		double last = 0;
		unsigned lastImported = 0;
		unsigned imported = 0;
		while (in.peek() != -1)
		{
			bytes block(8);
			in.read((char*)block.data(), 8);
			block.resize(RLP(block, RLP::LaissezFaire).actualSize());
			in.read((char*)block.data() + 8, block.size() - 8);

			switch (web3.ethereum()->queueBlock(block, safeImport))
			{
			case ImportResult::Success: good++; break;
			case ImportResult::AlreadyKnown: alreadyHave++; break;
			case ImportResult::UnknownParent: unknownParent++; break;
			case ImportResult::FutureTimeUnknown: unknownParent++; futureTime++; break;
			case ImportResult::FutureTimeKnown: futureTime++; break;
			default: bad++; break;
			}

			// sync chain with queue
			tuple<ImportRoute, bool, unsigned> r = web3.ethereum()->syncQueue(10);
			imported += get<2>(r);

			double e = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - t).count() / 1000.0;
			if ((unsigned)e >= last + 10)
			{
				auto i = imported - lastImported;
				auto d = e - last;
				cout << i << " more imported at " << (round(i * 10 / d) / 10) << " blocks/s. " << imported << " imported in " << e << " seconds at " << (round(imported * 10 / e) / 10) << " blocks/s (#" << web3.ethereum()->number() << ")" << "\n";
				last = (unsigned)e;
				lastImported = imported;
//				cout << web3.ethereum()->blockQueueStatus() << "\n";
			}
		}

		while (web3.ethereum()->blockQueue().items().first + web3.ethereum()->blockQueue().items().second > 0)
		{
			this_thread::sleep_for(chrono::seconds(1));
			web3.ethereum()->syncQueue(100000);
		}
		double e = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - t).count() / 1000.0;
		cout << imported << " imported in " << e << " seconds at " << (round(imported * 10 / e) / 10) << " blocks/s (#" << web3.ethereum()->number() << ")" << "\n";
		return 0;
	}

	try
	{
		if (keyManager.exists())
		{
			if (!keyManager.load(masterPassword) && masterSet)
			{
				while (true)
				{
					masterPassword = getPassword("Please enter your MASTER password: ");
					if (keyManager.load(masterPassword))
						break;
					cout << "The password you entered is incorrect. If you have forgotten your password, and you wish to start afresh, manually remove the file: " + getDataDir("ethereum") + "/keys.info" << "\n";
				}
			}
		}
		else
		{
			if (masterSet)
				keyManager.create(masterPassword);
			else
				keyManager.create(std::string());
		}
	}
	catch (...)
	{
		LOG(ERROR) << "Error initializing key manager: " << boost::current_exception_diagnostic_information() << "\n";
		return -1;
	}

	for (auto const& presale : presaleImports)
		importPresale(keyManager, presale, [&]() { return getPassword("Enter your wallet password for " + presale + ": "); });

	for (auto const& s : toImport)
	{
		keyManager.import(s, "Imported key (UNSAFE)");
	}

	cout << ethCredits();
	web3.setIdealPeerCount(peers);
	web3.setPeerStretch(peerStretch);
//	std::shared_ptr<eth::BasicGasPricer> gasPricer = make_shared<eth::BasicGasPricer>(u256(double(ether / 1000) / etherPrice), u256(blockFees * 1000));
	std::shared_ptr<eth::TrivialGasPricer> gasPricer = make_shared<eth::TrivialGasPricer>(askPrice, bidPrice);
	eth::Client* c = nodeMode == NodeMode::Full ? web3.ethereum() : nullptr;
	if (c)
	{
		c->setGasPricer(gasPricer);
		//DEV_IGNORE_EXCEPTIONS(asEthashClient(c)->setShouldPrecomputeDAG(true/*m.shouldPrecompute()*/));
		c->setSealer(""/*m.minerType()*/);
		c->setAuthor(author);
		if (networkID != NoNetworkID)
			c->setNetworkId(networkID);
	}

	auto renderFullAddress = [&](Address const & _a) -> std::string
	{
		return ICAP(_a).encoded() + " (" + toUUID(keyManager.uuid(_a)) + " - " + toHex(_a.ref().cropped(0, 4)) + ")";
	};

	if (author)
		LOG(TRACE) << " Main:: Mining Beneficiary: " << renderFullAddress(author) << "," << author;

	if (bootstrap || !remoteHost.empty() || enableDiscovery || listenSet)
	{
		//启动网络
		web3.startNetwork();
		cout << "Node ID: " << web3.enode() << "\n";
	}
	else
		cout << "Networking disabled. To start, use netstart or pass --bootstrap or a remote host." << "\n";

	unique_ptr<ModularServer<>> channelModularServer;
	unique_ptr<ModularServer<>> jsonrpcHttpServer;
	unique_ptr<ModularServer<>> jsonrpcHttpsServer;
	unique_ptr<ModularServer<>> jsonrpcIpcServer;
	unique_ptr<rpc::SessionManager> sessionManager;
	unique_ptr<SimpleAccountHolder> accountHolder;

	AddressHash allowedDestinations;

	std::function<bool(TransactionSkeleton const&, bool)> authenticator;
	if (testingMode)
		authenticator = [](TransactionSkeleton const&, bool) -> bool { return true; };
	else
		authenticator = [&](TransactionSkeleton const & _t, bool/* isProxy*/) -> bool {
		// "unlockAccount" functionality is done in the AccountHolder.
		if (!alwaysConfirm || allowedDestinations.count(_t.to))
			return true;

		string r = "always";/*getResponse(_t.userReadable(isProxy,
				[&](TransactionSkeleton const& _t) -> pair<bool, string>
				{
					h256 contractCodeHash = web3.ethereum()->postState().codeHash(_t.to);
					if (contractCodeHash == EmptySHA3)
						return std::make_pair(false, std::string());
					// TODO: actually figure out the natspec. we'll need the natspec database here though.
					return std::make_pair(true, std::string());
				}, [&](Address const& _a) { return ICAP(_a).encoded() + " (" + _a.abridged() + ")"; }
			) + "\nEnter yes/no/always (always to this address): ", {"yes", "n", "N", "no", "NO", "always"});
			*/
		if (r == "always")
			allowedDestinations.insert(_t.to);
		return r == "yes" || r == "always";
	};

	ExitHandler exitHandler;

	if (jsonRPCURL > -1 || ipc || jsonRPCSSLURL > -1)
	{
		using FullServer = ModularServer <
		                   rpc::EthFace, rpc::DBFace, rpc::WhisperFace,
		                   rpc::NetFace, rpc::Web3Face, rpc::PersonalFace,
		                   rpc::AdminEthFace, rpc::AdminNetFace, rpc::AdminUtilsFace,
		                   rpc::DebugFace, rpc::TestFace
		                   >;

		sessionManager.reset(new rpc::SessionManager());
		accountHolder.reset(new SimpleAccountHolder([&]() { return web3.ethereum(); }, getAccountPassword, keyManager, authenticator));
		auto ethFace = new rpc::Eth(*web3.ethereum(), *accountHolder.get());
		rpc::TestFace* testEth = nullptr;
		if (testingMode)
			testEth = new rpc::Test(*web3.ethereum());

		string limitConfigJSON = contentsString(chainParams.rateLimitConfig);

		if (jsonRPCURL >= 0)
		{
			rpc::AdminEth* adminEth = nullptr;
			rpc::PersonalFace* personal = nullptr;
			rpc::AdminNet* adminNet = nullptr;
			rpc::AdminUtils* adminUtils = nullptr;
			if (adminViaHttp)
			{
				personal = new rpc::Personal(keyManager, *accountHolder, *web3.ethereum());
				adminEth = new rpc::AdminEth(*web3.ethereum(), *gasPricer.get(), keyManager, *sessionManager.get());
				adminNet = new rpc::AdminNet(web3, *sessionManager.get());
				adminUtils = new rpc::AdminUtils(*sessionManager.get());
			}

			jsonrpcHttpServer.reset(new FullServer(
			                            ethFace, new rpc::LevelDB(), new rpc::Whisper(web3, {}),
			                            new rpc::Net(web3), new rpc::Web3(web3.clientVersion()), personal,
			                            adminEth, adminNet, adminUtils,
			                            new rpc::Debug(*web3.ethereum()),
			                            testEth
			                        ));
			auto httpConnector = new SafeHttpServer(jsonRPCURL, "", "", SensibleHttpThreads, limitConfigJSON);
			httpConnector->setNode(strNodeId);
			httpConnector->setGroup(strGroupId);
			httpConnector->setStoragePath(strStoragePath);
			httpConnector->setEth(web3.ethereum());
			httpConnector->setAllowedOrigin(rpcCorsDomain);
			jsonrpcHttpServer->addConnector(httpConnector);
			jsonrpcHttpServer->setStatistics(new InterfaceStatistics(getDataDir() + "RPC", chainParams.statsInterval));
			if ( false == jsonrpcHttpServer->StartListening() )
			{
				cout << "RPC StartListening Fail!!!!" << "\n";
				exit(0);
			}

			// 这个地方要判断rpc启动的返回
		}

		if (jsonRPCSSLURL >= 0)
		{
			rpc::AdminEth* adminEth = nullptr;
			rpc::PersonalFace* personal = nullptr;
			rpc::AdminNet* adminNet = nullptr;
			rpc::AdminUtils* adminUtils = nullptr;
			if (adminViaHttp)
			{
				personal = new rpc::Personal(keyManager, *accountHolder, *web3.ethereum());
				adminEth = new rpc::AdminEth(*web3.ethereum(), *gasPricer.get(), keyManager, *sessionManager.get());
				adminNet = new rpc::AdminNet(web3, *sessionManager.get());
				adminUtils = new rpc::AdminUtils(*sessionManager.get());
			}

			jsonrpcHttpsServer.reset(new FullServer(
			                             ethFace, new rpc::LevelDB(), new rpc::Whisper(web3, {}),
			                             new rpc::Net(web3), new rpc::Web3(web3.clientVersion()), personal,
			                             adminEth, adminNet, adminUtils,
			                             new rpc::Debug(*web3.ethereum()),
			                             testEth
			                         ));
			auto httpConnector = new SafeHttpServer(jsonRPCSSLURL, getDataDir() + "/server.crt", getDataDir() + "/server.key", SensibleHttpThreads, limitConfigJSON);
			httpConnector->setNode(strNodeId);
			httpConnector->setGroup(strGroupId);
			httpConnector->setStoragePath(strStoragePath);
			httpConnector->setEth(web3.ethereum());
			httpConnector->setAllowedOrigin(rpcCorsDomain);
			jsonrpcHttpsServer->addConnector(httpConnector);
			jsonrpcHttpsServer->setStatistics(new InterfaceStatistics(getDataDir() + "RPCSSL", chainParams.statsInterval));
			if ( false == jsonrpcHttpsServer->StartListening() )
			{
				cout << "RPC SSL StartListening Fail!!!!" << "\n";
				//exit(0);
			}

			// 这个地方要判断rpc ssl启动的返回
		}

		if (ipc)
		{
			jsonrpcIpcServer.reset(new FullServer(
			                           ethFace, new rpc::LevelDB(), new rpc::Whisper(web3, {}), new rpc::Net(web3),
			                           new rpc::Web3(web3.clientVersion()), new rpc::Personal(keyManager, *accountHolder, *web3.ethereum()),
			                           new rpc::AdminEth(*web3.ethereum(), *gasPricer.get(), keyManager, *sessionManager.get()),
			                           new rpc::AdminNet(web3, *sessionManager.get()),
			                           new rpc::AdminUtils(*sessionManager.get()),
			                           new rpc::Debug(*web3.ethereum()),
			                           testEth
			                       ));
			auto ipcConnector = new IpcServer("geth");
			jsonrpcIpcServer->addConnector(ipcConnector);
			jsonrpcIpcServer->setStatistics(new InterfaceStatistics(getDataDir() + "IPC", chainParams.statsInterval));
			ipcConnector->StartListening();
		}

		//启动ChannelServer
		if (!chainParams.listenIp.empty() && chainParams.channelPort > 0) {
			channelModularServer.reset(
			    new FullServer(ethFace, new rpc::LevelDB(),
			                   new rpc::Whisper(web3, { }), new rpc::Net(web3),
			                   new rpc::Web3(web3.clientVersion()),
			                   new rpc::Personal(keyManager, *accountHolder,
			                                     *web3.ethereum()),
			                   new rpc::AdminEth(*web3.ethereum(),
			                                     *gasPricer.get(), keyManager,
			                                     *sessionManager.get()),
			                   new rpc::AdminNet(web3, *sessionManager.get()),
			                   new rpc::AdminUtils(*sessionManager.get()),
			                   new rpc::Debug(*web3.ethereum()), testEth)
			);

			channelServer->setListenAddr(chainParams.listenIp);
			channelServer->setListenPort(chainParams.channelPort);
			channelModularServer->addConnector(channelServer.get());

			LOG(TRACE) << "channelServer启动 IP:" << chainParams.listenIp << " Port:" << chainParams.channelPort;

			channelModularServer->StartListening();
			//设置json rpc的accountholder
            RPCallback::getInstance().setAccountHolder(accountHolder.get());
		}

		if (jsonAdmin.empty())
			jsonAdmin = sessionManager->newSession(rpc::SessionPermissions{{rpc::Privilege::Admin}});
		else
			sessionManager->addSession(jsonAdmin, rpc::SessionPermissions{{rpc::Privilege::Admin}});

		LOG(TRACE) << "JSONRPC Admin Session Key: " << jsonAdmin ;
		writeFile(getDataDir("web3") + "/session.key", jsonAdmin);
		writeFile(getDataDir("web3") + "/session.url", "http://localhost:" + toString(jsonRPCURL));
	}

	for (auto const& p : preferredNodes)
		if (p.second.second)
			web3.requirePeer(p.first, p.second.first);
		else
			web3.addNode(p.first, p.second.first);

	if (bootstrap && privateChain.empty())
		for (auto const& i : Host::pocHosts())
			web3.requirePeer(i.first, i.second);
	if (!remoteHost.empty())
		web3.addNode(p2p::NodeID(), remoteHost + ":" + toString(remotePort));

	signal(SIGABRT, &ExitHandler::exitHandler);
	signal(SIGTERM, &ExitHandler::exitHandler);
	signal(SIGINT, &ExitHandler::exitHandler);

	unsigned account_type = ~(unsigned)0;
	if (NodeConnManagerSingleton::GetInstance().getAccountType(web3.id(), account_type)) {
		mining = ~(unsigned)0; // 有account_type配置，是否挖矿由account_type决定
	} else {
		LOG(ERROR) << "getAccountType error......" << "\n";
	}

	if (c)
	{
		unsigned n = c->blockChain().details().number;
		if (mining) {
			int try_cnt = 0;
			unsigned node_num = NodeConnManagerSingleton::GetInstance().getNodeNum();
			LOG(INFO) << "getNodeNum node_num is " << node_num << "\n";
			while (try_cnt++ < 5 && node_num > 0 && web3.peerCount() < node_num - 1) {
				LOG(INFO) << "Wait for connecting to peers........" << "\n";
				std::this_thread::sleep_for(std::chrono::seconds(1));
				continue;
			}
			LOG(INFO) << "Connected to " << web3.peerCount() << " peers" << "\n";
			LOG(INFO) << "startSealing ....." << "\n";
			c->startSealing();
		}

		while (!exitHandler.shouldExit())
		{
			stopSealingAfterXBlocks(c, n, mining);
			logRotateByTime();
		}
	}
	else
	{
	    while (!exitHandler.shouldExit())
		{
			this_thread::sleep_for(chrono::milliseconds(1000));
			logRotateByTime();
		}
	}
	

	if (jsonrpcHttpServer.get())
		jsonrpcHttpServer->StopListening();
	if (jsonrpcHttpsServer.get())
		jsonrpcHttpsServer->StopListening();
	if (jsonrpcIpcServer.get())
		jsonrpcIpcServer->StopListening();
	if (channelModularServer.get())
		channelModularServer->StopListening();

	/*	auto netData = web3.saveNetwork();
		if (!netData.empty())
			writeFile(getDataDir() + "/network.rlp", netData);*/
	return 0;
}
