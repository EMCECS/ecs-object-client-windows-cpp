/*
 * Copyright (c) 2017 - 2022, Dell Technologies, Inc. All Rights Reserved.
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

using namespace std;

#include "GetAllThreads.h"

namespace ecs_sdk
{

DWORD GetAllThreadsForProcess(DWORD dwPID, map<DWORD, THREADENTRY32>& ThreadMap)
{
    THREADENTRY32 te32;

    // take a snapshot of all running threads
    CHandle hThreadSnap(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
    if (hThreadSnap.m_h == INVALID_HANDLE_VALUE)
        return GetLastError();

    // fill in the size of the structure before using it. 
    te32.dwSize = sizeof(THREADENTRY32);

    // retrieve information about the first thread,
    // and exit if unsuccessful
    if (!Thread32First(hThreadSnap, &te32))
        return GetLastError();

    // now walk the thread list of the system,
    // and display thread ids of each thread
    // associated with the specified process
    do {
        if (te32.th32OwnerProcessID == dwPID)
            ThreadMap.emplace(make_pair(te32.th32ThreadID, te32));
    } while (Thread32Next(hThreadSnap, &te32));
    return ERROR_SUCCESS;
}

} // end namespace ecs_sdk
