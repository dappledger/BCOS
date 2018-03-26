﻿/*
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
 * @file: LOracle.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "LOracle.h"

#include <unistd.h>
#include <iostream>
#include <libdevcore/db.h>
#include <libdevcore/Common.h>
#include <libdevcore/CommonData.h>

using namespace std;
using namespace leveldb;
using namespace dev;

static void* EncodeValue(char* v) { return reinterpret_cast<void*>(v); }

static void Deleter(const Slice&, void* v) {

	char * pch = reinterpret_cast<char *>(v);
	delete pch;
	//	std::cout << "delete cache " << toHex(dev::bytesConstRef(key)) << std::endl;
}

class OracleWriterBatch : public leveldb::WriteBatch::Handler
{
public:

	virtual void Put(leveldb::Slice const& _key, leveldb::Slice const& _value)
	{
		leveldb::OdbcWriteStruct odbcWrite(_key, _value);
		_vData.push_back(odbcWrite);
	}

	virtual void Delete(leveldb::Slice const& _key)
	{
		std::cout << "Delete" << _key.ToString();
	}

	virtual void setCache(Cache *cc){
		if (cc == nullptr)
		{
			return;
		}

		for (auto data : _vData)
		{
			std::string sHex = toHex(dev::bytesConstRef(data._sValue));
			char *cstr = new char[sHex.size() + 1];
			strcpy(cstr, sHex.c_str());
			cc->Release(cc->Insert(data._sKey, EncodeValue(cstr), sHex.size(), &Deleter));
		}
	}

	std::string toWriteSqlString(std::string tableName)
	{
		if (_vData.size() == 0)
		{
			return "";
		}

		std::string retSql = "replace into " + tableName + " ( s_key,s_value) values ";
		int i = 0;
		for (auto data : _vData)
		{
			if (i > 0)
			{
				retSql += ",";
			}

			retSql += " ('" + toHex(dev::bytesConstRef(data._sKey)) + "','" + toHex(dev::bytesConstRef(data._sValue)) + "')";
			i++;
		}

		retSql += ";";

		return retSql;
	}

private:
	std::vector<leveldb::OdbcWriteStruct> _vData;
};

LOracle::LOracle(const std::string &sDbConnInfo, const std::string &sDbName, const std::string &sTableName, const std::string &sUserName, const std::string &sPwd, int iCacheSize)
:LvlDbInterface(sDbConnInfo, sDbName, sTableName, sUserName, sPwd, DBEngineType::oracle, iCacheSize)
{
	std::cout << "LOracle init success.";
}


//暂时未用
Status LOracle::Delete(const WriteOptions&, const Slice& key)
{
	std::cout << "LOracle delete " << key.ToString();

	//清掉缓存
	Status s;
	_dataCc->Erase(key);
	return s;
}

Status LOracle::Write(const WriteOptions&, WriteBatch* batch)
{
	leveldb::Status s = Status::Corruption("batch write");
	bool bSucc = false;
	int iTryTimes = 0;
	do
	{
		//std::cout << "writeData " << std::endl;
		try
		{
			OracleWriterBatch n;
			batch->Iterate(&n);

			std::string sql = n.toWriteSqlString(_sTableName);
			if (sql == "")
			{
				cerr << "there is no data in write batch." << std::endl;
				return Status::OK();
			}

			SACommand cmd(&con);
			SAString saSql(sql.c_str());
			cmd.setCommandText(saSql);

			cmd.Execute();
			con.Commit();

			n.setCache(_dataCc);
			s = Status::OK();
			bSucc = true;
		}
		catch (SAException &x)
		{
			sleep(100);
			// print error message
			std::cerr << "SAException: " << x.ErrText().GetMultiByteChars() << std::endl;
			if (x.ErrNativeCode() == CR_SERVER_LOST || x.ErrNativeCode() == CR_SERVER_GONE_ERROR)
			{
				if (con.isConnected())
				{
					con.Disconnect();
				}

				LvlDbInterface::Connect();
				std::cout << "SAException: lostServer  writeData：" << iTryTimes << "|" << x.ErrText().GetMultiByteChars() << "|" << x.ErrNativeCode() << std::endl;
				continue;
			}
			else{
				std::cout << "SAException: other reason  writeData：" << iTryTimes << "|" << x.ErrText().GetMultiByteChars() << "|" << x.ErrNativeCode() << std::endl;

			}
			iTryTimes++;

		}
		catch (std::exception& e){
			//todo 其他异常是否直接退出。需考虑
			std::cerr << "exception: " << e.what() << std::endl;
			break;
		}
		catch (...){
			//todo 其他异常是否直接退出。需考虑
			std::cerr << "unknown exception occured" << std::endl;
			break;
		}

	} while (!bSucc && iTryTimes <= 3);
	return s;
}

Status LOracle::Get(const ReadOptions&, const Slice& key, std::string* value)
{

	leveldb::Status s = leveldb::Status::NotFound("get");
	ostringstream ossql;
	try
	{
		//std::cout << "getData |" << toHex(bytesConstRef(key)) << std::endl;
		Cache::Handle *handle = _dataCc->Lookup(key);
		if (handle == NULL) {

			//用占位符 字符串替换
			bool bBreak = false;
			bool bFind = false;
			int iTryTimes = 0;
			do{
				try{
					ossql << "select s_value from " << _sTableName << " where s_key = '" << toHex(bytesConstRef(key)) << "';";
					SACommand cmd(&con);
					//std::cout << "LOracle Get is |" << ossql.str() << std::endl;
					SAString saSql(ossql.str().c_str());
					cmd.setCommandText(saSql);
					cmd.Execute();
					//std::cout << "LOracle DB querysql GET: " << ossql.str() << std::endl;

					while (cmd.FetchNext())
					{
						bFind = true;
						// 最后一个字段
						for (int i = 1; i <= cmd.FieldCount(); ++i)
						{
							*value = cmd[i].asString();
							//	std::cout << (const char*)cmd[i].Name() << "|" << (const char*)cmd[i].asBytes() << "|" << *value << endl;
						}
					}
					bBreak = true;
				}
				catch (SAException &x)
				{
					sleep(100);
					std::cout << "SAException: get data " << ossql.str() << "|" << x.ErrText().GetMultiByteChars() << "|" << x.ErrNativeCode() << std::endl;
					iTryTimes++;
					//可能连接闪断，需要尝试重新连接
					if (x.ErrNativeCode() == CR_SERVER_LOST || x.ErrNativeCode() == CR_SERVER_GONE_ERROR)
					{
						if (con.isConnected())
						{
							con.Disconnect();
						}

						LvlDbInterface::Connect();
						std::cout << "SAException: lostServer  getData: " << iTryTimes << "|" << ossql.str() << "|" << x.ErrText().GetMultiByteChars() << "|" << x.ErrNativeCode() << std::endl;

						continue;
					}
					else{

						//其余异常不知这么处理是否OK
						if (con.isConnected())
						{
							con.Disconnect();
						}

						LvlDbInterface::Connect();
						std::cout << "SAException: Oracle other reason. getData: " << iTryTimes << "|" << ossql.str() << "|" << x.ErrText().GetMultiByteChars() << "|" << x.ErrNativeCode() << std::endl;

					}
				}
				catch (std::exception& e){
					//todo 其他异常是否直接退出。需考虑
					std::cout << "exception: " << e.what() << std::endl;
					break;
				}
				catch (...){
					//todo 其他异常是否直接退出。需考虑
					std::cout << "unknown exception occured" << std::endl;
					break;
				}
				//std::cout << " LOracle GET end :" << bFind << " |" << toHex(bytesConstRef(key)) << "|" << *value << endl;
			} while (!bBreak && iTryTimes <= 3);

			if (bFind)
			{
				char *cstr = new char[value->size() + 1];
				strcpy(cstr, value->c_str());
				//交给楠哥free
				_dataCc->Release(_dataCc->Insert(key, EncodeValue(cstr), value->size(), &Deleter));
				*value = asString(fromHex(*value));
			}

			s = Status::OK();
		}
		else{
			//std::cout << "LOracle CAHCE HAS DATA " << toHex(dev::bytesConstRef(key)) << std::endl;
			char * ch = reinterpret_cast<char *>(_dataCc->Value(handle));
			*value = ch;
			_dataCc->Release(handle);
			//std::cout << "LOracle GET datacache is hex data is : " << toHex(dev::bytesConstRef(key)) << "|" << ch << "|" << value <<"|" << value->size()<< std::endl;
			//std::cout << "LOracle GET datacache is " << key.ToString() << "|" << *value << std::endl;
			*value = asString(fromHex(*value));

		}
	}
	catch (SAException &x)
	{
		std::cout << "SAException: get data " << ossql.str() << "|" << x.ErrText().GetMultiByteChars() << "|" << x.ErrNativeCode() << std::endl;
	}
	catch (std::exception& e){
		std::cout << "exception: " << e.what() << std::endl;
	}
	catch (...){
		std::cout << "unknown exception occured" << std::endl;
	}

	return s;
}


Status  LOracle::Put(const WriteOptions& opt, const Slice& key, const Slice& value)
{
	leveldb::WriteBatch batch;
	batch.Put(key, value);

	return Write(opt, &batch);
}