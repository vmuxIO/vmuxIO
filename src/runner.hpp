#pragma once
#include "src/vfio-server.hpp"
#include <thread>
#include <atomic>
#include <optional>
class VmuxRunner{

    public: 
    VfioUserServer vfu;
    VfioConsumer& vfioc;
    std::thread runner;
    std::optional<Capabilities> caps;
    std::atomic_int state;
    std::atomic_bool running;
    std::string device;
    std::string socket;
    //void (*VmuxRunner::interrupt_handler)(void);

    enum State{
        NOT_STARTED = 0,
        STARTED = 1,
        INITILIZED = 2,
        CONNECTED = 3,
    };

    VmuxRunner(std::string socket, std::string device, VfioConsumer& vfioc, int efd):
        vfu(socket,efd),vfioc(vfioc)
    {
        state.store(0);
        this->device = device;
        this->socket = socket;
    }
        
    void start(){
        runner = std::thread(&VmuxRunner::run, this);
    }

    void stop(){
        running.store(0);
    }
    
    void join(){
        runner.join();
    }

    bool is_initialized(){
        return state == INITILIZED;
    }
    bool is_connected(){
        return state == CONNECTED;
    }
    VfioUserServer& get_interrupts(){
        return vfu;
    }

    private:
    void run();
    void add_caps();
    void initilize();


};