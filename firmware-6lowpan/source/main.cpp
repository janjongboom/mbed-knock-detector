/*
 * Copyright (c) 2015 ARM Limited. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string>
#include <sstream>
#include <vector>
#include "mbed-drivers/mbed.h"
#include "atmel-rf-driver/driverRFPhy.h"    // rf_device_register
#include "mbed-mesh-api/Mesh6LoWPAN_ND.h"
#include "mbed-mesh-api/MeshThread.h"
#include "mbed-mesh-api/MeshInterfaceFactory.h"
#include "mbed-drivers/test_env.h"
#include "mbed-hal/rtc_api.h"
#include "mbed-client/m2minterfacefactory.h"
#include "mbed-client/m2mdevice.h"
#include "mbed-client/m2minterfaceobserver.h"
#include "mbed-client/m2minterface.h"
#include "mbed-client/m2mobjectinstance.h"
#include "mbed-client/m2mresource.h"
#include "mbed-mesh-api/AbstractMesh.h"
#include "mbedclient.h"
#include "fxos8700cq/fxos8700cq.h"

struct MbedClientDevice device = {
    "Manufacturer_String",      // Manufacturer
    "Type_String",              // Type
    "ModelNumber_String",       // ModelNumber
    "SerialNumber_String",      // SerialNumber
    "knock-sensor"              // DeviceType
};

// LED Output
DigitalOut led1(LED1);

// Accelerometer config
InterruptIn accel_interrupt_pin(PTC13);  // FRDM-K64F
FXOS8700CQ accel(PTE25, PTE24, FXOS8700CQ_SLAVE_ADDR1); // FRDM-K64F

/*
 * The button contains one property (click count).
 * When `handle_button_click` is executed, the counter updates.
 */
class ButtonResource {
public:
    ButtonResource() {
        // create ObjectID with metadata tag of '3200', which is 'digital input'
        btn_object = M2MInterfaceFactory::create_object("3200");
        M2MObjectInstance* btn_inst = btn_object->create_object_instance();
        // create resource with ID '5501', which is digital input counter
        M2MResource* btn_res = btn_inst->create_dynamic_resource("5501", "Button",
            M2MResourceInstance::INTEGER, true /* observable */);
        // we can read this value
        btn_res->set_operation(M2MBase::GET_ALLOWED);
        // set initial value (all values in mbed Client are buffers)
        // to be able to read this data easily in the Connector console, we'll use a string
        btn_res->set_value((uint8_t*)"0", 1);
    }

    M2MObject* get_object() {
        return btn_object;
    }

    /*
     * When you press the button, we read the current value of the click counter
     * from mbed Device Connector, then up the value with one.
     */
    void handle_button_click() {
        M2MObjectInstance* inst = btn_object->object_instance();
        M2MResource* res = inst->resource("5501");

        // up counter
        counter++;

        printf("handle_button_click, new value of counter is %d\r\n", counter);

        // serialize the value of counter as a string, and tell connector
        stringstream ss;
        ss << counter;
        std::string stringified = ss.str();
        res->set_value((uint8_t*)stringified.c_str(), stringified.length());
    }

private:
    M2MObject* btn_object;
    uint16_t counter = 0;
};

class AccelerometerResource {
public:
    AccelerometerResource() {
        accel_interrupt_pin.fall(this, &AccelerometerResource::interrupt);
        accel_interrupt_pin.mode(PullUp);

        accel.config_int();      // enabled interrupts from accelerometer
        accel.config_feature();  // turn on motion detection
        accel.enable();          // enable accelerometer

        accel_object = M2MInterfaceFactory::create_object("accelerometer");
        M2MObjectInstance* accel_inst = accel_object->create_object_instance();
        accel_res = accel_inst->create_dynamic_resource("last_knock", "Knock",
            M2MResourceInstance::INTEGER, true /* observable */);
        accel_res->set_operation(M2MBase::GET_ALLOWED);
        accel_res->set_value((uint8_t*)"0", 1);
    }

    M2MObject* get_object() {
        return accel_object;
    }

private:
    void led_off(void) {
        led1 = 1;
    }

    void motion_detected(void) {
        printf("motion_detected\r\n");
        // update in connector
        stringstream ss;
        ss << rtc_read();
        std::string stringified = ss.str();
        accel_res->set_value((uint8_t*)stringified.c_str(), stringified.length());

        minar::Scheduler::postCallback(mbed::util::FunctionPointer(this, &AccelerometerResource::led_off).bind()).delay(minar::milliseconds(1000));

        wait(1);
        led1 = 1;
    }

    void interrupt(void) {
        led1 = 0;  // turn led on
        accel.clear_int();
        minar::Scheduler::postCallback(mbed::util::FunctionPointer(this, &AccelerometerResource::motion_detected).bind());
    }

    M2MObject* accel_object;
    M2MResource* accel_res;
};

// Set bootstrap mode to be Thread, otherwise 6LOWPAN_ND is used
//#define APPL_BOOTSTRAP_MODE_THREAD

static MbedClient *mbedclient;
static InterruptIn obs_button(SW2);
static InterruptIn unreg_button(SW3);
static Serial &pc = get_stdio_serial();
static uint8_t mac_addr[8];

uint8_t *get_mac_address(){
    return mac_addr;
}

void app_start(int, char **)
{
	pc.baud(115200);  //Setting the Baud-Rate for trace output
    printf("Start mbed-client-example-6lowpan\r\n");

    // Instantiate the class which implements
    // LWM2M Client API
    mbedclient = new MbedClient(device);

    auto accel_resource = new AccelerometerResource();
    // auto button_resource = new ButtonResource();
    mbedclient->object_list_push(accel_resource->get_object());
    // mbedclient->object_list_push(button_resource->get_object());

    // This sets up the network interface configuration which will be used
    // by LWM2M Client API to communicate with mbed Device server.
    AbstractMesh *mesh_api;
    int8_t status;
#ifdef APPL_BOOTSTRAP_MODE_THREAD
    mesh_api = MeshInterfaceFactory::createInterface(MESH_TYPE_THREAD);
    uint8_t eui64[8];
    int8_t rf_device_id = rf_device_register();
    // Read mac address after registering the device.
    rf_read_mac_address(&eui64[0]);
    char *pskd = (char *)"Secret password";
    status = ((MeshThread *)mesh_api)->init(rf_device_id, AbstractMesh::mesh_network_handler_t(mbedclient, &MbedClient::mesh_network_handler), eui64, pskd);
#else /* APPL_BOOTSTRAP_MODE_THREAD */
    mesh_api = (Mesh6LoWPAN_ND *)MeshInterfaceFactory::createInterface(MESH_TYPE_6LOWPAN_ND);
    status = ((Mesh6LoWPAN_ND *)mesh_api)->init(rf_device_register(), AbstractMesh::mesh_network_handler_t(mbedclient, &MbedClient::mesh_network_handler));
#endif /* APPL_BOOTSTRAP_MODE */

    if (status != MESH_ERROR_NONE) {
        printf("Mesh network initialization failed %d!\r\n", status);
        return;
    }

    rf_read_mac_address(mac_addr);

    // Set up Hardware interrupt button.
    // On press of SW3 button on K64F board, example application
    // will call unregister API towards mbed Device Server
    unreg_button.fall(mbedclient, &MbedClient::test_unregister);

    // Observation Button (SW2) press will send update of endpoint resource values to connector
    // obs_button.fall(button_resource, &ButtonResource::handle_button_click);

    status = mesh_api->connect();
    if (status != MESH_ERROR_NONE) {
        printf("Can't connect to mesh network!\r\n");
        return;
    }
}
