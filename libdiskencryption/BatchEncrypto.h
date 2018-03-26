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
 * @file: BatchEncrypto.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once
#include <iostream>
#include <libdevcore/db.h>
#include <leveldb/db.h>
#include <libdevcore/Common.h>
using namespace std;
using namespace dev;
using namespace ldb;

class BatchEncrypto: public ldb::WriteBatch
{
private:
	int m_cryptoMod;
	string m_superKey;
	enum CRYPTOTYPE
	{
		CRYPTO_DEFAULT = 0,
		CRYPTO_LOCAL,
		CRYPTO_KEYCENTER
	};

	bytes enCryptoData(std::string const& v);
	bytes enCryptoData(bytesConstRef const& v);
	bytes enCryptoData(bytes const& v);
	bytes deCryptoData(std::string const& v) const;
	char* ascii2hex(const char* chs,int len);
public:
	BatchEncrypto(void);
	~BatchEncrypto(void);
	ldb::Status Put(ldb::Slice const& key, ldb::Slice const& value);
};
