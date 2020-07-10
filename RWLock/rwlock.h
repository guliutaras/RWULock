#ifndef _RW_LOCK_HPP_
#define _RW_LOCK_HPP_

#include <condition_variable>
#include <mutex>
#include <atomic>

namespace concurency {

    class UpgradableMutex
{
    std::mutex              mut_;
    std::condition_variable gate1_;
    std::condition_variable gate2_;
    unsigned                state_;

    static const unsigned write_entered_ = 1U << (sizeof(unsigned)*CHAR_BIT - 1);
    static const unsigned upgradable_entered_ = write_entered_ >> 1;
    static const unsigned n_readers_ = ~(write_entered_ | upgradable_entered_);

protected:


    bool try_unlock_shared_and_lock_upgrade()
    {
        std::unique_lock<std::mutex> lk(mut_);
        if (!(state_ & (write_entered_ | upgradable_entered_)))
        {
            state_ |= upgradable_entered_;
            return true;
        }
        return false;
    }
    void unlock_upgrade_and_lock()
    {
        std::unique_lock<std::mutex> lk(mut_);
        unsigned num_readers = (state_ & n_readers_) - 1;
        state_ &= ~(upgradable_entered_ | n_readers_);
        state_ |= write_entered_ | num_readers;
        while (state_ & n_readers_)
            gate2_.wait(lk);
    }
public:
    UpgradableMutex(): state_(0) {}
    ~UpgradableMutex() = default;

    UpgradableMutex(const UpgradableMutex&) = delete;
    UpgradableMutex& operator=(const UpgradableMutex&) = delete;

    // Exclusive ownership

    void lock()
    {
        std::unique_lock<std::mutex> lk(mut_);
        while (state_ & (write_entered_ | upgradable_entered_))
            gate1_.wait(lk);
        state_ |= write_entered_;
        while (state_ & n_readers_)
            gate2_.wait(lk);
    }
    void unlock()
    {
        std::lock_guard<std::mutex> _(mut_);
        state_ = 0;
        gate1_.notify_all();
    }

    // Shared ownership

    void lock_shared()
    {
        std::unique_lock<std::mutex> lk(mut_);
        while ((state_ & write_entered_) || (state_ & n_readers_) == n_readers_)
            gate1_.wait(lk);
        unsigned num_readers = (state_ & n_readers_) + 1;
        state_ &= ~n_readers_;
        state_ |= num_readers;
    }
    void unlock_shared()
    {
        std::lock_guard<std::mutex> _(mut_);
        unsigned num_readers = (state_ & n_readers_) - 1;
        state_ &= ~n_readers_;
        state_ |= num_readers;
        if (state_ & write_entered_)
        {
            if (num_readers == 0)
                gate2_.notify_one();
        }
        else
        {
            if (num_readers == n_readers_ - 1)
                gate1_.notify_one();
        }
    }

     // Upgrade ownership

    void lock_upgrade()
    {
        std::unique_lock<std::mutex> lk(mut_);
        while ((state_ & (write_entered_ | upgradable_entered_)) ||
            (state_ & n_readers_) == n_readers_)
            gate1_.wait(lk);
        unsigned num_readers = (state_ & n_readers_) + 1;
        state_ &= ~n_readers_;
        state_ |= upgradable_entered_ | num_readers;
    }

    void unlock_upgrade()
    {
        {
            std::lock_guard<std::mutex> _(mut_);
            unsigned num_readers = (state_ & n_readers_) - 1;
            state_ &= ~(upgradable_entered_ | n_readers_);
            state_ |= num_readers;
        }
        gate1_.notify_all();
    }

    bool try_upgrade()
    {
        if(try_unlock_shared_and_lock_upgrade())
        {
            unlock_upgrade_and_lock();
            return true;
        }
        return false;
    }
};


    enum RWULock_state
    {
        defer_state = 0,
        shared_state = 1,
        exclusive_state = 2,
        upgrade_state = 3
    };

    class RWULock {
        UpgradableMutex* mutex_;
        RWULock_state m_state;
    public:
        RWULock(UpgradableMutex &mutex, RWULock_state init_state = defer_state) : mutex_(&mutex), m_state(init_state)
        {
            switch(init_state)
            {
            case shared_state:
                LockRead();
                break;
            case exclusive_state:
                LockWrite();
                break;
            }
        }
        void LockWrite()
        {
            if (m_state == exclusive_state)
            {
                return;
            }
            if (m_state != defer_state)
            {
                throw std::exception("While trying to acquire unique lock, invalid state of upgrade_lock found. State was: " + m_state);
            }
            mutex_->lock();
            m_state = exclusive_state;
        }

        void LockRead()
        {
            if (m_state == shared_state)
            {
                return;
            }
            if (m_state != defer_state)
            {
                throw std::exception("While trying to acquire shared lock, invalid state of upgrade_lock found. State was: " + m_state);
            }
            mutex_->lock_shared();
            m_state = shared_state;
        }
        void TryUpgrade()
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
                mutex_->unlock_shared();
            }
            if(mutex_->try_upgrade())
            {
                m_state = exclusive_state;
            }
            else
            {
                throw std::exception("While trying to upgrade shared lock, invalid state of upgrade_lock found. State was: " + m_state);
            }
        }

        void Unlock()
        {
            switch (m_state)
            {
            case shared_state:
                mutex_->unlock_shared();
                m_state = defer_state;
                break;
            case exclusive_state:
                mutex_->unlock();
                m_state = defer_state;
                break;
            case upgrade_state:
                mutex_->unlock_upgrade();
                m_state = defer_state;
                break;
            }
        }
        ~RWULock() {
            Unlock();
        }
    };
};

#endif