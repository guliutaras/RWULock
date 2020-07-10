#pragma once
#include <Windows.h>
#include <exception>
#include <atomic>

namespace rip_parallel
{

    class upgrade_mutex
    {
    public:
        // Constructs upgrade_mutex object.
        upgrade_mutex(void) noexcept
            : m_readers(0), m_upgraders(0)
        {
            InitializeSRWLock(&m_sharedlock);

            m_mutex = CreateMutex(nullptr, FALSE, nullptr);

            // We need synchronization event as a barrier that will be set by the owner of shared lock once it needs upgrade.
            m_readevent = CreateEvent(nullptr, TRUE, TRUE, nullptr);
        }
        // Destroys upgrade_mutex object.
        ~upgrade_mutex(void)
        {
            // Once the object is marked for destruction - set the event and close the handle.
            SetEvent(m_readevent);
            CloseHandle(m_readevent);
        }
        // Acquires shared access over the lock. Suspends the calling thread until lock is obtained.
        void lock_shared(void)
        {
            // Request READ access.
            AcquireSRWLockShared(&m_sharedlock);

            // Once acquired, increment readers count.
            m_readers++;
        }
        // Releases shared access over the lock.
        void unlock_shared(void)
        {
            // Release READ access.
            ReleaseSRWLockShared(&m_sharedlock);

            // Once released, decrement readers count.
            m_readers--;
        }
        // Acquires exclusive access over the lock. Suspends the calling thread until lock is obtained.
        void lock(void)
        {
            // Request WRITE access.
            AcquireSRWLockExclusive(&m_sharedlock);
        }
        // Releases exclusive access over the lock.
        void unlock(void)
        {
            // Release WRITE access.
            ReleaseSRWLockExclusive(&m_sharedlock);
        }
        // Waits until shared access over the lock is disabled.
        void wait_read(void)
        {
            // Each thread that wants READ access, has to wait for read to be enabled first.
            // This will enable the thread that wants to acquire upgraded lock to disable further readers while upgrade is active.
            // Writers are not involved in this wait mechanism, cause once at least one thread has shared access, writers are suspended.

            // Wait infinite.
            WaitForSingleObject(m_readevent, INFINITE);
        }
        // Enables shared access over the lock.
        void enable_read(void)
        {
            // Since current thread has upgraded access type, we have to update readers count, since it'll be decremented in unlock_shared.
            m_readers++;

            // We have to keep track of upgraders count, in order to enable read ONLY once all upgarders have completed.
            m_upgraders--;

            if (m_upgraders == 0)
            {
                // Once all upgraders have completed W operation, enable readers.
                SetEvent(m_readevent);
            }
        }
        // Disables shared access over the lock.
        void disable_read(void)
        {
            // The thread that wants to upgrade access, has to disable further read access.
            // It has to reset the event and disable other threads to reach acquiring mutex - otherwise we would deadlock.
            if (m_upgraders == 0)
            {
                // If there are no other upgraders at the moment - reset the event. Otherwise, it's already in non-signaled state.
                ResetEvent(m_readevent);
            }

            // Since current thread is upgrading access type, we have to reduce readers count.
            m_readers--;

            // We have to keep track of upgraders count, in order to enable read ONLY once all upgarders have completed.
            m_upgraders++;
        }
        // Returns active readers count.
        int readers_count(void)
        {
            // Getactual readers count.
            return m_readers;
        }
        // Synchronizes all threads that are requesting upgrade in between, by allowing one writer at a time.
        void upgrade(void)
        {
            // Once we have upgraded our state, we need to acquire exclusive access.
            WaitForSingleObject(m_mutex, INFINITE);
        }
        // Synchronizes all threads that are requesting upgrade in between, by allowing one writer at a time.
        void downgrade(void)
        {
            // Once we have completed exclusive operation we have to release exclusive access.
            ReleaseMutex(m_mutex);
        }
    private:
        SRWLOCK m_sharedlock;
        HANDLE m_mutex;
        HANDLE m_readevent;
        volatile std::atomic<int> m_readers;
        volatile std::atomic<int> m_upgraders;
    };

    enum upgrade_lock_state
    {
        defer_state = 0,
        shared_state = 1,
        exclusive_state = 2,
        upgrade_state = 3
    };

    class upgrade_lock
    {
    public:
        upgrade_lock(upgrade_mutex& ref_mutex, upgrade_lock_state initial_state = defer_state)
            : m_mutex(ref_mutex), m_state(defer_state)
        {
            switch (initial_state)
            {
            case rip_parallel::shared_state:
                lock_shared();
                break;
            case rip_parallel::exclusive_state:
            case rip_parallel::upgrade_state:
                lock_unique();
                break;
            }
        }
        ~upgrade_lock(void)
        {
            unlock();
        }
    public:
        upgrade_lock(const upgrade_lock&) = delete;
        upgrade_lock(upgrade_lock&&) = delete;
    public:
        upgrade_lock& operator=(const upgrade_lock&) = delete;
        upgrade_lock& operator=(upgrade_lock&&) = delete;
    public:
        void unlock(void)
        {
            switch (m_state)
            {
            case rip_parallel::shared_state:
                m_mutex.unlock_shared();
                m_state = defer_state;
                break;
            case rip_parallel::exclusive_state:
                m_mutex.unlock();
                m_state = defer_state;
                break;
            case rip_parallel::upgrade_state:
                m_mutex.downgrade();
                m_mutex.enable_read();

                m_mutex.unlock_shared();
                m_state = defer_state;
                break;
            }
        }
        void lock_unique(void)
        {
            if (m_state == rip_parallel::exclusive_state)
            {
                return;
            }
            if (m_state != rip_parallel::defer_state)
            {
                throw std::exception("While trying to acquire unique lock, invalid state of upgrade_lock found. State was: " + m_state);
            }

            m_mutex.lock();
            m_state = rip_parallel::exclusive_state;
        }
        void lock_shared(void)
        {
            if (m_state == rip_parallel::shared_state)
            {
                return;
            }
            if (m_state != rip_parallel::defer_state)
            {
                throw std::exception("While trying to acquire shared lock, invalid state of upgrade_lock found. State was: " + m_state);
            }

            m_mutex.wait_read();

            m_mutex.lock_shared();
            m_state = rip_parallel::shared_state;
        }
        void lock_upgrade(void)
        {
            if (m_state == upgrade_state)
            {
                return;
            }
            else if (m_state == exclusive_state)
            {
                throw std::exception("While trying to upgrade shared lock, invalid state of upgrade_lock found. State was: " + m_state);
            }
            else if (m_state == defer_state)
            {
                m_mutex.lock_shared();
            }
            m_state = rip_parallel::upgrade_state;

            m_mutex.disable_read();
            while (m_mutex.readers_count())
            {
                Sleep(10);
            }

            m_mutex.upgrade();
            // DO THE JOB
        }
    private:
        upgrade_mutex& m_mutex;
        upgrade_lock_state m_state;
    };
};