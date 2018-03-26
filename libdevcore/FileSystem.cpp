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
/** @file FileSystem.cpp
 * @authors
 *	 Eric Lombrozo <elombrozo@gmail.com>
 *	 Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include "FileSystem.h"
#include "Common.h"
#include "easylog.h"

#if defined(_WIN32)
#include <shlobj.h>
#else
#include <stdlib.h>
#include <stdio.h>
#include <pwd.h>
#include <unistd.h>
#endif
#include <boost/filesystem.hpp>
using namespace std;
using namespace dev;

//TODO: remove init_priority, use a better way.
// Should be written to only once during startup
//ubuntu 编过
//static string s_ethereumDatadir;
static string s_ethereumDatadir __attribute__ ((init_priority (1000))); //centos

static string s_ethereumIpcPath;
static string s_ehtereumConfigPath;
static string s_caInitType;

static int	s_cryptoMod;//加密方式
static map<int,string> s_dataKey;//datakey数据

static int s_cryptoprivatekeyMod;//私钥是否使用keycenter加密
static string s_privateKey;//证书私钥数据
static int s_ssl;//是否使用ssl证书进行数据传输


void dev::setCryptoPrivateKeyMod(int cryptoprivatekeyMod)
{
	s_cryptoprivatekeyMod = cryptoprivatekeyMod;
}

int dev::getCryptoPrivateKeyMod()
{
	return s_cryptoprivatekeyMod;
}

void dev::setPrivateKey(string const& privateKey)
{
	s_privateKey = privateKey;
}

string dev::getPrivateKey()
{
	return s_privateKey;
}

void dev::setCryptoMod(int cryptoMod)
{
	s_cryptoMod = cryptoMod;
}

int dev::getCryptoMod()
{
	return s_cryptoMod;
}

int dev::getSSL()
{
	return s_ssl;
}

void dev::setSSL(int ssl)
{
	s_ssl = ssl;
}

void dev::setDataKey(string const& dataKey1,string const& dataKey2,string const& dataKey3,string const& dataKey4)
{
	s_dataKey[0] = dataKey1;
	s_dataKey[1] = dataKey2;
	s_dataKey[2] = dataKey3;
	s_dataKey[3] = dataKey4;
}

map<int,string> dev::getDataKey()
{
	return s_dataKey;
}


void dev::setDataDir(string const& _dataDir)
{
	s_ethereumDatadir = _dataDir;
}

void dev::setIpcPath(string const& _ipcDir)
{
	s_ethereumIpcPath = _ipcDir;
}

void dev::setConfigPath(string const& _configPath)
{
	s_ehtereumConfigPath = _configPath;
}

void dev::setCaInitType(string const& _caInitType)
{
	s_caInitType = _caInitType;
}

string dev::getCaInitType()
{
	if (s_caInitType.empty())
	{
		return "webank";
	}

	return s_caInitType;
}

string dev::getConfigPath()
{
	if (s_ehtereumConfigPath.empty())
	{
		return getDataDir() + "/config.json";
	}

	return s_ehtereumConfigPath;
}

string dev::getIpcPath()
{
	if (s_ethereumIpcPath.empty())
		return string(getDataDir());
	else
	{
		size_t socketPos = s_ethereumIpcPath.rfind("geth.ipc");
		if (socketPos != string::npos)
			return s_ethereumIpcPath.substr(0, socketPos);
		return s_ethereumIpcPath;
	}
}

string dev::getDataDir(string _prefix)
{
	if (_prefix.empty())
		_prefix = "ethereum";
	if (_prefix == "ethereum" && !s_ethereumDatadir.empty())
		return s_ethereumDatadir;
	if (_prefix == "web3" && !s_ethereumDatadir.empty())
		return s_ethereumDatadir+".web3";
		
	return getDefaultDataDir(_prefix);
}

string dev::getDefaultDataDir(string _prefix)
{
	if (_prefix.empty())
		_prefix = "ethereum";

#if defined(_WIN32)
	_prefix[0] = toupper(_prefix[0]);
	char path[1024] = "";
	if (SHGetSpecialFolderPathA(NULL, path, CSIDL_APPDATA, true))
		return (boost::filesystem::path(path) / _prefix).string();
	else
	{
	#ifndef _MSC_VER // todo?
		LOG(WARNING) << "getDataDir(): SHGetSpecialFolderPathA() failed.";
	#endif
		BOOST_THROW_EXCEPTION(std::runtime_error("getDataDir() - SHGetSpecialFolderPathA() failed."));
	}
#else
	boost::filesystem::path dataDirPath;
	char const* homeDir = getenv("HOME");
	if (!homeDir || strlen(homeDir) == 0)
	{
		struct passwd* pwd = getpwuid(getuid());
		if (pwd)
			homeDir = pwd->pw_dir;
	}

	if (!homeDir || strlen(homeDir) == 0)
		dataDirPath = boost::filesystem::path("/");
	else
		dataDirPath = boost::filesystem::path(homeDir);

	return (dataDirPath / ("." + _prefix)).string();
#endif
}
