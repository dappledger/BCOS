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
/** @file Worker.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include "Worker.h"

#include <chrono>
#include <thread>
#include "easylog.h"

using namespace std;
using namespace dev;

void Worker::startWorking()
{
//	LOG(INFO) << "startWorking for thread" << m_name;
	Guard l(x_work);
	if (m_work)
	{
		WorkerState ex = WorkerState::Stopped;
		m_state.compare_exchange_strong(ex, WorkerState::Starting);
		//如果m_state==ex 就把WorkerState::Starting赋给m_state
	}
	else
	{
		m_state = WorkerState::Starting;
		//新起线程异步执行
		m_work.reset(new thread([&]()
		{
			pthread_setThreadName(m_name.c_str());
//			LOG(INFO) << "Thread begins";
			while (m_state != WorkerState::Killing)
			{
				WorkerState ex = WorkerState::Starting;
				//不确定这段代码执行有什么用， 为了下面的日志？  还有一个强制转换也不确定
				bool ok = m_state.compare_exchange_strong(ex, WorkerState::Started);
//				LOG(INFO) << "Trying to set Started: Thread was" << (unsigned)ex << "; " << ok;
				(void)ok;

				try
				{
					//host继承worker  这里调用host里面的继承方法
					//client继承worker
					startedWorking();
					workLoop();
					doneWorking();
				}
				catch (std::exception const& _e)
				{
					LOG(ERROR) << "Exception thrown in Worker thread[" << m_name << "]: " << _e.what();
				}

//				ex = WorkerState::Stopping;
//				m_state.compare_exchange_strong(ex, WorkerState::Stopped);

				ex = m_state.exchange(WorkerState::Stopped);
//				LOG(INFO) << "State: Stopped: Thread was" << (unsigned)ex;
				if (ex == WorkerState::Killing || ex == WorkerState::Starting)
					m_state.exchange(ex);

//				LOG(INFO) << "Waiting until not Stopped...";
				DEV_TIMED_ABOVE("Worker stopping", 100)
					while (m_state == WorkerState::Stopped)
						this_thread::sleep_for(chrono::milliseconds(20));
			}
		}));
//		LOG(INFO) << "Spawning" << m_name;
	}
	DEV_TIMED_ABOVE("Start worker", 100)
		while (m_state == WorkerState::Starting)
			this_thread::sleep_for(chrono::microseconds(20));
}

void Worker::stopWorking()
{
	DEV_GUARDED(x_work)
		if (m_work)
		{
			WorkerState ex = WorkerState::Started;
			m_state.compare_exchange_strong(ex, WorkerState::Stopping);

			DEV_TIMED_ABOVE("Stop worker", 100)
				while (m_state != WorkerState::Stopped)
					this_thread::sleep_for(chrono::microseconds(20));
		}
}

void Worker::terminate()
{
//	LOG(INFO) << "stopWorking for thread" << m_name;
	DEV_GUARDED(x_work)
		if (m_work)
		{
			m_state.exchange(WorkerState::Killing);

			DEV_TIMED_ABOVE("Terminate worker", 100)
				m_work->join();

			m_work.reset();
		}
}

void Worker::workLoop()
{
	while (m_state == WorkerState::Started)
	{
		if (m_idleWaitMs)
			this_thread::sleep_for(chrono::milliseconds(m_idleWaitMs));
		doWork();
	}
}
