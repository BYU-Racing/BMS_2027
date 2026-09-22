#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BMS_STATE_IDLE,
    BMS_STATE_READY,
    BMS_STATE_RUNNING,
    BMS_STATE_CHARGING,
    BMS_STATE_FAULT
} BmsState;

typedef struct {
    //State IDLE
    bool isoSpiConnected;
    bool modulesPresent;
    bool tempSensorsPresent;
    bool voltageSensorsPresent;
    bool canConnected;

    //State READY
    bool chargingRequested;

    //State READY and RUNNING and CHARGING
    bool voltagesGood;
    bool tempsGood;

    //State FAULT
    bool resetRequested;

} BmsInputs;

typedef struct {
    BmsState state;
    bool shutdownCircuitClosed;

    //a permission signal, checks if it is currently safe to balance at all
    //balancing logic can check canBalance before doing anything else
    bool canBalance;
} BmsOutputs;

//FSM runs every 10ms so it has to remember waht state it was in from the previous call or it will never transition
//variable declared inside a function will only live for that one call
//here the caller owns and hands in the memory that lives outside the function each time
typedef struct {
    BmsState state;
    //unsigned always 32 bits. Should it stay this type? Just the standard one C typically uses
    //will count down once per 10ms tick while Idle is waiting on a connection
    //when it hits 0, retry the checks and if still not connected set it back to 10 and keep waiting
    uint32_t idleRetryTicks;
} BmsContext;

//function that will run once every 10ms and does one full check and decide what is happening cycle for the whole state machine
/*
Every time it's called:
-Looks at where it currently is (ctx the state left over from last time)
-Looks at what's true right now (inputs)
-Decides whether to change state, updates ctx if yes
-Fills in what the rest of the system should do (outputs)
*/
void Control_Step(BmsContext *ctx, const BmsInputs *inputs, BmsOutputs *outputs);