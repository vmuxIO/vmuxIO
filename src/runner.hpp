#pragma once

#include <thread>
#include <atomic>
#include <optional>
#include <memory>
#include "src/device.hpp"
#include "src/vfio-server.hpp"


class VmuxRunner{
    public: 
        VfioUserServer vfu;
        std::shared_ptr<VmuxDevice> device;
        std::thread runner;
        std::shared_ptr<Capabilities> caps;
        std::atomic_int state;
        std::atomic_bool running;
        std::string socket;

        //void (*VmuxRunner::interrupt_handler)(void);

        enum State{
            NOT_STARTED = 0,
            STARTED = 1,
            INITILIZED = 2,
            CONNECTED = 3,
        };

        VmuxRunner(std::string socket, std::shared_ptr<VmuxDevice> device, 
                int efd): vfu(socket,efd), device(device) {
            state.store(0);
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
        void add_caps(std::shared_ptr<VfioConsumer> vfioc);
        void initilize();
};
