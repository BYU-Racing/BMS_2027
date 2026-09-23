#include "include/control.h"

void Control_Step(BmsContext *ctx, const BmsInputs *inputs, BmsOutputs *outputs) {
    
    //this switch statement decides the state
    switch (ctx -> state) {

        case BMS_STATE_IDLE:
        //state changes to ready if everything is true
        //if sensors are missing after connecting go to fault
        //if still waiting in isoSPI or CAN stay in Idle and manage retry timer
        //fill in outputs before this case ends: state, shutdown circuit, balancing
            if (inputs -> isoSpiConnected && inputs -> canConnected){
                if (inputs -> modulesPresent && inputs -> tempSensorsPresent && inputs ->voltageSensorsPresent){
                    ctx -> state = BMS_STATE_READY;
                } else {
                    ctx -> state = BMS_STATE_FAULT;
                }
            } else {
                //retry timer
                //counts down to 0
                if (ctx -> idleRetryTicks > 0){
                    ctx -> idleRetryTicks--;
                } else {
                    //count from 10
                    ctx -> idleRetryTicks = 10;
                }
            }
            break;

        case BMS_STATE_READY:
        //if volatages or temps are bad state changes to fault
        //if voltages and temps are good and charging is requested state changes to charging
        //if voltages and temps are good and charging is not requested state changes to running
            if (inputs -> voltagesGood && inputs -> tempsGood) {
                if (inputs -> chargingRequested) {
                    ctx -> state = BMS_STATE_CHARGING;
                } else {
                    ctx -> state = BMS_STATE_RUNNING;
                }
            } else {
                ctx -> state = BMS_STATE_FAULT;
            }
            break;

        case BMS_STATE_RUNNING:
        //if voltages AND temps are good stay in Running
        //if voltages OR temps are bad state changes to fault
            if (inputs -> voltagesGood && inputs -> tempsGood){
                ctx -> state = BMS_STATE_RUNNING;
            } else {
                ctx -> state = BMS_STATE_FAULT;
            }
            break;

        case BMS_STATE_CHARGING:
        //if voltages AND temps are good stay in Charging
        //if voltages OR temps are bad state changes to fault
            if (inputs -> voltagesGood && inputs -> tempsGood){
                ctx -> state = BMS_STATE_CHARGING;
            } else {
                ctx -> state = BMS_STATE_FAULT;
            }
            break;

        //QUESTION: What happens when charging is done? Are the only state options staying in charging or moving to fault?

        case BMS_STATE_FAULT:
            if (inputs -> resetRequested) {
                ctx -> state = BMS_STATE_IDLE;
            }
            break;
    }

    //QUESTION: which states are safe to balance in?

    //this switch statement decides the outputs
    switch (ctx->state) {
        case BMS_STATE_IDLE:
            outputs -> state = ctx -> state;
            outputs -> shutdownCircuitClosed = false;
            outputs -> canBalance = false;
            break;
        case BMS_STATE_READY:
            outputs -> state = ctx -> state;
            outputs -> shutdownCircuitClosed = true;
            outputs -> canBalance = true;
            break;
        case BMS_STATE_RUNNING:
            outputs -> state = ctx -> state;
            outputs -> shutdownCircuitClosed = true;
            outputs -> canBalance = true;
            break;
        case BMS_STATE_CHARGING:
            outputs -> state = ctx -> state;
            outputs -> shutdownCircuitClosed = true;
            outputs -> canBalance = true;
            break;
        case BMS_STATE_FAULT:
            outputs -> state = ctx -> state;
            outputs -> shutdownCircuitClosed = false;
            outputs -> canBalance = false;
            break;
    }
}