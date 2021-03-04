/*
* Copyright (c) 2017 - 2021, Dell Technologies, Inc. All Rights Reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License").
* You may not use this file except in compliance with the License.
* A copy of the License is located at
*
* http://www.apache.org/licenses/LICENSE-2.0.txt
*
* or in the "license" file accompanying this file. This file is distributed
* on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
* express or implied. See the License for the specific language governing
* permissions and limitations under the License.
*/

#include "stdafx.h"

#include "ECSGlobal.h"

namespace ecs_sdk
{

struct CGarbageCollectThread : public CSimpleWorkerThread
{
	CGarbageCollectThread()
	{}
	~CGarbageCollectThread()
	{}
protected:
	void DoWork()
	{
		CECSConnection::GarbageCollect();
		CThreadPoolBase::GlobalGarbageCollect();
	}
};

CGarbageCollectThread GarbageCollectThread;

// ECSInitLib
// initialize library
void ECSInitLib(
	DWORD dwGarbageCollectInterval)				// 0 - no garbage collection, >0 - interval in ms
{
	CECSConnection::Init();
	if (dwGarbageCollectInterval > 0)
	{
		GarbageCollectThread.SetCycleTime(dwGarbageCollectInterval);
		(void)GarbageCollectThread.CreateThread();
		GarbageCollectThread.StartWork();
	}
}

// ECSTermLib
// clean up library
void ECSTermLib()
{
	GarbageCollectThread.KillThreadWait();
	CECSConnection::TerminateThrottle();
}

} // end namespace ecs_sdk
