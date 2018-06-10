/*
 * Copyright (C) 2017-2018  Christian Berger
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "cluon/SharedMemory.hpp"

// clang-format off
#ifdef WIN32
    #include <limits>
#else
    #include <cstdlib>
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif
// clang-format on

#include <cstring>
#include <iostream>

namespace cluon {

SharedMemory::SharedMemory(const std::string &name, uint32_t size) noexcept
    : m_size(size) {
    if (!name.empty()) {
#ifdef WIN32
        constexpr int MAX_LENGTH_NAME{MAX_PATH};
#else
        constexpr int MAX_LENGTH_NAME{254};
#endif
        const std::string n{name.substr(0, (name.size() > MAX_LENGTH_NAME ? MAX_LENGTH_NAME : name.size()))};
        if ('/' != n[0]) {
            m_name = "/";
        }
        m_name += n;
        if (m_name.size() > MAX_LENGTH_NAME) {
            m_name = m_name.substr(0, MAX_LENGTH_NAME);
        }

#ifdef WIN32
        initWIN32();
#else
        const char *CLUON_SHAREDMEMORY_POSIX = getenv("CLUON_SHAREDMEMORY_POSIX");
        m_usePOSIX = ( (nullptr != CLUON_SHAREDMEMORY_POSIX) && (CLUON_SHAREDMEMORY_POSIX[0] == '1') );
        std::clog << "[cluon::SharedMemory] Using " << (m_usePOSIX ? "POSIX" : "SysV") << " implementation." << std::endl;
        if (m_usePOSIX) {
            initPOSIX();
        }
        else {
            initSysV();
        }
#endif
    }
}

SharedMemory::~SharedMemory() noexcept {
#ifdef WIN32
    deinitWIN32();
#else
    if (m_usePOSIX) {
        deinitPOSIX();
    }
    else {
        deinitSysV();
    }
#endif
}

void SharedMemory::lock() noexcept {
#ifdef WIN32
    lockWIN32();
#else
    if (m_usePOSIX) {
        lockPOSIX();
    }
    else {
        lockSysV();
    }
#endif
}

void SharedMemory::unlock() noexcept {
#ifdef WIN32
    unlockWIN32();
#else
    if (m_usePOSIX) {
        unlockPOSIX();
    }
    else {
        unlockSysV();
    }
#endif
}

void SharedMemory::wait() noexcept {
#ifdef WIN32
    waitWIN32();
#else
    if (m_usePOSIX) {
        waitPOSIX();
    }
    else {
        waitSysV();
    }
#endif
}

void SharedMemory::notifyAll() noexcept {
#ifdef WIN32
    notifyAllWIN32();
#else
    if (m_usePOSIX) {
        notifyAllPOSIX();
    }
    else {
        notifyAllSysV();
    }
#endif
}

bool SharedMemory::valid() noexcept {
    bool valid{true};
    valid &= (nullptr != m_sharedMemory);
    valid &= (0 < m_size);
#ifndef WIN32
    if (m_usePOSIX) {
        valid &= validPOSIX();
    }
    else {
        valid &= validSysV();
    }
#endif
    return valid;
}

char *SharedMemory::data() noexcept {
    return m_userAccessibleSharedMemory;
}

uint32_t SharedMemory::size() const noexcept {
    return m_size;
}

const std::string SharedMemory::name() const noexcept {
    return m_name;
}

////////////////////////////////////////////////////////////////////////////////
// Platform-dependent implementations.
#ifdef WIN32
void SharedMemory::initWIN32() noexcept {
    std::string mutexName = m_name;
    if (mutexName.size() > MAX_PATH) {
        mutexName = mutexName.substr(0, MAX_PATH - 6);
    }
    const std::string conditionEventName = mutexName + "_event";
    mutexName += "_mutex";

    if (0 < m_size) {
        // Create a shared memory area and semaphores.
        const LONG MUTEX_INITIAL_COUNT = 1;
        const LONG MUTEX_MAX_COUNT     = 1;
        const DWORD FLAGS              = 0; // Reserved.
        __mutex                        = CreateSemaphoreEx(NULL, MUTEX_INITIAL_COUNT, MUTEX_MAX_COUNT, mutexName.c_str(), FLAGS, SEMAPHORE_ALL_ACCESS);
        if (nullptr != __mutex) {
            __conditionEvent = CreateEvent(
                NULL /*use default security*/, TRUE /*manually resetting event*/, FALSE /*initial state is not signaled*/, conditionEventName.c_str());
            if (nullptr != __conditionEvent) {
                __sharedMemory = CreateFileMapping(INVALID_HANDLE_VALUE /*use paging file*/,
                                                   NULL /*use default security*/,
                                                   PAGE_READWRITE,
                                                   0,
                                                   m_size + sizeof(uint32_t) /*size + size-information (uint32_t)*/,
                                                   m_name.c_str());
                if (nullptr != __sharedMemory) {
                    m_sharedMemory = (char *)MapViewOfFile(__sharedMemory, FILE_MAP_ALL_ACCESS, 0, 0, m_size + sizeof(uint32_t));
                    if (nullptr != m_sharedMemory) {
                        // Provide size information at the beginning of the shared memory.
                        *(uint32_t *)m_sharedMemory  = m_size;
                        m_userAccessibleSharedMemory = m_sharedMemory + sizeof(uint32_t);
                    } else {
                        std::cerr << "[cluon::SharedMemory] Failed to map shared memory '" << m_name << "': "
                                  << " (" << GetLastError() << ")" << std::endl;
                        CloseHandle(__sharedMemory);
                        __sharedMemory = nullptr;

                        CloseHandle(__conditionEvent);
                        __conditionEvent = nullptr;

                        CloseHandle(__mutex);
                        __mutex = nullptr;
                    }
                } else {
                    std::cerr << "[cluon::SharedMemory] Failed to request shared memory '" << m_name << "': "
                              << " (" << GetLastError() << ")" << std::endl;
                    CloseHandle(__conditionEvent);
                    __conditionEvent = nullptr;

                    CloseHandle(__mutex);
                    __mutex = nullptr;
                }
            } else {
                std::cerr << "[cluon::SharedMemory] Failed to request event '" << conditionEventName << "': "
                          << " (" << GetLastError() << ")" << std::endl;
                CloseHandle(__conditionEvent);
                __conditionEvent = nullptr;

                CloseHandle(__mutex);
                __mutex = nullptr;
            }
        } else {
            std::cerr << "[cluon::SharedMemory] Failed to create mutex '" << mutexName << "': "
                      << " (" << GetLastError() << ")" << std::endl;
            CloseHandle(__mutex);
            __mutex = nullptr;
        }
    } else {
        // Open a shared memory area and semaphores.
        m_hasOnlyAttachedToSharedMemory = true;
        const BOOL INHERIT_HANDLE       = FALSE;
        __mutex                         = OpenSemaphore(SEMAPHORE_ALL_ACCESS, INHERIT_HANDLE, mutexName.c_str());
        if (nullptr != __mutex) {
            __conditionEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE /*do not inherit the name*/, conditionEventName.c_str());
            if (nullptr != __conditionEvent) {
                __sharedMemory = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE /*do not inherit the name*/, m_name.c_str());
                if (nullptr != __sharedMemory) {
                    // Firstly, map only for the size of a uint32_t to read the entire size.
                    m_sharedMemory = (char *)MapViewOfFile(__sharedMemory, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(uint32_t));
                    if (nullptr != m_sharedMemory) {
                        //  Now, read the real size...
                        m_size = *(uint32_t *)m_sharedMemory;
                        // ..unmap and re-map.
                        UnmapViewOfFile(m_sharedMemory);
                        m_sharedMemory = (char *)MapViewOfFile(__sharedMemory, FILE_MAP_ALL_ACCESS, 0, 0, m_size + sizeof(uint32_t));
                        if (nullptr != m_sharedMemory) {
                            m_userAccessibleSharedMemory = m_sharedMemory + sizeof(uint32_t);
                        } else {
                            std::cerr << "[cluon::SharedMemory] Failed to finally map shared memory '" << m_name << "': "
                                      << " (" << GetLastError() << ")" << std::endl;
                            CloseHandle(__sharedMemory);
                            __sharedMemory = nullptr;

                            CloseHandle(__conditionEvent);
                            __conditionEvent = nullptr;

                            CloseHandle(__mutex);
                            __mutex = nullptr;
                        }
                    } else {
                        std::cerr << "[cluon::SharedMemory] Failed to temporarily map shared memory '" << m_name << "': "
                                  << " (" << GetLastError() << ")" << std::endl;
                        CloseHandle(__sharedMemory);
                        __sharedMemory = nullptr;

                        CloseHandle(__conditionEvent);
                        __conditionEvent = nullptr;

                        CloseHandle(__mutex);
                        __mutex = nullptr;
                    }
                } else {
                    std::cerr << "[cluon::SharedMemory] Failed to open shared memory '" << m_name << "': "
                              << " (" << GetLastError() << ")" << std::endl;
                    CloseHandle(__conditionEvent);
                    __conditionEvent = nullptr;

                    CloseHandle(__mutex);
                    __mutex = nullptr;
                }
            } else {
                std::cerr << "[cluon::SharedMemory] Failed to open event '" << conditionEventName << "': "
                          << " (" << GetLastError() << ")" << std::endl;
                CloseHandle(__conditionEvent);
                __conditionEvent = nullptr;

                CloseHandle(__mutex);
                __mutex = nullptr;
            }
        } else {
            std::cerr << "[cluon::SharedMemory] Failed to open mutex '" << mutexName << "': "
                      << " (" << GetLastError() << ")" << std::endl;
            CloseHandle(__mutex);
            __mutex = nullptr;
        }
    }
}

void SharedMemory::deinitWIN32() noexcept {
    if (nullptr != __conditionEvent) {
        SetEvent(__conditionEvent);
        CloseHandle(__conditionEvent);
    }
    if (nullptr != __mutex) {
        unlock();
        CloseHandle(__mutex);
    }
    if (nullptr != m_sharedMemory) {
        UnmapViewOfFile(m_sharedMemory);
    }
    if (nullptr != __sharedMemory) {
        CloseHandle(__sharedMemory);
    }
}

void SharedMemory::lockWIN32() noexcept {
    if (nullptr != __mutex) {
        WaitForSingleObject(__mutex, INFINITE);
    }
}

void SharedMemory::unlockWIN32() noexcept {
    if (nullptr != __mutex) {
        const LONG RELEASE_COUNT = 1;
        ReleaseSemaphore(__mutex, RELEASE_COUNT, 0);
    }
}

void SharedMemory::waitWIN32() noexcept {
    if (nullptr != __conditionEvent) {
        WaitForSingleObject(__conditionEvent, INFINITE);
    }
}

void SharedMemory::notifyAllWIN32() noexcept {
    if (nullptr != __conditionEvent) {
        SetEvent(__conditionEvent);
        ResetEvent(__conditionEvent);
    }
}

#else /* POSIX */

void SharedMemory::initPOSIX() noexcept {
    // If size is greater than 0, the caller wants to create a new shared
    // memory area. Otherwise, the caller wants to open an existing shared memory.
    int flags = O_RDWR;
    if (0 < m_size) {
        flags |= O_CREAT | O_EXCL;
    }

    m_fd = ::shm_open(m_name.c_str(), flags, S_IRUSR | S_IWUSR);
    if (-1 == m_fd) {
        std::cerr << "[cluon::SharedMemory (POSIX)] Failed to open shared memory '" << m_name << "': " << ::strerror(errno) << " (" << errno << ")" << std::endl;
        // Try to remove existing shared memory segment and try again.
        if ((flags & O_CREAT) == O_CREAT) {
            std::clog << "[cluon::SharedMemory (POSIX)] Trying to remove existing shared memory '" << m_name << "' and trying again... ";
            if (0 == ::shm_unlink(m_name.c_str())) {
                m_fd = ::shm_open(m_name.c_str(), flags, S_IRUSR | S_IWUSR);
            }

            if (-1 == m_fd) {
                std::cerr << "failed: " << ::strerror(errno) << " (" << errno << ")" << std::endl; // LCOV_EXCL_LINE
            } else {
                std::cerr << "succeeded." << std::endl;
            }
        }
    }

    if (-1 != m_fd) {
        bool retVal{true};

        // When creating a shared memory segment, truncate it.
        if (0 < m_size) {
            retVal = (0 == ::ftruncate(m_fd, static_cast<off_t>(sizeof(SharedMemoryHeader) + m_size)));
            if (!retVal) {
                std::cerr << "[cluon::SharedMemory (POSIX)] Failed to truncate '" << m_name << "': " << ::strerror(errno) << " (" << errno << ")" // LCOV_EXCL_LINE
                          << std::endl;                                                                                                   // LCOV_EXCL_LINE
            }
        }

        // Accessing shared memory segment.
        if (retVal) {
            // On opening (i.e., NOT creating) a shared memory segment, m_size is still 0 and we need to figure out the size first.
            m_sharedMemory = static_cast<char *>(::mmap(0, sizeof(SharedMemoryHeader) + m_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0));
            if (MAP_FAILED != m_sharedMemory) {
                m_sharedMemoryHeader = reinterpret_cast<SharedMemoryHeader *>(m_sharedMemory);

                // On creating (i.e., NOT opening) a shared memory segment, setup the shared memory header.
                if (0 < m_size) {
                    // Store user accessible size in shared memory.
                    m_sharedMemoryHeader->__size = m_size;

                    // Create process-shared mutex (fastest approach, cf. Stevens & Rago: "Advanced Programming in the UNIX (R) Environment").
                    pthread_mutexattr_t mutexAttribute;
                    ::pthread_mutexattr_init(&mutexAttribute);
                    ::pthread_mutexattr_setpshared(&mutexAttribute, PTHREAD_PROCESS_SHARED); // Share between unrelated processes.
#ifndef __APPLE__
                    ::pthread_mutexattr_setrobust(&mutexAttribute, PTHREAD_MUTEX_ROBUST); // Allow continuation of other processes waiting for this mutex
                                                                                          // when the currently holding process unexpectedly terminates.
#endif
                    ::pthread_mutexattr_settype(&mutexAttribute, PTHREAD_MUTEX_NORMAL); // Using regular mutex with deadlock behavior.
                    ::pthread_mutex_init(&(m_sharedMemoryHeader->__mutex), &mutexAttribute);
                    ::pthread_mutexattr_destroy(&mutexAttribute);

                    // Create shared condition.
                    pthread_condattr_t conditionAttribute;
                    ::pthread_condattr_init(&conditionAttribute);
#ifndef __APPLE__
                    ::pthread_condattr_setclock(&conditionAttribute, CLOCK_MONOTONIC); // Use realtime clock for timed waits with non-negative jumps.
#endif
                    ::pthread_condattr_setpshared(&conditionAttribute, PTHREAD_PROCESS_SHARED); // Share between unrelated processes.
                    ::pthread_cond_init(&(m_sharedMemoryHeader->__condition), &conditionAttribute);
                    ::pthread_condattr_destroy(&conditionAttribute);
                } else {
                    // Indicate that this instance is attaching to an existing shared memory segment.
                    m_hasOnlyAttachedToSharedMemory = true;

                    // Read size as we are attaching to an existing shared memory.
                    m_size = m_sharedMemoryHeader->__size;

                    // Now, as we know the real size, unmap the first mapping that did not know the size.
                    if (::munmap(m_sharedMemory, sizeof(SharedMemoryHeader))) {
                        std::cerr << "[cluon::SharedMemory (POSIX)] Failed to unmap shared memory: " << ::strerror(errno) << " (" << errno << ")" // LCOV_EXCL_LINE
                                  << std::endl;                                                                                           // LCOV_EXCL_LINE
                    }

                    // Invalidate all pointers.
                    m_sharedMemory       = nullptr;
                    m_sharedMemoryHeader = nullptr;

                    // Re-map with the correct size parameter.
                    m_sharedMemory = static_cast<char *>(::mmap(0, sizeof(SharedMemoryHeader) + m_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0));
                    if (MAP_FAILED != m_sharedMemory) {
                        m_sharedMemoryHeader = reinterpret_cast<SharedMemoryHeader *>(m_sharedMemory);
                    }
                }
            } else {                                                                                                                 // LCOV_EXCL_LINE
                std::cerr << "[cluon::SharedMemory (POSIX)] Failed to map '" << m_name << "': " << ::strerror(errno) << " (" << errno << ")" // LCOV_EXCL_LINE
                          << std::endl;                                                                                              // LCOV_EXCL_LINE
            }

            // If the shared memory segment is correctly available, store the pointer for the user data.
            if (MAP_FAILED != m_sharedMemory) {
                m_userAccessibleSharedMemory = m_sharedMemory + sizeof(SharedMemoryHeader);

                // Lock the shared memory into RAM for performance reasons.
                if (-1 == ::mlock(m_sharedMemory, sizeof(SharedMemoryHeader) + m_size)) {
                    std::cerr << "[cluon::SharedMemory (POSIX)] Failed to mlock shared memory: " // LCOV_EXCL_LINE
                              << ::strerror(errno) << " (" << errno << ")" << std::endl; // LCOV_EXCL_LINE
                }
            }
        } else {                                                                                                                       // LCOV_EXCL_LINE
            if (-1 != m_fd) {                                                                                                          // LCOV_EXCL_LINE
                if (-1 == ::shm_unlink(m_name.c_str())) {                                                                              // LCOV_EXCL_LINE
                    std::cerr << "[cluon::SharedMemory (POSIX)] Failed to unlink shared memory: " << ::strerror(errno) << " (" << errno << ")" // LCOV_EXCL_LINE
                              << std::endl;                                                                                            // LCOV_EXCL_LINE
                }
            }
            m_fd = -1; // LCOV_EXCL_LINE
        }
    }
}

void SharedMemory::deinitPOSIX() noexcept {
    if ((nullptr != m_sharedMemoryHeader) && (!m_hasOnlyAttachedToSharedMemory)) {
        // Wake any waiting threads as we are going to end the shared memory session.
        ::pthread_cond_broadcast(&(m_sharedMemoryHeader->__condition));
        ::pthread_cond_destroy(&(m_sharedMemoryHeader->__condition));
        ::pthread_mutex_destroy(&(m_sharedMemoryHeader->__mutex));
    }
    if ((nullptr != m_sharedMemory) && ::munmap(m_sharedMemory, sizeof(SharedMemoryHeader) + m_size)) {
        std::cerr << "[cluon::SharedMemory (POSIX)] Failed to unmap shared memory: " << ::strerror(errno) << " (" << errno << ")" << std::endl; // LCOV_EXCL_LINE
    }
    if (!m_hasOnlyAttachedToSharedMemory && (-1 != m_fd) && (-1 == ::shm_unlink(m_name.c_str()) && (ENOENT != errno))) {
        std::cerr << "[cluon::SharedMemory (POSIX)] Failed to unlink shared memory: " << ::strerror(errno) << " (" << errno << ")" << std::endl; // LCOV_EXCL_LINE
    }
}

void SharedMemory::lockPOSIX() noexcept {
    if (nullptr != m_sharedMemoryHeader) {
        if (EOWNERDEAD == ::pthread_mutex_lock(&(m_sharedMemoryHeader->__mutex))) {
            std::cerr << "[cluon::SharedMemory (POSIX)] pthread_mutex_lock returned for EOWNERDEAD for mutex in shared memory '" << m_name // LCOV_EXCL_LINE
                      << "': " << ::strerror(errno)                                                                                // LCOV_EXCL_LINE
                      << " (" << errno << ")" << std::endl;                                                                        // LCOV_EXCL_LINE
        }
    }
}

void SharedMemory::unlockPOSIX() noexcept {
    if (nullptr != m_sharedMemoryHeader) {
        ::pthread_mutex_unlock(&(m_sharedMemoryHeader->__mutex));
    }
}

void SharedMemory::waitPOSIX() noexcept {
    if (nullptr != m_sharedMemoryHeader) {
        lock();
        ::pthread_cond_wait(&(m_sharedMemoryHeader->__condition), &(m_sharedMemoryHeader->__mutex));
        unlock();
    }
}

void SharedMemory::notifyAllPOSIX() noexcept {
    if (nullptr != m_sharedMemoryHeader) {
        ::pthread_cond_broadcast(&(m_sharedMemoryHeader->__condition));
    }
}

bool SharedMemory::validPOSIX() noexcept {
    return (-1 != m_fd) && (MAP_FAILED != m_sharedMemory);
}

void SharedMemory::initSysV() noexcept {
    // If size is greater than 0, the caller wants to create a new shared
    // memory area. Otherwise, the caller wants to open an existing shared memory.
    int flags = O_RDWR;
    if (0 < m_size) {
        flags |= O_CREAT | O_EXCL;
    }

    m_fd = ::shm_open(m_name.c_str(), flags, S_IRUSR | S_IWUSR);
    if (-1 == m_fd) {
        std::cerr << "[cluon::SharedMemory (SysV)] Failed to open shared memory '" << m_name << "': " << ::strerror(errno) << " (" << errno << ")" << std::endl;
        // Try to remove existing shared memory segment and try again.
        if ((flags & O_CREAT) == O_CREAT) {
            std::clog << "[cluon::SharedMemory (SysV)] Trying to remove existing shared memory '" << m_name << "' and trying again... ";
            if (0 == ::shm_unlink(m_name.c_str())) {
                m_fd = ::shm_open(m_name.c_str(), flags, S_IRUSR | S_IWUSR);
            }

            if (-1 == m_fd) {
                std::cerr << "failed: " << ::strerror(errno) << " (" << errno << ")" << std::endl; // LCOV_EXCL_LINE
            } else {
                std::cerr << "succeeded." << std::endl;
            }
        }
    }

    if (-1 != m_fd) {
        bool retVal{true};

        // When creating a shared memory segment, truncate it.
        if (0 < m_size) {
            retVal = (0 == ::ftruncate(m_fd, static_cast<off_t>(sizeof(SharedMemoryHeader) + m_size)));
            if (!retVal) {
                std::cerr << "[cluon::SharedMemory (SysV)] Failed to truncate '" << m_name << "': " << ::strerror(errno) << " (" << errno << ")" // LCOV_EXCL_LINE
                          << std::endl;                                                                                                   // LCOV_EXCL_LINE
            }
        }

        // Accessing shared memory segment.
        if (retVal) {
            // On opening (i.e., NOT creating) a shared memory segment, m_size is still 0 and we need to figure out the size first.
            m_sharedMemory = static_cast<char *>(::mmap(0, sizeof(SharedMemoryHeader) + m_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0));
            if (MAP_FAILED != m_sharedMemory) {
                m_sharedMemoryHeader = reinterpret_cast<SharedMemoryHeader *>(m_sharedMemory);

                // On creating (i.e., NOT opening) a shared memory segment, setup the shared memory header.
                if (0 < m_size) {
                    // Store user accessible size in shared memory.
                    m_sharedMemoryHeader->__size = m_size;

                    // Create process-shared mutex (fastest approach, cf. Stevens & Rago: "Advanced Programming in the UNIX (R) Environment").
                    pthread_mutexattr_t mutexAttribute;
                    ::pthread_mutexattr_init(&mutexAttribute);
                    ::pthread_mutexattr_setpshared(&mutexAttribute, PTHREAD_PROCESS_SHARED); // Share between unrelated processes.
#ifndef __APPLE__
                    ::pthread_mutexattr_setrobust(&mutexAttribute, PTHREAD_MUTEX_ROBUST); // Allow continuation of other processes waiting for this mutex
                                                                                          // when the currently holding process unexpectedly terminates.
#endif
                    ::pthread_mutexattr_settype(&mutexAttribute, PTHREAD_MUTEX_NORMAL); // Using regular mutex with deadlock behavior.
                    ::pthread_mutex_init(&(m_sharedMemoryHeader->__mutex), &mutexAttribute);
                    ::pthread_mutexattr_destroy(&mutexAttribute);

                    // Create shared condition.
                    pthread_condattr_t conditionAttribute;
                    ::pthread_condattr_init(&conditionAttribute);
#ifndef __APPLE__
                    ::pthread_condattr_setclock(&conditionAttribute, CLOCK_MONOTONIC); // Use realtime clock for timed waits with non-negative jumps.
#endif
                    ::pthread_condattr_setpshared(&conditionAttribute, PTHREAD_PROCESS_SHARED); // Share between unrelated processes.
                    ::pthread_cond_init(&(m_sharedMemoryHeader->__condition), &conditionAttribute);
                    ::pthread_condattr_destroy(&conditionAttribute);
                } else {
                    // Indicate that this instance is attaching to an existing shared memory segment.
                    m_hasOnlyAttachedToSharedMemory = true;

                    // Read size as we are attaching to an existing shared memory.
                    m_size = m_sharedMemoryHeader->__size;

                    // Now, as we know the real size, unmap the first mapping that did not know the size.
                    if (::munmap(m_sharedMemory, sizeof(SharedMemoryHeader))) {
                        std::cerr << "[cluon::SharedMemory (SysV)] Failed to unmap shared memory: " << ::strerror(errno) << " (" << errno << ")" // LCOV_EXCL_LINE
                                  << std::endl;                                                                                           // LCOV_EXCL_LINE
                    }

                    // Invalidate all pointers.
                    m_sharedMemory       = nullptr;
                    m_sharedMemoryHeader = nullptr;

                    // Re-map with the correct size parameter.
                    m_sharedMemory = static_cast<char *>(::mmap(0, sizeof(SharedMemoryHeader) + m_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0));
                    if (MAP_FAILED != m_sharedMemory) {
                        m_sharedMemoryHeader = reinterpret_cast<SharedMemoryHeader *>(m_sharedMemory);
                    }
                }
            } else {                                                                                                                 // LCOV_EXCL_LINE
                std::cerr << "[cluon::SharedMemory (SysV)] Failed to map '" << m_name << "': " << ::strerror(errno) << " (" << errno << ")" // LCOV_EXCL_LINE
                          << std::endl;                                                                                              // LCOV_EXCL_LINE
            }

            // If the shared memory segment is correctly available, store the pointer for the user data.
            if (MAP_FAILED != m_sharedMemory) {
                m_userAccessibleSharedMemory = m_sharedMemory + sizeof(SharedMemoryHeader);

                // Lock the shared memory into RAM for performance reasons.
                if (-1 == ::mlock(m_sharedMemory, sizeof(SharedMemoryHeader) + m_size)) {
                    std::cerr << "[cluon::SharedMemory (SysV)] Failed to mlock shared memory: " // LCOV_EXCL_LINE
                              << ::strerror(errno) << " (" << errno << ")" << std::endl; // LCOV_EXCL_LINE
                }
            }
        } else {                                                                                                                       // LCOV_EXCL_LINE
            if (-1 != m_fd) {                                                                                                          // LCOV_EXCL_LINE
                if (-1 == ::shm_unlink(m_name.c_str())) {                                                                              // LCOV_EXCL_LINE
                    std::cerr << "[cluon::SharedMemory (SysV)] Failed to unlink shared memory: " << ::strerror(errno) << " (" << errno << ")" // LCOV_EXCL_LINE
                              << std::endl;                                                                                            // LCOV_EXCL_LINE
                }
            }
            m_fd = -1; // LCOV_EXCL_LINE
        }
    }
}

void SharedMemory::deinitSysV() noexcept {
    if ((nullptr != m_sharedMemoryHeader) && (!m_hasOnlyAttachedToSharedMemory)) {
        // Wake any waiting threads as we are going to end the shared memory session.
        ::pthread_cond_broadcast(&(m_sharedMemoryHeader->__condition));
        ::pthread_cond_destroy(&(m_sharedMemoryHeader->__condition));
        ::pthread_mutex_destroy(&(m_sharedMemoryHeader->__mutex));
    }
    if ((nullptr != m_sharedMemory) && ::munmap(m_sharedMemory, sizeof(SharedMemoryHeader) + m_size)) {
        std::cerr << "[cluon::SharedMemory (SysV)] Failed to unmap shared memory: " << ::strerror(errno) << " (" << errno << ")" << std::endl; // LCOV_EXCL_LINE
    }
    if (!m_hasOnlyAttachedToSharedMemory && (-1 != m_fd) && (-1 == ::shm_unlink(m_name.c_str()) && (ENOENT != errno))) {
        std::cerr << "[cluon::SharedMemory (SysV)] Failed to unlink shared memory: " << ::strerror(errno) << " (" << errno << ")" << std::endl; // LCOV_EXCL_LINE
    }
}

void SharedMemory::lockSysV() noexcept {
    if (nullptr != m_sharedMemoryHeader) {
        if (EOWNERDEAD == ::pthread_mutex_lock(&(m_sharedMemoryHeader->__mutex))) {
            std::cerr << "[cluon::SharedMemory (SysV)] pthread_mutex_lock returned for EOWNERDEAD for mutex in shared memory '" << m_name // LCOV_EXCL_LINE
                      << "': " << ::strerror(errno)                                                                                // LCOV_EXCL_LINE
                      << " (" << errno << ")" << std::endl;                                                                        // LCOV_EXCL_LINE
        }
    }
}

void SharedMemory::unlockSysV() noexcept {
    if (nullptr != m_sharedMemoryHeader) {
        ::pthread_mutex_unlock(&(m_sharedMemoryHeader->__mutex));
    }
}

void SharedMemory::waitSysV() noexcept {
    if (nullptr != m_sharedMemoryHeader) {
        lock();
        ::pthread_cond_wait(&(m_sharedMemoryHeader->__condition), &(m_sharedMemoryHeader->__mutex));
        unlock();
    }
}

void SharedMemory::notifyAllSysV() noexcept {
    if (nullptr != m_sharedMemoryHeader) {
        ::pthread_cond_broadcast(&(m_sharedMemoryHeader->__condition));
    }
}

bool SharedMemory::validSysV() noexcept {
    return (-1 != m_fd) && (MAP_FAILED != m_sharedMemory);
}
#endif

} // namespace cluon
