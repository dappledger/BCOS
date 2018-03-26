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

#include "SmartVM.h"
#include <unordered_map>
#include <thread>
#include <libdevcore/concurrent_queue.h>
#include <libdevcore/easylog.h>
#include <libdevcore/Guards.h>
#include "VMFactory.h"
#include "JitVM.h"

namespace dev
{
namespace eth
{
namespace
{

	using HitMap = std::unordered_map<h256, uint64_t>;

	HitMap& getHitMap()
	{
		static HitMap s_hitMap;
		return s_hitMap;
	}

	struct JitTask
	{
		bytes code;
		h256 codeHash;
		evm_mode mode;

		static JitTask createStopSentinel() { return JitTask(); }

		bool isStopSentinel()
		{
			assert((!code.empty() || !codeHash) && "'empty code => empty hash' invariant failed");
			return code.empty();
		}
	};

	class JitWorker
	{
		concurrent_queue<JitTask> m_queue;
		std::thread m_worker; // Worker must be last to initialize

		void work()
		{
			LOG(INFO) << "JIT worker started.";
			JitTask task;
			while (!(task = m_queue.pop()).isStopSentinel())
			{
				LOG(INFO) << "Compilation... " << task.codeHash;
				JitVM::compile(task.mode, {task.code.data(), task.code.size()}, task.codeHash);
				LOG(INFO) << "   ...finished " << task.codeHash;
			}
			LOG(INFO) << "JIT worker finished.";
		}

	public:
		JitWorker() noexcept: m_worker([this]{ work(); })
		{}

		~JitWorker()
		{
			push(JitTask::createStopSentinel());
			m_worker.join();
		}

		void push(JitTask&& _task) { m_queue.push(std::move(_task)); }
	};
}

bytesConstRef SmartVM::execImpl(u256& io_gas, ExtVMFace& _ext, OnOpFunc const& _onOp)
{
	auto vmKind = VMKind::Interpreter; // default VM
	auto mode = JitVM::scheduleToMode(_ext.evmSchedule());
	// Jitted EVM code already in memory?
	if (JitVM::isCodeReady(mode, _ext.codeHash))
	{
		LOG(INFO) << "JIT:           " << _ext.codeHash;
		vmKind = VMKind::JIT;
	}
	else if (!_ext.code.empty()) // This check is needed for VM tests
	{
		static JitWorker s_worker;

		// Check EVM code hit count
		static const uint64_t c_hitTreshold = 2;
		auto& hits = getHitMap()[_ext.codeHash];
		++hits;
		if (hits == c_hitTreshold)
		{
			LOG(INFO) << "Schedule:      " << _ext.codeHash;
			s_worker.push({_ext.code, _ext.codeHash, mode});
		}
		LOG(INFO) << "Interpreter:   " << _ext.codeHash;
	}

	// TODO: Selected VM must be kept only because it returns reference to its internal memory.
	//       VM implementations should be stateless, without escaping memory reference.
	m_selectedVM = VMFactory::create(vmKind);
	return m_selectedVM->execImpl(io_gas, _ext, _onOp);
}

}
}
