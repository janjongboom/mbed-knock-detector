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
#include "minar/minar.h"
#include "mbed-hal/rtc_api.h"
#include "security.h"
#include "simpleclient.h"
#include "lwipv4_init.h"
#include "fxos8700cq/fxos8700cq.h"

using namespace mbed::util;

Serial &output = get_stdio_serial();

EthernetInterface eth;

// These are example resource values for the Device Object
struct MbedClientDevice device = {
    "Manufacturer_String",      // Manufacturer
    "Type_String",              // Type
    "ModelNumber_String",       // ModelNumber
    "SerialNumber_String"       // SerialNumber
};

// Instantiate the class which implements LWM2M Client API (from simpleclient.h)
MbedClient mbed_client(device);

// Set up Hardware interrupt button.
InterruptIn obs_button(SW2);
InterruptIn unreg_button(SW3);

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

void app_start(int /*argc*/, char* /*argv*/[]) {

    //Sets the console baud-rate
    output.baud(115200);

    output.printf("In app_start()\r\n");
    
    led1 = 1; // turn led off

    // This sets up the network interface configuration which will be used
    // by LWM2M Client API to communicate with mbed Device server.
    eth.init();     //Use DHCP
    if (eth.connect() != 0) {
        output.printf("Failed to form a connection!\r\n");
    }
    if (lwipv4_socket_init() != 0) {
        output.printf("Error on lwipv4_socket_init!\r\n");
    }
    output.printf("IP address %s\r\n", eth.getIPAddress());
    output.printf("Device name %s\r\n", MBED_ENDPOINT_NAME);

    // we create our button and LED resources
    auto button_resource = new ButtonResource();
    auto accel_resource = new AccelerometerResource();

    // Unregister button (SW3) press will unregister endpoint from connector.mbed.com
    unreg_button.fall(&mbed_client, &MbedClient::test_unregister);

    // Observation Button (SW2) press will send update of endpoint resource values to connector
    obs_button.fall(button_resource, &ButtonResource::handle_button_click);

    // Create endpoint interface to manage register and unregister
    mbed_client.create_interface();

    // Create Objects of varying types, see simpleclient.h for more details on implementation.
    M2MSecurity* register_object = mbed_client.create_register_object(); // server object specifying connector info
    M2MDevice*   device_object   = mbed_client.create_device_object();   // device resources object

    // Create list of Objects to register
    M2MObjectList object_list;

    // Add objects to list
    object_list.push_back(device_object);
    object_list.push_back(button_resource->get_object());
    object_list.push_back(accel_resource->get_object());

    // Set endpoint registration object
    mbed_client.set_register_object(register_object);

    // Issue register command.
    FunctionPointer2<void, M2MSecurity*, M2MObjectList> fp(&mbed_client, &MbedClient::test_register);
    minar::Scheduler::postCallback(fp.bind(register_object,object_list));
    minar::Scheduler::postCallback(&mbed_client,&MbedClient::test_update_register).period(minar::milliseconds(25000));
}
