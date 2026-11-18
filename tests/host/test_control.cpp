#include <catch2/catch_test_macros.hpp>

extern "C" {
#include "control.h"
}

TEST_CASE("IDLE stays IDLE when nothing is connected", "[control][idle]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_IDLE;
    ctx.idleRetryTicks = 10;

    BmsInputs inputs{};
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_IDLE);
    CHECK(ctx.idleRetryTicks == 9);
    CHECK(outputs.state == BMS_STATE_IDLE);
    CHECK_FALSE(outputs.shutdownCircuitClosed);
    CHECK_FALSE(outputs.canBalance);
}

TEST_CASE("IDLE stays IDLE when isoSPI not connected", "[control][idle]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_IDLE;
    ctx.idleRetryTicks = 10;

    BmsInputs inputs{};
    inputs.isoSpiConnected = false;
    inputs.canConnected = true;
    inputs.modulesPresent = true;
    inputs.tempSensorsPresent = true;
    inputs.voltageSensorsPresent = true;

    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_IDLE);
}

TEST_CASE("IDLE stays IDLE when CAN is not connected", "[control][idle]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_IDLE;
    ctx.idleRetryTicks = 10;
    BmsInputs inputs{};
    inputs.isoSpiConnected = true;
    inputs.canConnected = false;
    inputs.modulesPresent = true;
    inputs.tempSensorsPresent = true;
    inputs.voltageSensorsPresent = true;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_IDLE);
}

TEST_CASE("IDLE moves to READY when everything connects", "[control][idle]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_IDLE;
    ctx.idleRetryTicks = 10;
    BmsInputs inputs{};
    inputs.isoSpiConnected = true;
    inputs.canConnected = true;
    inputs.modulesPresent = true;
    inputs.tempSensorsPresent = true;
    inputs.voltageSensorsPresent = true;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_READY);
}

TEST_CASE("IDLE moves to FAULT when modules are missing after connecting", "[control][idle]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_IDLE;
    ctx.idleRetryTicks = 10;
    BmsInputs inputs{};
    inputs.isoSpiConnected = true;
    inputs.canConnected = true;
    inputs.modulesPresent = false;
    inputs.tempSensorsPresent = true;
    inputs.voltageSensorsPresent = true;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_FAULT);
}

TEST_CASE("IDLE moves to FAULT when voltage sensors are missing after connecting", "[control][idle]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_IDLE;
    ctx.idleRetryTicks = 10;
    BmsInputs inputs{};
    inputs.isoSpiConnected = true;
    inputs.canConnected = true;
    inputs.modulesPresent = true;
    inputs.tempSensorsPresent = true;
    inputs.voltageSensorsPresent = false;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_FAULT);
}

TEST_CASE(
    "IDLE moves to FAULT when temperature sensors are missing after connecting", "[control][idle]"
) {
    BmsContext ctx{};
    ctx.state = BMS_STATE_IDLE;
    ctx.idleRetryTicks = 10;
    BmsInputs inputs{};
    inputs.isoSpiConnected = true;
    inputs.canConnected = true;
    inputs.modulesPresent = true;
    inputs.tempSensorsPresent = false;
    inputs.voltageSensorsPresent = true;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_FAULT);
}

TEST_CASE("IDLE moves to FAULT when both sensor types are missing after connecting", "[control][idle]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_IDLE;
    ctx.idleRetryTicks = 10;
    BmsInputs inputs{};
    inputs.isoSpiConnected = true;
    inputs.canConnected = true;
    inputs.modulesPresent = true;
    inputs.tempSensorsPresent = false;
    inputs.voltageSensorsPresent = false;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_FAULT);
}

TEST_CASE(
    "IDLE moves to FAULT when modules and sensors are missing after connecting", "[control][idle]"
) {
    BmsContext ctx{};
    ctx.state = BMS_STATE_IDLE;
    ctx.idleRetryTicks = 10;
    BmsInputs inputs{};
    inputs.isoSpiConnected = true;
    inputs.canConnected = true;
    inputs.modulesPresent = false;
    inputs.tempSensorsPresent = false;
    inputs.voltageSensorsPresent = false;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_FAULT);
}

TEST_CASE("IDLE retry timer counts down one tick", "[control][idle]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_IDLE;
    ctx.idleRetryTicks = 10;
    BmsInputs inputs{};
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.idleRetryTicks == 9);
}

TEST_CASE("IDLE retry timer wraps around to ten", "[control][idle]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_IDLE;
    ctx.idleRetryTicks = 10;
    BmsInputs inputs{};
    BmsOutputs outputs{};

    for (int i = 0; i < 10; i++) {
        Control_Step(&ctx, &inputs, &outputs);
    }
    CHECK(ctx.idleRetryTicks == 0);

    Control_Step(&ctx, &inputs, &outputs);
    CHECK(ctx.idleRetryTicks == 10);
}

TEST_CASE("IDLE retry timer wraps around to ten without isoSPI", "[control][idle]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_IDLE;
    ctx.idleRetryTicks = 10;
    BmsInputs inputs{};
    inputs.isoSpiConnected = false;
    inputs.canConnected = true;
    inputs.modulesPresent = true;
    inputs.tempSensorsPresent = true;
    inputs.voltageSensorsPresent = true;
    BmsOutputs outputs{};

    for (int i = 0; i < 10; i++) {
        Control_Step(&ctx, &inputs, &outputs);
    }
    CHECK(ctx.idleRetryTicks == 0);

    Control_Step(&ctx, &inputs, &outputs);
    CHECK(ctx.idleRetryTicks == 10);
}

TEST_CASE("IDLE retry timer wraps around to ten without CAN", "[control][idle]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_IDLE;
    ctx.idleRetryTicks = 10;
    BmsInputs inputs{};
    inputs.isoSpiConnected = true;
    inputs.canConnected = false;
    inputs.modulesPresent = true;
    inputs.tempSensorsPresent = true;
    inputs.voltageSensorsPresent = true;
    BmsOutputs outputs{};

    for (int i = 0; i < 10; i++) {
        Control_Step(&ctx, &inputs, &outputs);
    }
    CHECK(ctx.idleRetryTicks == 0);

    Control_Step(&ctx, &inputs, &outputs);
    CHECK(ctx.idleRetryTicks == 10);
}

TEST_CASE(
    "READY moves to CHARGING when readings are good and charging is requested", "[control][ready]"
) {
    BmsContext ctx{};
    ctx.state = BMS_STATE_READY;
    BmsInputs inputs{};
    inputs.voltagesGood = true;
    inputs.tempsGood = true;
    inputs.chargingRequested = true;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_CHARGING);
}

TEST_CASE(
    "READY moves to RUNNING when readings are good and charging is not requested", "[control][ready]"
) {
    BmsContext ctx{};
    ctx.state = BMS_STATE_READY;
    BmsInputs inputs{};
    inputs.voltagesGood = true;
    inputs.tempsGood = true;
    inputs.chargingRequested = false;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_RUNNING);
}

TEST_CASE("READY moves to FAULT when both readings are bad", "[control][ready]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_READY;
    BmsInputs inputs{};
    inputs.voltagesGood = false;
    inputs.tempsGood = false;
    inputs.chargingRequested = true;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_FAULT);
}

TEST_CASE("READY moves to FAULT when voltage readings are bad", "[control][ready]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_READY;
    BmsInputs inputs{};
    inputs.voltagesGood = false;
    inputs.tempsGood = true;
    inputs.chargingRequested = true;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_FAULT);
}

TEST_CASE("READY moves to FAULT when temperature readings are bad", "[control][ready]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_READY;
    BmsInputs inputs{};
    inputs.voltagesGood = true;
    inputs.tempsGood = false;
    inputs.chargingRequested = false;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_FAULT);
}

TEST_CASE("RUNNING stays RUNNING when readings are good", "[control][running]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_RUNNING;
    BmsInputs inputs{};
    inputs.voltagesGood = true;
    inputs.tempsGood = true;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_RUNNING);
}

TEST_CASE("RUNNING moves to FAULT when voltage readings are bad", "[control][running]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_RUNNING;
    BmsInputs inputs{};
    inputs.voltagesGood = false;
    inputs.tempsGood = true;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_FAULT);
}

TEST_CASE("RUNNING moves to FAULT when temperature readings are bad", "[control][running]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_RUNNING;
    BmsInputs inputs{};
    inputs.voltagesGood = true;
    inputs.tempsGood = false;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_FAULT);
}

TEST_CASE("CHARGING stays CHARGING when readings are good", "[control][charging]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_CHARGING;
    BmsInputs inputs{};
    inputs.voltagesGood = true;
    inputs.tempsGood = true;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_CHARGING);
}

TEST_CASE("CHARGING moves to FAULT when voltage readings are bad", "[control][charging]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_CHARGING;
    BmsInputs inputs{};
    inputs.voltagesGood = false;
    inputs.tempsGood = true;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_FAULT);
}

TEST_CASE("CHARGING moves to FAULT when temperature readings are bad", "[control][charging]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_CHARGING;
    BmsInputs inputs{};
    inputs.voltagesGood = true;
    inputs.tempsGood = false;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_FAULT);
}

TEST_CASE("FAULT moves to IDLE when reset is requested", "[control][fault]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_FAULT;
    BmsInputs inputs{};
    inputs.resetRequested = true;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_IDLE);
}

TEST_CASE("FAULT stays FAULT when reset is not requested", "[control][fault]") {
    BmsContext ctx{};
    ctx.state = BMS_STATE_FAULT;
    BmsInputs inputs{};
    inputs.resetRequested = false;
    BmsOutputs outputs{};

    Control_Step(&ctx, &inputs, &outputs);

    CHECK(ctx.state == BMS_STATE_FAULT);
}
