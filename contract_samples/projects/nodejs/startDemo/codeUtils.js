/**
 * @file: codeUtils.js
 * @author: fisco-dev
 * 
 * @date: 2017
 */

/*
编码工具类，包含交易编码及日志编码
*/
var Web3 = require('web3');
var sha3 = require("./sha3")
var Event = require('web3/lib/web3/event');
var Coder = require('web3/lib/solidity/coder');

/*
#交易编码
###参数编码
*/
function codeParams(types, params) {
    var code_ret = Coder.encodeParams(types, params);
    //console.log("code_ret : ",code_ret);
    return code_ret;
}

/*
###函数名编码
*/
function codeFun(fun_str) {

    var code_fun = '0x' + sha3(fun_str).slice(0, 8);
    console.log("code_fun : ", code_fun);
    return code_fun;
}

/*
###交易数据编码
*/
function codeTxData(fun_Str, types, params) {
    var txData_code = codeFun(fun_Str);
    txData_code += codeParams(types, params);
    console.log("txData_code : ", txData_code);
    return txData_code;
}

function hex2a(hex) {
    var str = '';
    for (var i = 0; i < hex.length; i += 2) {
        var v = parseInt(hex.substr(i, 2), 16);
        if (v) str += String.fromCharCode(v);
    }
    return str;
}

exports.codeTxData = codeTxData;
exports.hex2a = hex2a;