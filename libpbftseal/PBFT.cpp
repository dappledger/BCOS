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
 * @file: PBFT.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include <boost/filesystem.hpp>
#include <libethcore/ChainOperationParams.h>
#include <libethcore/CommonJS.h>
#include <libethereum/Interface.h>
#include <libethereum/BlockChain.h>
#include <libethereum/EthereumHost.h>
#include <libethereum/NodeConnParamsManagerApi.h>
#include <libdevcrypto/Common.h>
#include "PBFT.h"
#include <libdevcore/easylog.h>
#include <libdevcore/LogGuard.h>
#include <libethereum/StatLog.h>
using namespace std;
using namespace dev;
using namespace eth;

void PBFT::init()
{
	ETH_REGISTER_SEAL_ENGINE(PBFT);
}

PBFT::PBFT()
{
}

PBFT::~PBFT() {
	if (m_backup_db) {
		delete m_backup_db;
	}
	stopWorking();
}

void PBFT::initEnv(std::weak_ptr<PBFTHost> _host, BlockChain* _bc, OverlayDB* _db, BlockQueue *bq, KeyPair const& _key_pair, unsigned _view_timeout)
{
	Guard l(m_mutex);

	m_host = _host;
	m_bc.reset(_bc);
	m_stateDB.reset(_db);
	m_bq.reset(bq);

	m_bc->setSignChecker([this](BlockHeader const & _header, std::vector<std::pair<u256, Signature>> _sign_list) {
		return checkBlockSign(_header, _sign_list);
	});

	m_key_pair = _key_pair;

	resetConfig();

	m_view_timeout = _view_timeout;
	m_consensus_block_number = 0;
	m_last_consensus_time =  utcTime();
	m_change_cycle = 0;
	m_to_view = 0;
	m_leader_failed = false;

	m_last_sign_time = 0;

	m_last_collect_time = std::chrono::system_clock::now();

	m_future_prepare_cache = std::make_pair(Invalid256, PrepareReq());

	m_last_exec_finish_time = utcTime();

	initBackupDB();

	LOG(INFO) << "PBFT initEnv success";
}

void PBFT::initBackupDB() {
	ldb::Options o;
	o.max_open_files = 256;
	o.create_if_missing = true;
	std::string path = m_bc->chainParams().dataDir + "/pbftMsgBackup";
	ldb::Status status = ldb::DB::Open(o, path, &m_backup_db);
	if (!status.ok() || !m_backup_db)
	{
		if (boost::filesystem::space(path).available < 1024)
		{
			LOG(ERROR) << "Not enough available space found on hard drive. Please free some up and then re-run. Bailing.";
			BOOST_THROW_EXCEPTION(NotEnoughAvailableSpace());
		}
		else
		{
			LOG(ERROR) << status.ToString();
			LOG(ERROR) << "Database " << path << "already open. You appear to have another instance of ethereum running. Bailing.";
			BOOST_THROW_EXCEPTION(DatabaseAlreadyOpen());
		}
	}

	// reload msg from db
	reloadMsg(backup_key_committed, &m_committed_prepare_cache);
}

void PBFT::resetConfig() {
	if (!NodeConnManagerSingleton::GetInstance().getAccountType(m_key_pair.pub(), m_account_type)) {
		LOG(ERROR) << "resetConfig: can't find myself id, stop sealing";
		m_cfg_err = true;
		return;
	}

	auto node_num = NodeConnManagerSingleton::GetInstance().getMinerNum();
	if (node_num == 0) {
		LOG(ERROR) << "resetConfig: miner_num = 0, stop sealing";
		m_cfg_err = true;
		return;
	}

	u256 node_idx;
	if (!NodeConnManagerSingleton::GetInstance().getIdx(m_key_pair.pub(), node_idx)) {
		//BOOST_THROW_EXCEPTION(PbftInitFailed() << errinfo_comment("NodeID not in cfg"));
		LOG(ERROR) << "resetConfig: can't find myself id, stop sealing";
		m_cfg_err = true;
		return;
	}

	if (node_num != m_node_num || node_idx != m_node_idx) {
		m_node_num = node_num;
		m_node_idx = node_idx;
		m_f = (m_node_num - 1 ) / 3;

		m_prepare_cache.clear();
		m_sign_cache.clear();
		m_recv_view_change_req.clear();

		if (!getMinerList(-1, m_miner_list)) {
			LOG(ERROR) << "resetConfig: getMinerList return false";
			m_cfg_err = true;
			return;
		}

		if (m_miner_list.size() != m_node_num) {
			LOG(ERROR) << "resetConfig: m_miner_list.size=" << m_miner_list.size() << ",m_node_num=" << m_node_num;
			m_cfg_err = true;
			return;
		}
		LOG(INFO) << "resetConfig: m_node_idx=" << m_node_idx << ", m_node_num=" << m_node_num;
	}

	m_cfg_err = false;
}

StringHashMap PBFT::jsInfo(BlockHeader const& _bi) const
{
	return { { "number", toJS(_bi.number()) }, { "timestamp", toJS(_bi.timestamp()) } };
}

bool PBFT::generateSeal(BlockHeader const& _bi, bytes const& _block_data, u256 &_view)
{
	Timer t;
	Guard l(m_mutex);
	_view = m_view;
	if (!broadcastPrepareReq(_bi, _block_data)) {
		LOG(ERROR) << "broadcastPrepareReq failed, " << _bi.number() << _bi.hash(WithoutSeal);
		return false;
	}

	LOG(DEBUG) << "generateSeal, blk=" << _bi.number() << ", timecost=" << 1000 * t.elapsed();

	return true;
}

bool PBFT::generateCommit(BlockHeader const& _bi, bytes const& _block_data, u256 const& _view)
{
	Guard l(m_mutex);

	if (_view != m_view) {
		LOG(INFO) << "view has changed, generateCommit failed, _view=" << _view << ", m_view=" << m_view;
		return false;
	}

	PrepareReq req;
	req.height = _bi.number();
	req.view = _view;
	req.idx = m_node_idx;
	req.timestamp = u256(utcTime());
	req.block_hash = _bi.hash(WithoutSeal);
	req.sig = signHash(req.block_hash);
	req.sig2 = signHash(req.fieldsWithoutBlock());
	req.block = _block_data;

	if (addPrepareReq(req) && broadcastSignReq(req)) {
		checkAndCommit(); // 支持单节点可出块
	}

	return true;
}

bool PBFT::shouldSeal(Interface*)
{
	Guard l(m_mutex);

	if (m_cfg_err || m_account_type != EN_ACCOUNT_TYPE_MINER) { // 配置中找不到自己或非记账节点就不出块,
		return false;
	}

	std::pair<bool, u256> ret = getLeader();

	if (!ret.first) {
		return false;
	}

	if (ret.second != m_node_idx) {
		if (auto h = m_host.lock()) {
			h512 node_id = h512(0);
			if (NodeConnManagerSingleton::GetInstance().getPublicKey(ret.second, node_id) && !h->isConnected(node_id)) {
				LOG(ERROR) << "getLeader ret:<" << ret.first << "," << ret.second << ">" << ", need viewchange for disconnected";
				m_last_consensus_time = 0;
				m_last_sign_time = 0;  // 两个都设置为0，才能保证快速切换
				m_signalled.notify_all();
			}
		}
		return false;
	}

	// 判断是否要把committed_prepare拿出来重放
	if (m_consensus_block_number == m_committed_prepare_cache.height) {
		if (m_consensus_block_number != m_raw_prepare_cache.height) {
			reHandlePrepareReq(m_committed_prepare_cache);
		}
		return false;
	}

	return true;
}

void PBFT::reHandlePrepareReq(PrepareReq const& _req) {
	LOG(INFO) << "shouldSeal: found an committed but not saved block, post out again. hash=" << m_committed_prepare_cache.block_hash.abridged();

	clearMask(); // to make sure msg will be delivered

	PrepareReq req;
	req.height = _req.height;
	req.view = m_view;
	req.idx = m_node_idx;
	req.timestamp = u256(utcTime());
	req.block_hash = _req.block_hash;
	req.sig = signHash(req.block_hash);
	req.sig2 = signHash(req.fieldsWithoutBlock());
	req.block = _req.block;

	LOG(INFO) << "BLOCK_TIMESTAMP_STAT:[" << toString(req.block_hash) << "][" << req.height << "][" <<  utcTime() << "][" << "broadcastPrepareReq" << "]";
	RLPStream ts;
	req.streamRLPFields(ts);
	broadcastMsg(req.block_hash.hex(), PrepareReqPacket, ts.out());

	handlePrepareMsg(m_node_idx, req, true); // 指明是来自自己的Req
}

std::pair<bool, u256> PBFT::getLeader() const {
	if (m_cfg_err || m_leader_failed || m_highest_block.number() == Invalid256) {
		return std::make_pair(false, Invalid256);
	}

	return std::make_pair(true, (m_view + m_highest_block.number()) % m_node_num);
}

void PBFT::reportBlock(BlockHeader const & _b, u256 const &) {
	Guard l(m_mutex);

	auto old_height = m_highest_block.number();
	auto old_view = m_view;

	m_highest_block = _b;

	if (m_highest_block.number() >= m_consensus_block_number) {
		m_view = m_to_view = m_change_cycle = 0;
		m_leader_failed = false;
		m_last_consensus_time = utcTime();
		m_consensus_block_number = m_highest_block.number() + 1;
		//m_recv_view_change_req.clear();
		delViewChange(); // 如果是最新块的viewchange，不能丢弃
	}

	resetConfig();

	delCache(m_highest_block.hash(WithoutSeal));

	LOG(INFO) << "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ Report: blk=" << m_highest_block.number() << ",hash=" << _b.hash(WithoutSeal).abridged() << ",idx=" << m_highest_block.genIndex() << ", Next: blk=" << m_consensus_block_number;
	// onchain log
	stringstream ss;
	ss << "blk:" << m_highest_block.number().convert_to<string>()
		<< " hash:" << _b.hash(WithoutSeal).abridged() << " idx:" << m_highest_block.genIndex().convert_to<string>()
		<< " next:" << m_consensus_block_number.convert_to<string>();
	PBFTFlowLog(old_height + old_view, ss.str());
}

void PBFT::onPBFTMsg(unsigned _id, std::shared_ptr<p2p::Capability> _peer, RLP const & _r) {
	if (_id <= ViewChangeReqPacket) {
		//LOG(INFO) << "onPBFTMsg: id=" << _id;
		u256 idx = u256(0);
		if (!NodeConnManagerSingleton::GetInstance().getIdx(_peer->session()->id(), idx)) {
			LOG(ERROR) << "Recv an pbft msg from unknown peer id=" << _id;
			return;
		}
		//handleMsg(_id, idx, _peer->session()->id(), _r[0]);
		m_msg_queue.push(PBFTMsgPacket(idx, _peer->session()->id(), _id, _r[0].data()));
	} else {
		LOG(ERROR) << "Recv an illegal msg, id=" << _id;
	}
}

void PBFT::workLoop() {
	while (isWorking()) {
		try
		{
			std::pair<bool, PBFTMsgPacket> ret = m_msg_queue.tryPop(5);
			if (ret.first) {
				handleMsg(ret.second.packet_id, ret.second.node_idx, ret.second.node_id, RLP(ret.second.data));
			} else {
				std::unique_lock<std::mutex> l(x_signalled);
				m_signalled.wait_for(l, chrono::milliseconds(5));
			}

			checkTimeout();
			handleFutureBlock();
			collectGarbage();
		} catch (Exception &_e) {
			LOG(ERROR) << _e.what();
		}
	}
}

void PBFT::handleMsg(unsigned _id, u256 const& _from, h512 const& _node, RLP const& _r) {
	Guard l(m_mutex);

	auto now_time = utcTime();
	std::string key;
	PBFTMsg pbft_msg;
	switch (_id) {
	case PrepareReqPacket: {
		PrepareReq req;
		req.populate(_r);
		handlePrepareMsg(_from, req);
		key = req.block_hash.hex();
		pbft_msg = req;
		break;
	}
	case SignReqPacket:	{
		SignReq req;
		req.populate(_r);
		handleSignMsg(_from, req);
		key = req.sig.hex();
		pbft_msg = req;
		break;
	}
	case CommitReqPacket: {
		CommitReq req;
		req.populate(_r);
		handleCommitMsg(_from, req);
		key = req.sig.hex();
		pbft_msg = req;
		break;
	}
	case ViewChangeReqPacket: {
		ViewChangeReq req;
		req.populate(_r);
		handleViewChangeMsg(_from, req);
		key = req.sig.hex() + toJS(req.view);
		pbft_msg = req;
		break;
	}
	default: {
		LOG(ERROR) << "Recv error msg, id=" << _id;
		return;
	}
	}

	bool time_flag = (pbft_msg.timestamp >= now_time) || (now_time - pbft_msg.timestamp < m_view_timeout);
	bool height_flag = (pbft_msg.height > m_highest_block.number()) || (m_highest_block.number() - pbft_msg.height < 10);
	//LOG(TRACE) << "key=" << key << ",time_flag=" << time_flag << ",height_flag=" << height_flag;
	if (key.size() > 0 && time_flag && height_flag) {
		std::unordered_set<h512> filter;
		filter.insert(_node);
		h512 gen_node_id = h512(0);
		if (NodeConnManagerSingleton::GetInstance().getPublicKey(pbft_msg.idx, gen_node_id)) {
			filter.insert(gen_node_id);
		}
		broadcastMsg(key, _id, _r.toBytes(), filter);
	}
}

void PBFT::changeViewForEmptyBlockWithoutLock(u256 const& _from) {
	LOG(INFO) << "changeViewForEmptyBlockWithoutLock m_to_view=" << m_to_view << ", from=" << _from << ", node=" << m_node_idx;
	m_last_consensus_time = 0;
	m_last_sign_time = 0;
	m_change_cycle = 0;
	m_empty_block_flag = true;
	m_signalled.notify_all();
}

void PBFT::changeViewForEmptyBlockWithLock() {
	Guard l(m_mutex);
	LOG(INFO) << "changeViewForEmptyBlockWithLock m_to_view=" << m_to_view << ", node=" << m_node_idx;
	m_last_consensus_time = 0;
	m_last_sign_time = 0;
	m_change_cycle = 0;
	m_empty_block_flag = true;
	m_leader_failed = true; // 在checkTimeout的时候会设置，但是这里加上的目的是为了让出空块者不会立即再出空块
	m_signalled.notify_all();
}

void PBFT::checkTimeout() {
	Timer t;
	bool flag = false;
	{
		Guard l(m_mutex);

		auto now_time = utcTime();
		auto last_time = std::max(m_last_consensus_time, m_last_sign_time);
		auto interval = (uint64_t)(m_view_timeout * std::pow(1.5, m_change_cycle));
		if (now_time - last_time >= interval) {
			m_leader_failed = true;
			m_to_view += 1;
			m_change_cycle = std::min(m_change_cycle + 1, (unsigned)kMaxChangeCycle); // 防止溢出
			m_last_consensus_time = now_time;
			flag = true;
			// 曾经收到的viewchange消息中跟我当前要的不一致就清空
			for (auto iter = m_recv_view_change_req[m_to_view].begin(); iter != m_recv_view_change_req[m_to_view].end();) {
				if (iter->second.height < m_highest_block.number()) {
					iter = m_recv_view_change_req[m_to_view].erase(iter);
				} else if (iter->second.height == m_highest_block.number() && iter->second.block_hash !=  m_highest_block.hash(WithoutSeal)) {  // 防作恶
					iter = m_recv_view_change_req[m_to_view].erase(iter);
				} else {
					++iter;
				}
			}

			// start viewchange log
			if (m_view + 1 == m_to_view) 
			{
				PBFTFlowViewChangeLog(m_highest_block.number() + m_view, " view:" + m_view.convert_to<string>());
			}
			else 
			{
				STAT_ERROR_MSG_LOGGUARD(STAT_PBFT_VIEWCHANGE_TAG) << "Timeout and ViewChanged!" 
					<< " m_view=" << m_view << ", m_to_view=" << m_to_view << ", m_change_cycle=" << m_change_cycle;
			}

			if (!broadcastViewChangeReq()) {
				LOG(ERROR) << "broadcastViewChangeReq failed";
				return;
			}
			checkAndChangeView();
			LOG(DEBUG) << "checkTimeout timecost=" << t.elapsed() << ", m_view=" << m_view << ",m_to_view=" << m_to_view;
		}
	}

	if (flag && m_onViewChange) {
		m_onViewChange();
	}
}

void PBFT::handleFutureBlock() {
	Guard l(m_mutex);

	if (m_future_prepare_cache.second.height == m_consensus_block_number && m_future_prepare_cache.second.view == m_view) {
		LOG(INFO) << "handleFurtureBlock, blk=" << m_future_prepare_cache.second.height;
		handlePrepareMsg(m_future_prepare_cache.first, m_future_prepare_cache.second);
		m_future_prepare_cache = std::make_pair(Invalid256, PrepareReq());
	}
}

void PBFT::recvFutureBlock(u256 const& _from, PrepareReq const& _req) {
	if (m_future_prepare_cache.second.block_hash != _req.block_hash) {
		m_future_prepare_cache = std::make_pair(_from, _req);
		LOG(INFO) << "recvFutureBlock, blk=" << _req.height << ",hash=" << _req.block_hash << ",idx=" << _req.idx;
	}
}

Signature PBFT::signHash(h256 const & _hash) const {
	return dev::sign(m_key_pair.sec(), _hash);
}

bool PBFT::checkSign(u256 const & _idx, h256 const & _hash, Signature const & _sig) const {
	Public pub_id;
	if (!NodeConnManagerSingleton::GetInstance().getPublicKey(_idx, pub_id)) {
		LOG(ERROR) << "Can't find node, idx=" << _idx;
		return false;
	}
	return dev::verify(pub_id, _sig, _hash);
}

bool PBFT::checkSign(PBFTMsg const& _req) const {
	Public pub_id;
	if (!NodeConnManagerSingleton::GetInstance().getPublicKey(_req.idx, pub_id)) {
		LOG(ERROR) << "Can't find node, idx=" << _req.idx;
		return false;
	}
	return dev::verify(pub_id, _req.sig, _req.block_hash) && dev::verify(pub_id, _req.sig2, _req.fieldsWithoutBlock());
}

bool PBFT::broadcastViewChangeReq() {
	LOG(INFO) << "Ready to broadcastViewChangeReq, blk=" << m_highest_block.number() << ",view=" << m_view << ",to_view=" << m_to_view << ",m_change_cycle=" << m_change_cycle;

	if (m_account_type != EN_ACCOUNT_TYPE_MINER) {
		LOG(INFO) << "broadcastViewChangeReq give up for not miner";
		return true;
	}

	ViewChangeReq req;
	req.height = m_highest_block.number();
	req.view = m_to_view;
	req.idx = m_node_idx;
	req.timestamp = u256(utcTime());
	req.block_hash = m_highest_block.hash(WithoutSeal);
	req.sig = signHash(req.block_hash);
	req.sig2 = signHash(req.fieldsWithoutBlock());

	if (!m_empty_block_flag) {
		LOGCOMWARNING << WarningMap.at(ChangeViewWarning) << "|blockNumber:" << req.height << " ChangeView:" << req.view;
		m_empty_block_flag = false;
	}
	RLPStream ts;
	req.streamRLPFields(ts);
	bool ret = broadcastMsg(req.sig.hex() + toJS(req.view), ViewChangeReqPacket, ts.out());
	return ret;
}

bool PBFT::broadcastSignReq(PrepareReq const & _req) {
	SignReq sign_req;
	sign_req.height = _req.height;
	sign_req.view = _req.view;
	sign_req.idx = m_node_idx;
	sign_req.timestamp = u256(utcTime());
	sign_req.block_hash = _req.block_hash;
	sign_req.sig = signHash(sign_req.block_hash);
	sign_req.sig2 = signHash(sign_req.fieldsWithoutBlock());

	RLPStream ts;
	sign_req.streamRLPFields(ts);
	if (broadcastMsg(sign_req.sig.hex(), SignReqPacket, ts.out())) {
		addSignReq(sign_req);
		return true;
	}
	return false;
}

bool PBFT::broadcastCommitReq(PrepareReq const & _req) {
	CommitReq commit_req;
	commit_req.height = _req.height;
	commit_req.view = _req.view;
	commit_req.idx = m_node_idx;
	commit_req.timestamp = u256(utcTime());
	commit_req.block_hash = _req.block_hash;
	commit_req.sig = signHash(commit_req.block_hash);
	commit_req.sig2 = signHash(commit_req.fieldsWithoutBlock());

	RLPStream ts;
	commit_req.streamRLPFields(ts);
	if (broadcastMsg(commit_req.sig.hex(), CommitReqPacket, ts.out())) {
		addCommitReq(commit_req);
		return true;
	}
	return false;
}

bool PBFT::broadcastPrepareReq(BlockHeader const & _bi, bytes const & _block_data) {
	PrepareReq req;
	req.height = _bi.number();
	req.view = m_view;
	req.idx = m_node_idx;
	req.timestamp = u256(utcTime());
	req.block_hash = _bi.hash(WithoutSeal);
	req.sig = signHash(req.block_hash);
	req.sig2 = signHash(req.fieldsWithoutBlock());
	req.block = _block_data;

	RLPStream ts;
	req.streamRLPFields(ts);
	if (broadcastMsg(req.block_hash.hex(), PrepareReqPacket, ts.out())) {
		addRawPrepare(req);
		return true;
	}
	return false;
}

bool PBFT::broadcastMsg(std::string const & _key, unsigned _id, bytes const & _data, std::unordered_set<h512> const & _filter) {

	if (auto h = m_host.lock()) {
		h->foreachPeer([&](shared_ptr<PBFTPeer> _p)
		{
			unsigned account_type = 0;
			if (!NodeConnManagerSingleton::GetInstance().getAccountType(_p->session()->id(), account_type)) {
				LOG(ERROR) << "Cannot get account type for peer" << _p->session()->id();
				return true;
			}
			if (_id != ViewChangeReqPacket && account_type != EN_ACCOUNT_TYPE_MINER && !m_bc->chainParams().broadcastToNormalNode) {
				return true;
			}

			if (_filter.count(_p->session()->id())) {  // 转发广播
				this->broadcastMark(_key, _id, _p);
				return true;
			}
			if (this->broadcastFilter(_key, _id, _p)) {
				return true;
			}

			RLPStream ts;
			_p->prep(ts, _id, 1).append(_data);
			_p->sealAndSend(ts);
			this->broadcastMark(_key, _id, _p);
			return true;
		});
		return true;
	}
	return false;
}

bool PBFT::broadcastFilter(std::string const & _key, unsigned _id, shared_ptr<PBFTPeer> _p) {
	if (_id == PrepareReqPacket) {
		DEV_GUARDED(_p->x_knownPrepare)
		return _p->m_knownPrepare.exist(_key);
	} else if (_id == SignReqPacket) {
		DEV_GUARDED(_p->x_knownSign)
		return _p->m_knownSign.exist(_key);
	} else if (_id == ViewChangeReqPacket) {
		DEV_GUARDED(_p->x_knownViewChange)
		return _p->m_knownViewChange.exist(_key);
	} else if (_id == CommitReqPacket) {
		DEV_GUARDED(_p->x_knownCommit)
		return _p->m_knownCommit.exist(_key);
	} else {
		return true;
	}
	return true;
}

void PBFT::broadcastMark(std::string const & _key, unsigned _id, shared_ptr<PBFTPeer> _p) {
	if (_id == PrepareReqPacket) {
		DEV_GUARDED(_p->x_knownPrepare)
		{
			if (_p->m_knownPrepare.size() > kKnownPrepare) {
				_p->m_knownPrepare.pop();
			}
			_p->m_knownPrepare.push(_key);
		}
	} else if (_id == SignReqPacket) {
		DEV_GUARDED(_p->x_knownSign)
		{
			if (_p->m_knownSign.size() > kKnownSign) {
				_p->m_knownSign.pop();
			}
			_p->m_knownSign.push(_key);
		}
	} else if (_id == ViewChangeReqPacket) {
		DEV_GUARDED(_p->x_knownViewChange)
		{
			if (_p->m_knownViewChange.size() > kKnownViewChange) {
				_p->m_knownViewChange.pop();
			}
			_p->m_knownViewChange.push(_key);
		}
	} else if (_id == CommitReqPacket) {
		DEV_GUARDED(_p->x_knownCommit)
		{
			if (_p->m_knownCommit.size() > kKnownCommit) {
				_p->m_knownCommit.pop();
			}
			_p->m_knownCommit.push(_key);
		}
	} else {
		// do nothing
	}
}

void PBFT::clearMask() {
	if (auto h = m_host.lock()) {
		h->foreachPeer([&](shared_ptr<PBFTPeer> _p)
		{
			DEV_GUARDED(_p->x_knownPrepare)
			_p->m_knownPrepare.clear();
			DEV_GUARDED(_p->x_knownSign)
			_p->m_knownSign.clear();
			DEV_GUARDED(_p->x_knownCommit)
			_p->m_knownCommit.clear();
			DEV_GUARDED(_p->x_knownViewChange)
			_p->m_knownViewChange.clear();
			return true;
		});
	}
}

bool PBFT::isExistPrepare(PrepareReq const & _req) {
	return m_raw_prepare_cache.block_hash == _req.block_hash;
}

bool PBFT::isExistSign(SignReq const & _req) {
	auto iter = m_sign_cache.find(_req.block_hash);
	if (iter == m_sign_cache.end()) {
		return false;
	}
	return iter->second.find(_req.sig.hex()) != iter->second.end();
}

bool PBFT::isExistCommit(CommitReq const & _req) {
	auto iter = m_commit_cache.find(_req.block_hash);
	if (iter == m_commit_cache.end()) {
		return false;
	}
	return iter->second.find(_req.sig.hex()) != iter->second.end();
}

bool PBFT::isExistViewChange(ViewChangeReq const & _req) {
	auto iter = m_recv_view_change_req.find(_req.view);
	if (iter == m_recv_view_change_req.end()) {
		return false;
	}
	return iter->second.find(_req.idx) != iter->second.end();
}

void PBFT::handlePrepareMsg(u256 const & _from, PrepareReq const & _req, bool _self) {
	Timer t;
	ostringstream oss;
	oss << "handlePrepareMsg: idx=" << _req.idx << ",view=" << _req.view << ",blk=" << _req.height << ",hash=" << _req.block_hash.abridged() << ",from=" << _from;
	VLOG(10) << oss.str() << ", net-time=" << u256(utcTime()) - _req.timestamp;

	if (isExistPrepare(_req)) {
		VLOG(10) << oss.str()  << "Discard an illegal prepare, duplicated";
		return;
	}

	if (!_self && _req.idx == m_node_idx) {
		LOG(ERROR) << oss.str() << "Discard an illegal prepare, your own req";
		return;
	}

	if (_req.height < m_consensus_block_number || _req.view < m_view) {
		VLOG(10) << oss.str() << "Discard an illegal prepare, lower than your needed blk";
		return;
	}

	if (_req.height > m_consensus_block_number || _req.view > m_view) {
		LOG(INFO) << oss.str() << "Recv a future block, wait to be handled later";
		recvFutureBlock(_from, _req);
		return;
	}

	addRawPrepare(_req); // 必须在recvFutureBlock之后

	auto leader = getLeader();
	if (!leader.first || _req.idx != leader.second) {
		LOG(ERROR) << oss.str()  << "Recv an illegal prepare, err leader";
		return;
	}

	if (_req.height == m_committed_prepare_cache.height && _req.block_hash != m_committed_prepare_cache.block_hash) {
		LOG(INFO) << oss.str() << "Discard an illegal prepare req, commited but not saved hash=" << m_committed_prepare_cache.block_hash.abridged();
		return;
	}

	if (!checkSign(_req)) {
		LOG(ERROR) << oss.str()  << "CheckSign failed";
		return;
	}

	LOG(TRACE) << "start exec tx, blk=" << _req.height << ",hash=" << _req.block_hash << ",idx=" << _req.idx << ", time=" << utcTime();
	Block outBlock(*m_bc, *m_stateDB);
	try {
		m_bc->checkBlockValid(_req.block_hash, _req.block, outBlock);
		if (outBlock.info().hash(WithoutSeal) != _req.block_hash) {  // 检验块数据是否被更改
			LOG(ERROR) << oss.str() << ", block_hash is not equal to block";
			return;
		}
		m_last_exec_finish_time = utcTime();
	}
	catch (Exception &ex) {
		LOG(ERROR) << oss.str()  << "CheckBlockValid failed" << ex.what();
		return;
	}

	// 空块切换
	if (outBlock.pending().size() == 0 && m_omit_empty_block) {
		changeViewForEmptyBlockWithoutLock(_from);
		// for empty block
		stringstream ss;
		ss << "#empty blk hash:" << _req.block_hash.abridged() << " height:" << _req.height;
		PBFTFlowLog(m_highest_block.number() + m_view, ss.str(), 1);
		return;
	}

	// 重新生成block数据
	outBlock.commitToSeal(*m_bc, outBlock.info().extraData());
	m_bc->addBlockCache(outBlock, outBlock.info().difficulty());

	RLPStream ts;
	outBlock.info().streamRLP(ts, WithoutSeal);
	if (!outBlock.sealBlock(ts.out())) {
		LOG(ERROR) << oss.str() << "Error: sealBlock failed 3";
		return;
	}

	LOG(DEBUG) << "finish exec tx, blk=" << _req.height << ", time=" << utcTime();
	// execed log
	stringstream ss;
	ss << "hash:" << _req.block_hash.abridged() << " realhash:" << outBlock.info().hash(WithoutSeal).abridged() 
		<< " height:" << _req.height << " txnum:" << outBlock.pending().size();
	PBFTFlowLog(m_highest_block.number() + m_view, ss.str());

	// 重新生成Prepare
	PrepareReq req;
	req.height = _req.height;
	req.view = _req.view;
	req.idx = _req.idx;
	req.timestamp = u256(utcTime());
	req.block_hash = outBlock.info().hash(WithoutSeal);
	req.sig = signHash(req.block_hash);
	req.sig2 = signHash(req.fieldsWithoutBlock());
	req.block = outBlock.blockData();

	if (!addPrepareReq(req)) {
		LOG(ERROR) << oss.str()  << "addPrepare failed";
		return;
	}

	if (m_account_type == EN_ACCOUNT_TYPE_MINER && !broadcastSignReq(req)) {
		LOG(ERROR) << oss.str()  << "broadcastSignReq failed";
		//return;
	}

	LOG(INFO) << oss.str() << ",real_block_hash=" << outBlock.info().hash(WithoutSeal).abridged() << " success";

	checkAndCommit();

	LOG(DEBUG) << "handlePrepareMsg, timecost=" << 1000 * t.elapsed();
	return;
}

void PBFT::handleSignMsg(u256 const & _from, SignReq const & _req) {
	Timer t;
	ostringstream oss;
	oss << "handleSignMsg: idx=" << _req.idx << ",view=" << _req.view << ",blk=" << _req.height << ",hash=" <<  _req.block_hash.abridged() << ", from=" << _from;
	VLOG(10) << oss.str() << ", net-time=" << u256(utcTime()) - _req.timestamp;

	if (isExistSign(_req)) {
		VLOG(10) << oss.str() << "Discard an illegal sign, duplicated";
		return;
	}

	if (_req.idx == m_node_idx) {
		LOG(ERROR) << oss.str() << "Discard an illegal sign, your own req";
		return;
	}

	if (m_prepare_cache.block_hash != _req.block_hash) {
		VLOG(10) << oss.str()  << "Recv a sign_req for block which not in prepareCache, preq=" << m_prepare_cache.block_hash.abridged();
		bool future_msg = _req.height >= m_consensus_block_number || _req.view > m_view;
		if (future_msg && checkSign(_req)) {
			addSignReq(_req);
			LOG(INFO) << oss.str()  << "Cache this sign_req";
		}
		return;
	}

	if (m_prepare_cache.view != _req.view) {
		LOG(INFO) << oss.str() << "Discard a sign_req which view is not equal, preq.v=" << m_prepare_cache.view;
		return;
	}

	if (!checkSign(_req)) {
		LOG(ERROR) << oss.str()  << "CheckSign failed";
		return;
	}

	LOG(INFO) << oss.str() << ", success";

	addSignReq(_req);

	checkAndCommit();

	LOG(DEBUG) << "handleSignMsg, timecost=" << 1000 * t.elapsed();
	return;
}

void PBFT::handleCommitMsg(u256 const &_from, CommitReq const &_req) {
	Timer t;
	ostringstream oss;
	oss << "handleCommitMsg: idx=" << _req.idx << ",view=" << _req.view << ",blk=" << _req.height << ",hash=" <<  _req.block_hash.abridged() << ", from=" << _from;
	VLOG(10) << oss.str() << ", net-time=" << u256(utcTime()) - _req.timestamp;

	if (isExistCommit(_req)) {
		VLOG(10) << oss.str() << " Discard an illegal commit, duplicated";
		return;
	}

	if (_req.idx == m_node_idx) {
		LOG(ERROR) << oss.str() << " Discard an illegal commit, your own req";
		return;
	}

	if (m_prepare_cache.block_hash != _req.block_hash) {
		VLOG(10) << oss.str()  << "Recv a commit_req for block which not in prepareCache, preq=" << m_prepare_cache.block_hash.abridged();
		bool future_msg = _req.height >= m_consensus_block_number || _req.view > m_view;
		if (future_msg && checkSign(_req)) {
			addCommitReq(_req);
			LOG(INFO) << oss.str()  << "Cache this commit_req";
		}
		return;
	}

	if (m_prepare_cache.view != _req.view) {
		LOG(INFO) << oss.str() << " Discard an illegal commit, view is not equal prepare " << m_prepare_cache.view;
		return;
	}

	if (!checkSign(_req)) {
		LOG(ERROR) << oss.str()  << "CheckSign failed";
		return;
	}

	LOG(INFO) << oss.str() << ", success";

	addCommitReq(_req);

	checkAndSave();

	LOG(DEBUG) << "handleCommitMsg, timecost=" << 1000 * t.elapsed();
	return;
}

void PBFT::handleViewChangeMsg(u256 const & _from, ViewChangeReq const & _req) {
	Timer t;
	ostringstream oss;
	oss << "handleViewChangeMsg: idx=" << _req.idx << ",view=" << _req.view  << ",blk=" << _req.height << ",hash=" << _req.block_hash.abridged() << ",from=" << _from;
	VLOG(10) << oss.str() << ", net-time=" << u256(utcTime()) - _req.timestamp;

	if (isExistViewChange(_req)) {
		VLOG(10) << oss.str() << "Discard an illegal viewchange, duplicated";
		return;
	}

	if (_req.idx == m_node_idx) {
		LOG(ERROR) << oss.str() << "Discard an illegal viewchange, your own req";
		return;
	}

	// +1 是为了防止触碰到刚好view正在切换的边界条件。因为view落后的节点的view必定是低于(>2)其他整成节点的view
	if (_req.view + 1 < m_to_view) {
		LOG(INFO) << oss.str() << " send response to node=" << _from << " for motivating viewchange";
		broadcastViewChangeReq();
	}

	if (_req.height < m_highest_block.number() || _req.view <= m_view) {
		VLOG(10) << oss.str() << "Discard an illegal viewchange, m_highest_block=" << m_highest_block.number() << ",m_view=" << m_view;
		return;
	}

	if (_req.height == m_highest_block.number() && _req.block_hash != m_highest_block.hash(WithoutSeal) && m_bc->block(_req.block_hash).size() == 0) {
		LOG(INFO) << oss.str() << "Discard an illegal viewchange, same height but not hash, chain has been forked, my=" << m_highest_block.hash(WithoutSeal) << ",req=" << _req.block_hash;
		return;
	}

	if (!checkSign(_req)) {
		LOG(ERROR) << oss.str() << "CheckSign failed";
		return;
	}

	LOG(INFO) << oss.str() << ", success";

	m_recv_view_change_req[_req.view][_req.idx] = _req;

	if (_req.view == m_to_view) {
		checkAndChangeView();
	} else  {
		u256 count = u256(0);
		u256 min_view = Invalid256;
		u256 min_height = Invalid256;

		/*for (auto iter = m_recv_view_change_req.begin(); iter != m_recv_view_change_req.end(); ++iter) {
			if (iter->first > m_to_view) {
				count += iter->second.size();
				if (min_view > iter->first) {
					min_view = iter->first;
				}
			}
		}*/

		std::map<u256, u256> idx_view_map;
		for (auto it = m_recv_view_change_req.begin(); it != m_recv_view_change_req.end(); ++it) {
			if (it->first > m_to_view) {
				for (auto it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
					auto found = idx_view_map.find(it2->first) != idx_view_map.end();
					if (it2->second.height >= m_highest_block.number()
					        && (!found || it->first > idx_view_map[it2->first])) {
						idx_view_map[it2->first] = it->first;
						if (min_view > it->first) {
							min_view = it->first;
						}
						if (min_height > it2->second.height) {
							min_height = it2->second.height;
						}
					}
				}
			}
		}

		count = idx_view_map.size();

		// 当前正在共识的块，如果还未落盘。在下一个出块节点确定宕机，其他节点先发现然后发出视图切换，此时本节点暂不立即切换
		// 待落地块后发现下一个共识节点宕机，自动发出切换。——目的为了避免过早发出切换视图包被其他节点丢弃的现象（其他节点块高比本节点高）
		bool flag = (min_height == m_consensus_block_number && min_height == m_committed_prepare_cache.height);
		if (count > m_f && !flag) {
			LOG(INFO) << "Fast start viewchange, m_to_view=" << m_to_view << ",req.view=" << _req.view << ",min_view=" << min_view;
			m_last_consensus_time = 0;
			m_last_sign_time = 0;
			m_to_view = min_view - 1; // it will be setted equal to min_view when viewchange happened.
			m_signalled.notify_all();
		}
	}

	LOG(DEBUG) << "handleViewChangeMsg, timecost=" << 1000 * t.elapsed();
	return;
}

void PBFT::checkAndSave() {
	u256 have_sign = m_sign_cache[m_prepare_cache.block_hash].size();
	u256 have_commit = m_commit_cache[m_prepare_cache.block_hash].size();
	if (have_sign >= quorum() && have_commit == quorum()) {
		LOG(INFO) << "######### Reach enough commit for block="  << m_prepare_cache.height << ",hash=" << m_prepare_cache.block_hash.abridged() << ",have_sign=" << have_sign << ",have_commit=" << have_commit << ",quorum=" << quorum();

		if (m_prepare_cache.view != m_view) {
			LOG(INFO) << "view has changed, discard this block, preq.view=" << m_prepare_cache.view << ",m_view=" << m_view;
			return;
		}

		if (m_prepare_cache.height > m_highest_block.number()) {
			// 把签名加上
			std::vector<std::pair<u256, Signature>> sig_list;
			sig_list.reserve(static_cast<unsigned>(quorum()));
			for (auto item : m_commit_cache[m_prepare_cache.block_hash]) {
				sig_list.push_back(std::make_pair(item.second.idx, Signature(item.first.c_str())));
			}
			RLP r(m_prepare_cache.block);
			RLPStream rs;
			rs.appendList(5);
			rs.appendRaw(r[0].data()); // header
			rs.appendRaw(r[1].data()); // tx
			rs.appendRaw(r[2].data()); // uncles
			rs.appendRaw(r[3].data()); // hash
			rs.appendVector(sig_list); // sign_list

			LOG(INFO) << "BLOCK_TIMESTAMP_STAT:[" << toString(m_prepare_cache.block_hash) << "][" <<  m_prepare_cache.height << "][" << utcTime() << "][" << "onSealGenerated" << "]" << ",idx=" << m_prepare_cache.idx;
			m_onSealGenerated(rs.out(), m_prepare_cache.idx == m_node_idx);
		} else {
			LOG(INFO) << "Discard this block, blk_no=" << m_prepare_cache.height << ",highest_block=" << m_highest_block.number();
		}
		// reach commit log
		PBFTFlowLog(m_highest_block.number() + m_view, " ");
	}
}

void PBFT::checkAndCommit() {
	u256 have_sign = m_sign_cache[m_prepare_cache.block_hash].size();
	if (have_sign == quorum()) { // 只发一次
		LOG(INFO) << "######### Reach enough sign for block=" << m_prepare_cache.height << ",hash=" << m_prepare_cache.block_hash.abridged() << ",have_sign=" << have_sign << ",need_sign=" << quorum();

		if (m_prepare_cache.view != m_view) {
			LOG(INFO) << "view has changed, discard this block, preq.view=" << m_prepare_cache.view << ",m_view=" << m_view;
			return;
		}

		m_committed_prepare_cache = m_raw_prepare_cache;
		backupMsg(backup_key_committed, m_committed_prepare_cache);

		if (m_account_type == EN_ACCOUNT_TYPE_MINER && !broadcastCommitReq(m_prepare_cache)) {
			LOG(ERROR) << "broadcastCommitReq failed";
		}

		// 重置倒计时，给出足够的时间收集签名
		m_last_sign_time = utcTime();

		// reach sign log
		PBFTFlowLog(m_highest_block.number() + m_view, " ");
		checkAndSave();
	}
}

void PBFT::checkAndChangeView() {
	u256 count = m_recv_view_change_req[m_to_view].size();
	if (count >= quorum() - 1) {
		LOG(INFO) << "######### Reach consensus, to_view=" << m_to_view;
		// changeview finish 要在 m_view 赋值之前 destory state
		PBFTFlowLog(m_highest_block.number() + m_view, "new_view:" + m_to_view.convert_to<string>() + " m_change_cycle:" + std::to_string(m_change_cycle));	

		m_leader_failed = false;
		m_view = m_to_view;

		m_raw_prepare_cache.clear();
		m_prepare_cache.clear();
		m_sign_cache.clear();
		m_commit_cache.clear();

		for (auto iter = m_recv_view_change_req.begin(); iter != m_recv_view_change_req.end();) {
			if (iter->first <= m_view) {
				iter = m_recv_view_change_req.erase(iter);
			} else {
				++iter;
			}
		}
		

		clearMask();
		// start new block log
		PBFTFlowLog(m_highest_block.number() + m_view, "from viewchange", (int)isLeader(), true);
	}
}

bool PBFT::addRawPrepare(PrepareReq const& _req) {
	m_raw_prepare_cache = _req;
	return true;
}

bool PBFT::addPrepareReq(PrepareReq const & _req) {
	m_prepare_cache = _req;

	auto sign_iter = m_sign_cache.find(m_prepare_cache.block_hash);
	if (sign_iter != m_sign_cache.end()) {
		for (auto iter2 = sign_iter->second.begin(); iter2 != sign_iter->second.end();) {
			if (iter2->second.view != m_prepare_cache.view) {
				iter2 = sign_iter->second.erase(iter2);
			} else {
				++iter2;
			}
		}
	}

	auto commit_iter = m_commit_cache.find(m_prepare_cache.block_hash);
	if (commit_iter != m_commit_cache.end()) {
		for (auto iter2 = commit_iter->second.begin(); iter2 != commit_iter->second.end();) {
			if (iter2->second.view != m_prepare_cache.view) {
				iter2 = commit_iter->second.erase(iter2);
			} else {
				++iter2;
			}
		}
	}

	return true;
}

void PBFT::addSignReq(SignReq const & _req) {
	m_sign_cache[_req.block_hash][_req.sig.hex()] = _req;
}

void PBFT::addCommitReq(CommitReq const & _req) {
	m_commit_cache[_req.block_hash][_req.sig.hex()] = _req;
}

void PBFT::delCache(h256 const& _hash) {
	auto iter = m_sign_cache.find(_hash);
	if (iter == m_sign_cache.end()) {
		LOG(DEBUG) << "Try to delete not-exist, hash=" << _hash;
		//BOOST_THROW_EXCEPTION(UnexpectError());
	} else {
		m_sign_cache.erase(iter);
	}

	auto iter2 = m_commit_cache.find(_hash);
	if (iter2 == m_commit_cache.end()) {
		LOG(DEBUG) << "Try to delete not-exist, hash=" << _hash;
		//BOOST_THROW_EXCEPTION(UnexpectError());
	} else {
		m_commit_cache.erase(iter2);
	}

	if (_hash == m_prepare_cache.block_hash) {
		m_prepare_cache.clear();
	}
}

void PBFT::delViewChange() {
	for (auto it = m_recv_view_change_req.begin(); it != m_recv_view_change_req.end(); ) {
		for (auto it2 = it->second.begin(); it2 != it->second.end();) {
			if (it2->second.height < m_highest_block.number()) {
				it2 = it->second.erase(it2);
			} else if (it2->second.height == m_highest_block.number() && it2->second.block_hash !=  m_highest_block.hash(WithoutSeal)) { // 防作恶
				it2 = it->second.erase(it2);
			} else {
				++it2;
			}
		}

		if (it->second.size() == 0) {
			it = m_recv_view_change_req.erase(it);
		} else {
			++it;
		}
	}
}

void PBFT::collectGarbage() {
	Timer t;
	Guard l(m_mutex);
	if (!m_highest_block) return;

	std::chrono::system_clock::time_point now_time = std::chrono::system_clock::now();
	if (now_time - m_last_collect_time >= std::chrono::seconds(kCollectInterval)) {
		for (auto iter = m_sign_cache.begin(); iter != m_sign_cache.end();) {
			for (auto iter2 = iter->second.begin(); iter2 != iter->second.end();) {
				if (iter2->second.height < m_highest_block.number()) {
					iter2 = iter->second.erase(iter2);
				} else {
					++iter2;
				}
			}
			if (iter->second.size() == 0) {
				iter = m_sign_cache.erase(iter);
			} else {
				++iter;
			}
		}

		for (auto iter = m_commit_cache.begin(); iter != m_commit_cache.end();) {
			for (auto iter2 = iter->second.begin(); iter2 != iter->second.end();) {
				if (iter2->second.height < m_highest_block.number()) {
					iter2 = iter->second.erase(iter2);
				} else {
					++iter2;
				}
			}
			if (iter->second.size() == 0) {
				iter = m_commit_cache.erase(iter);
			} else {
				++iter;
			}
		}

		m_last_collect_time = now_time;

		LOG(DEBUG) << "collectGarbage timecost(ms)=" << 1000 * t.elapsed();
	}
}


bool PBFT::getMinerList(int _blk_no, h512s &_miner_list) const {
	std::map<std::string, NodeConnParams> all_node;
	NodeConnManagerSingleton::GetInstance().getAllNodeConnInfo(_blk_no, all_node);

	unsigned miner_num = 0;
	for (auto iter = all_node.begin(); iter != all_node.end(); ++iter) {
		if (iter->second._iIdentityType == EN_ACCOUNT_TYPE_MINER) {
			++miner_num;
		}
	}
	_miner_list.resize(miner_num);
	for (auto iter = all_node.begin(); iter != all_node.end(); ++iter) {
		if (iter->second._iIdentityType == EN_ACCOUNT_TYPE_MINER) {
			auto idx = static_cast<unsigned>(iter->second._iIdx);
			if (idx >= miner_num) {
				LOG(ERROR) << "getMinerList return false cause for idx=" << idx << ",miner_num=" << miner_num;
				return false;
			}
			_miner_list[idx] = jsToPublic(toJS(iter->second._sNodeId));
		}
	}

	return true;

}

bool PBFT::checkBlockSign(BlockHeader const& _header, std::vector<std::pair<u256, Signature>> _sign_list) {
	Timer t;

	LOG(TRACE) << "PBFT::checkBlockSign " << _header.number();


	h512s miner_list;
	if (!getMinerList(static_cast<int>(_header.number() - 1), miner_list)) {
		LOG(ERROR) << "checkBlockSign failed for getMinerList return false, blk=" <<  _header.number() - 1;
		return false;
	}

	LOG(DEBUG) << "checkBlockSign call getAllNodeConnInfo: blk=" << _header.number() - 1 << ", miner_num=" << miner_list.size();

	// 检查公钥列表
	if (_header.nodeList() != miner_list) {
		ostringstream oss;
		for (size_t i = 0; i < miner_list.size(); ++i) {
			oss << miner_list[i] << ",";
		}
		LOG(ERROR) << "checkBlockSign failed, chain_block=" << _header.number() << ",miner_list size=" << miner_list.size() << ",value=" << oss.str();
		oss.clear();
		for (size_t i = 0; i < _header.nodeList().size(); ++i) {
			oss << _header.nodeList()[i] << ",";
		}
		LOG(ERROR) << "checkBlockSign failed, down_block=" << _header.number() << ",miner_list size=" << _header.nodeList().size() << ",value=" << oss.str();
		return false;
	}

	// 检查签名数量
	if (_sign_list.size() < (miner_list.size() - (miner_list.size() - 1) / 3)) {
		LOG(ERROR) << "checkBlockSign failed, blk=" << _header.number() << " not enough sign, sign_num=" << _sign_list.size() << ",miner_num" << miner_list.size();
		return false;
	}

	// 检查签名是否有效
	for (auto item : _sign_list) {
		if (item.first >= miner_list.size()) {
			LOG(ERROR) << "checkBlockSign failed, block=" << _header.number() << "sig idx=" << item.first << ", out of bound, miner_list size=" << miner_list.size();
			return false;
		}

		if (!dev::verify(miner_list[static_cast<int>(item.first)], item.second, _header.hash(WithoutSeal))) {
			LOG(ERROR) << "checkBlockSign failed, verify false, blk=" << _header.number() << ",hash=" << _header.hash(WithoutSeal);
			return false;
		}
	}

	LOG(DEBUG) << "checkBlockSign success, blk=" << _header.number() << ",hash=" << _header.hash(WithoutSeal) << ",timecost=" << t.elapsed() / 1000 << "ms";

	return true;
}

void PBFT::backupMsg(std::string const& _key, PBFTMsg const& _msg) {
	if (!m_backup_db) {
		return;
	}

	RLPStream ts;
	_msg.streamRLPFields(ts);
	RLPStream ts2;
	ts2.appendList(1).append(ts.out());
	bytes rlp;
	ts2.swapOut(rlp);

	auto ret = m_backup_db->Put(m_writeOptions, ldb::Slice(_key), ldb::Slice((char*)rlp.data(), rlp.size()));
	if (!ret.ok()) {
		LOG(ERROR) << "backupMsg failed, return " << ret.ToString();
	}
}

void PBFT::reloadMsg(std::string const& _key, PBFTMsg * _msg) {
	if (!m_backup_db || !_msg) {
		return;
	}

	std::string data;
	auto ret = m_backup_db->Get(m_readOptions, ldb::Slice(_key), &data);
	if (!ret.ok()) {
		LOG(ERROR) << "reloadMsg failed, return " << ret.ToString();
		return;
	}
	if (data.empty()) {
		return;
	}

	_msg->clear();
	RLP rlp(data);
	_msg->populate(rlp[0]);

	LOG(INFO) << "reloadMsg, data len=" << data.size() << ", height=" << _msg->height << ",hash=" << _msg->block_hash.abridged() << ",idx=" << _msg->idx;
}
