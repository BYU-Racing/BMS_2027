#include "include/control.h"

void BMSControl::updateState(State bmsState) {
    switch (State) {
        case State::Idle:
            if (!isoSpiConnected) {
                /* wait until connected */
                break;
            }
            else if (!canConnected) {
                /* wait until connected */
                break;
            }
            else if (!moduleConnected) {
                bmsState = State::Fault;
            }
            else if (!tempSensorsConnected) {
                bmsState = State::Fault;
            }
            else if (!voltageSensorsConnected) {
                bmsState = State::Fault;
            }      
            else {
                bmsState = State::Ready;
            } 
            break;
        case State::Ready:
            if (tempStatus == tempStatus::Error || tempStatus == tempStatus::Disconnected) {
                bmsState = State::Fault;
            }
            else if (voltageStatusStatus == voltageStatus::Error || voltageStatusStatus == voltageStatus::Disconnected) {
                bmsState = State::Fault;
            }
            // TODO check if charging state is requested
            else {
                bmsState = State::Running;
            }
            break;
        case State::Running:
        case State::Charging:
            if (tempStatus == tempStatus::Error || tempStatus == tempStatus::Disconnected) {
                bmsState = State::Fault;
            }
            if (voltageStatusStatus == voltageStatus::Error || voltageStatusStatus == voltageStatus::Disconnected) {
                bmsState = State::Fault;
            }
            break;
        case State::Fault:
            // TODO pull shutdown pin
            break;
        default:
            break;
    }
}