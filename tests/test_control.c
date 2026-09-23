// Host-side tests for the BMS control state machine.
// Compiled and run on a laptop with plain `cc` -- never part of the STM32
// firmware build, and never needs the ARM toolchain. Purely checks the logic
// in Core/Src/application/control.c against fake inputs.
//
// Run it with:
//   cc -std=c11 -Wall -Wextra -Wpedantic \
//      -I ../Core/Inc \
//      test_control.c ../Core/Src/application/control.c \
//      -o /tmp/test_control && /tmp/test_control
// (run that from inside the tests/ folder)

#include <assert.h>
#include <stdio.h>
#include "include/control.h"

static void test_idle_stays_idle_when_nothing_connected(void) {
    BmsContext ctx = { .state = BMS_STATE_IDLE, .idleRetryTicks = 10 };
    BmsInputs inputs = { 0 };
    BmsOutputs outputs;

    Control_Step(&ctx, &inputs, &outputs);

    assert(ctx.state == BMS_STATE_IDLE);
}

static void test_idle_stays_idle_when_isoSpi_not_connected(void) {
    BmsContext ctx = { .state = BMS_STATE_IDLE, .idleRetryTicks = 10 };
    BmsInputs inputs = { 
        .isoSpiConnected = false,
        .canConnected = true,
        .modulesPresent = true,
        .tempSensorsPresent = true,
        .voltageSensorsPresent = true,
     };
    BmsOutputs outputs;

    Control_Step(&ctx, &inputs, &outputs);

    assert(ctx.state == BMS_STATE_IDLE);
}

static void test_idle_stays_idle_when_can_not_connected(void) {
    BmsContext ctx = { .state = BMS_STATE_IDLE, .idleRetryTicks = 10 };
    BmsInputs inputs = { 
        .isoSpiConnected = true,
        .canConnected = false,
        .modulesPresent = true,
        .tempSensorsPresent = true,
        .voltageSensorsPresent = true,
     };
    BmsOutputs outputs;

    Control_Step(&ctx, &inputs, &outputs);

    assert(ctx.state == BMS_STATE_IDLE);
}
    
static void test_idle_moves_to_ready_when_everything_connects(void) {
    BmsContext ctx = { .state = BMS_STATE_IDLE, .idleRetryTicks = 10 };
    BmsInputs inputs = {
        .isoSpiConnected = true,
        .canConnected = true,
        .modulesPresent = true,
        .tempSensorsPresent = true,
        .voltageSensorsPresent = true,
    };
    BmsOutputs outputs;

    Control_Step(&ctx, &inputs, &outputs);

    assert(ctx.state == BMS_STATE_READY);
}

static void test_idle_moves_to_fault_when_modules_are_missing_after_everything_connects(void) {
    BmsContext ctx = { .state = BMS_STATE_IDLE, .idleRetryTicks = 10 };
    BmsInputs inputs = {
        .isoSpiConnected = true,
        .canConnected = true,
        .modulesPresent = false,
        .tempSensorsPresent = true,
        .voltageSensorsPresent = true,
    };
    BmsOutputs outputs;

    Control_Step(&ctx, &inputs, &outputs);

    assert(ctx.state == BMS_STATE_FAULT);
}

static void test_idle_moves_to_fault_when_voltage_senosrs_are_missing_after_everything_connects(void) {
    BmsContext ctx = { .state = BMS_STATE_IDLE, .idleRetryTicks = 10 };
    BmsInputs inputs = {
        .isoSpiConnected = true,
        .canConnected = true,
        .modulesPresent = true,
        .tempSensorsPresent = true,
        .voltageSensorsPresent = false,
    };
    BmsOutputs outputs;

    Control_Step(&ctx, &inputs, &outputs);

    assert(ctx.state == BMS_STATE_FAULT);
}

static void test_idle_moves_to_fault_when_temp_senosrs_are_missing_after_everything_connects(void) {
    BmsContext ctx = { .state = BMS_STATE_IDLE, .idleRetryTicks = 10 };
    BmsInputs inputs = {
        .isoSpiConnected = true,
        .canConnected = true,
        .modulesPresent = true,
        .tempSensorsPresent = false,
        .voltageSensorsPresent = true,
    };
    BmsOutputs outputs;

    Control_Step(&ctx, &inputs, &outputs);

    assert(ctx.state == BMS_STATE_FAULT);
}

static void test_idle_moves_to_fault_when_senosrs_are_missing_after_everything_connects(void) {
    BmsContext ctx = { .state = BMS_STATE_IDLE, .idleRetryTicks = 10 };
    BmsInputs inputs = {
        .isoSpiConnected = true,
        .canConnected = true,
        .modulesPresent = true,
        .tempSensorsPresent = false,
        .voltageSensorsPresent = false,
    };
    BmsOutputs outputs;

    Control_Step(&ctx, &inputs, &outputs);

    assert(ctx.state == BMS_STATE_FAULT);
}

static void test_idle_moves_to_fault_when_everything_missing_after_everything_connects(void) {
    BmsContext ctx = { .state = BMS_STATE_IDLE, .idleRetryTicks = 10 };
    BmsInputs inputs = {
        .isoSpiConnected = true,
        .canConnected = true,
        .modulesPresent = false,
        .tempSensorsPresent = false,
        .voltageSensorsPresent = false,
    };
    BmsOutputs outputs;

    Control_Step(&ctx, &inputs, &outputs);

    assert(ctx.state == BMS_STATE_FAULT);
}

static void test_idle_retry_timer_counts_down_one_tick(void) {
    BmsContext ctx = { .state = BMS_STATE_IDLE, .idleRetryTicks = 10 };
    BmsInputs inputs = { 0 };
    BmsOutputs outputs;

    Control_Step(&ctx, &inputs, &outputs);

    assert(ctx.idleRetryTicks == 9);
}

static void test_idle_retry_timer_wraps_around_to_ten(void) {
    BmsContext ctx = { .state = BMS_STATE_IDLE, .idleRetryTicks=10};
    BmsInputs inputs = { 0 };
    BmsOutputs outputs;

    for(int i = 0; i<10; i++){
        Control_Step(&ctx, &inputs, &outputs);
    }
    assert(ctx.idleRetryTicks == 0);

    Control_Step(&ctx, &inputs, &outputs);
    assert(ctx.idleRetryTicks == 10);
    
}

static void test_idle_retry_timer_wraps_around_to_ten_when_no_isoSPI(void) {
    BmsContext ctx = { .state = BMS_STATE_IDLE, .idleRetryTicks=10};
    BmsInputs inputs = { 
        .isoSpiConnected = false,
        .canConnected = true,
        .modulesPresent = true,
        .tempSensorsPresent = true,
        .voltageSensorsPresent = true,
    };
    BmsOutputs outputs;

    for(int i = 0; i<10; i++){
        Control_Step(&ctx, &inputs, &outputs);
    }
    assert(ctx.idleRetryTicks == 0);

    Control_Step(&ctx, &inputs, &outputs);
    assert(ctx.idleRetryTicks == 10);
    
}

static void test_idle_retry_timer_wraps_around_to_ten_when_no_can(void) {
    BmsContext ctx = { .state = BMS_STATE_IDLE, .idleRetryTicks=10};
    BmsInputs inputs = { 
        .isoSpiConnected = true,
        .canConnected = false,
        .modulesPresent = true,
        .tempSensorsPresent = true,
        .voltageSensorsPresent = true,
    };
    BmsOutputs outputs;

    for(int i = 0; i<10; i++){
        Control_Step(&ctx, &inputs, &outputs);
    }
    assert(ctx.idleRetryTicks == 0);

    Control_Step(&ctx, &inputs, &outputs);
    assert(ctx.idleRetryTicks == 10);
    
}

int main(void) {

    //IDLE state tests
    test_idle_stays_idle_when_nothing_connected();
    test_idle_stays_idle_when_isoSpi_not_connected();
    test_idle_stays_idle_when_can_not_connected();
    test_idle_moves_to_ready_when_everything_connects();
    test_idle_moves_to_fault_when_modules_are_missing_after_everything_connects();
    test_idle_moves_to_fault_when_voltage_senosrs_are_missing_after_everything_connects();
    test_idle_moves_to_fault_when_temp_senosrs_are_missing_after_everything_connects();
    test_idle_moves_to_fault_when_senosrs_are_missing_after_everything_connects();
    test_idle_moves_to_fault_when_everything_missing_after_everything_connects();
    test_idle_retry_timer_counts_down_one_tick();
    test_idle_retry_timer_wraps_around_to_ten();
    test_idle_retry_timer_wraps_around_to_ten_when_no_isoSPI();
    test_idle_retry_timer_wraps_around_to_ten_when_no_can();

    printf("All tests passed.\n");
    return 0;

}