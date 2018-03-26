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
 * @file: SystemContract.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once
#include <libdevcore/Guards.h>
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/CommonIO.h>
#include "Client.h"
#include "SystemContractApi.h"

using namespace std;
using namespace dev;

using namespace dev::eth;


namespace dev
{
namespace eth
{

enum class FilterType {
    Account,
    Node
};

class SystemContractApi;
enum class SystemContractCode;

using Address = h160;


/**
 * @brief Main API hub for System Contract
 */
//过滤器
struct SystemFilter {
    Address filter;
    string  name;
};
//行为合约
struct SystemAction {
    Address action;
    string  name;
};

class SystemContract: public SystemContractApi
{
public:
    /// Constructor.
    SystemContract(const Address _address, const Address _god, Client* _client) : SystemContractApi(  _address, _god), m_client(_client), m_tempblock(0)
    {
        //m_tempblock = m_client->block(m_client->blockChain().number());
       // Transactions ts;
       // updateSystemContract(ts);

        std::shared_ptr<Block> tempblock(new Block(0));
        *tempblock = m_client->block(m_client->blockChain().number());

        //Transactions ts;
        updateSystemContract(tempblock);

    }

    /// Destructor.
    virtual ~SystemContract() {}

    //所有的filter检查
    virtual u256 transactionFilterCheck(const Transaction & transaction) override;

   virtual void startStatTranscation(h256)override;

    virtual void updateCache(Address address) override;

    //是否是链的管理员
    virtual bool isAdmin(const Address & _address) override;
    //获取全网配置项
    virtual bool getValue(const string _key, string & _value) override;

    // 获取节点列表 里面已经含有idx
    virtual void getAllNode(int _blocknumber/*<0 代表最新块*/ ,std::vector< NodeConnParams> & _nodelist )override;
    virtual u256 getBlockChainNumber()override;

    virtual void getCaInfo(string _hash,CaInfo & _cainfo) override;

    //系统合约被命中的时候，要更新cache 现在先不实现
    //virtual void    updateSystemContract(const Transactions &) override;
    virtual void updateSystemContract(std::shared_ptr<Block> block) override;

private:

    //交易统计数据
    std::map<h256, pair<u256,u256> > m_stattransation;

    Client* m_client;

    mutable SharedMutex  m_blocklock; // 锁块
    //Block m_tempblock;//提高性能，避免反复构建

    std::shared_ptr<Block> m_tempblock;

    mutable SharedMutex  m_lockroute;//锁cache
    std::vector<SystemAction> m_routes;

    mutable SharedMutex  m_lockfilter;//锁cache
    SystemFilter m_transactionfilter;//目前只有交易，就先只用一个变量吧
    std::map<h256, u256> m_filterchecktranscache; //filterCheck 交易的cache

    unsigned m_transcachehit;//命中次数
    unsigned m_transcount;//总数

    mutable SharedMutex  m_locknode;//锁节点列表更新
    std::vector< NodeConnParams> m_nodelist;//缓存当前最新块的节点列表

    mutable SharedMutex  m_lockca;//锁节点列表更新
    std::map<string,CaInfo> m_calist;//缓存当前最新块的ca列表

	mutable Address m_abiMgrAddr;


    ExecutionResult call(Address const& _to, bytes const& _inputdata, bool cache = false) ; // 系统合约调用

    //ExecutionResult call(const std::string &name, bytes const& _inputdata) ; // 系统合约调用

    Address getRoute(const string & _route)const;

    h256 filterCheckTransCacheKey(const Transaction & _t) const ;

    void updateRoute( );
    void updateNode( );
    void tempGetAllNode(int _blocknumber,std::vector< NodeConnParams> & _nodevector);//在指定块上面获取列表
    void updateConfig( );
    void updateCa( );
	void updateContractAbiInfo();

    void getNodeFromContract(std::function<ExecutionResult(Address const,bytes const,bool cache )>,std::vector< NodeConnParams> & _nodelist);

    struct CallCache {
    	std::map<bytes, ExecutionResult> res;
    };

    std::map<Address, CallCache> _callCaches;
};



}
}



