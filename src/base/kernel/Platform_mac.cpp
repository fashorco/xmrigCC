/* XMRig
 * Copyright (c) 2018-2021 SChernykh   <https://github.com/SChernykh>
 * Copyright (c) 2016-2021 XMRig       <https://github.com/xmrig>, <support@xmrig.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include <IOKit/IOKitLib.h>
#include <IOKit/ps/IOPowerSources.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <uv.h>
#include <thread>
#include <fstream>


#include "base/kernel/Platform.h"
#include "base/tools/Chrono.h"
#include "version.h"


char *xmrig::Platform::createUserAgent()
{
    constexpr const size_t max = 256;

    char *buf = new char[max]();
    int length = snprintf(buf, max,
                          "%s/%s (Macintosh; macOS"
#                         ifdef XMRIG_ARM
                          "; arm64"
#                         else
                          "; x86_64"
#                         endif
                          ") libuv/%s", APP_NAME, APP_VERSION, uv_version_string());

#   ifdef __clang__
    length += snprintf(buf + length, max - length, " clang/%d.%d.%d", __clang_major__, __clang_minor__, __clang_patchlevel__);
#   elif defined(__GNUC__)
    length += snprintf(buf + length, max - length, " gcc/%d.%d.%d", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#   endif

    return buf;
}


bool xmrig::Platform::setThreadAffinity(uint64_t cpu_id)
{
    return true;
}


void xmrig::Platform::setProcessPriority(int)
{
}


void xmrig::Platform::setThreadPriority(int priority)
{
    if (priority == -1) {
        return;
    }

    int prio = 19;
    switch (priority)
    {
    case 1:
        prio = 5;
        break;

    case 2:
        prio = 0;
        break;

    case 3:
        prio = -5;
        break;

    case 4:
        prio = -10;
        break;

    case 5:
        prio = -15;
        break;

    default:
        break;
    }

    setpriority(PRIO_PROCESS, 0, prio);
}


bool xmrig::Platform::isOnBatteryPower()
{
    return IOPSGetTimeRemainingEstimate() != kIOPSTimeRemainingUnlimited;
}


uint64_t xmrig::Platform::idleTime()
{
    uint64_t idle_time  = 0;
    const auto service  = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("IOHIDSystem"));
    const auto property = IORegistryEntryCreateCFProperty(service, CFSTR("HIDIdleTime"), kCFAllocatorDefault, 0);

    CFNumberGetValue((CFNumberRef)property, kCFNumberSInt64Type, &idle_time);

    CFRelease(property);
    IOObjectRelease(service);

    return idle_time / 1000000U;
}

int64_t xmrig::Platform::getThreadSleepTimeToLimitMaxCpuUsage(uint8_t maxCpuUsage)
{
    uint64_t currentSystemTime = Chrono::highResolutionMicroSecs();
    if (currentSystemTime - m_systemTime > MIN_RECALC_THRESHOLD_USEC)
    {
        thread_basic_info_data_t info = {0};
        mach_msg_type_number_t infoCount = THREAD_BASIC_INFO_COUNT;

        mach_port_t port = mach_thread_self();
        kern_return_t kernErr = thread_info(port, THREAD_BASIC_INFO, (thread_info_t) & info, &infoCount);
        mach_port_deallocate(mach_task_self(), port);

        if (kernErr == KERN_SUCCESS)
        {
            int64_t currentThreadUsageTime = info.user_time.microseconds + (info.user_time.seconds * 1000000)
                                             + info.system_time.microseconds + (info.system_time.seconds * 1000000);

            if (m_threadUsageTime > 0 || m_systemTime > 0)
            {
                m_threadTimeToSleep = ((currentThreadUsageTime - m_threadUsageTime) * 100 / maxCpuUsage)
                                      - (currentSystemTime - m_systemTime - m_threadTimeToSleep);
            }

            m_threadUsageTime = currentThreadUsageTime;
            m_systemTime = currentSystemTime;
        }

        // Something went terrible wrong, reset everything
        if (m_threadTimeToSleep > 10000000 || m_threadTimeToSleep < 0)
        {
            m_threadTimeToSleep = 0;
            m_threadUsageTime = 0;
            m_systemTime = 0;
        }

        return m_threadTimeToSleep;
    }

    return 0;
}
