/*******************************************************************************
 * Copyright (c) 2014, 2015 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial API and implementation and/or initial documentation
 *    Ian Craggs - make sure QoS2 processing works, and add device headers
 *******************************************************************************/

 /**
  This is a sample program to illustrate the use of the MQTT Client library
  on the mbed platform.  The Client class requires two classes which mediate
  access to system interfaces for networking and timing.  As long as these two
  classes provide the required public programming interfaces, it does not matter
  what facilities they use underneath. In this program, they use the mbed
  system libraries.

 */

#define MQTTCLIENT_QOS1 0
#define MQTTCLIENT_QOS2 0

#include "easy-connect.h"
#include "MQTTNetwork.h"
#include "MQTTmbed.h"
#include "MQTTClient.h"
#include "MQTT_server_setting.h"
#include "mbed-trace/mbed_trace.h"
#include "mbed_events.h"
#include "NTPClient.h"
#include "mbedtls/error.h"

#define MQTT_MAX_CONNECTIONS     5
#define MQTT_MAX_PACKET_SIZE  1024

#define TIME_JWT_EXP      (60*60*24)  // 24 hours (MAX)

// LED on/off - This could be different among boards. 
// The following values do target the NUCLEO_F767ZI board.
#define LED_ON  1    
#define LED_OFF 0

#include <string>
#include <map>

using ErrorCodesMap_t = std::map<nsapi_size_or_error_t, std::string>;
using IndexElement_t  = ErrorCodesMap_t::value_type;

static ErrorCodesMap_t make_error_codes_map()
{
    ErrorCodesMap_t eMap;
    
    eMap.insert(IndexElement_t(NSAPI_ERROR_OK, std::string("\"no error\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_WOULD_BLOCK, std::string("\"no data is not available but call is non-blocking\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_UNSUPPORTED, std::string("\"unsupported functionality\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_PARAMETER, std::string("\"invalid configuration\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_NO_CONNECTION, std::string("\"not connected to a network\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_NO_SOCKET, std::string("\"socket not available for use\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_NO_ADDRESS, std::string("\"IP address is not known\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_NO_MEMORY, std::string("\"memory resource not available\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_NO_SSID, std::string("\"ssid not found\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_DNS_FAILURE, std::string("\"DNS failed to complete successfully\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_DHCP_FAILURE, std::string("\"DHCP failed to complete successfully\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_AUTH_FAILURE, std::string("\"connection to access point failed\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_DEVICE_ERROR, std::string("\"failure interfacing with the network processor\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_IN_PROGRESS, std::string("\"operation (eg connect) in progress\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_ALREADY, std::string("\"operation (eg connect) already in progress\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_IS_CONNECTED, std::string("\"socket is already connected\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_CONNECTION_LOST, std::string("\"connection lost\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_CONNECTION_TIMEOUT, std::string("\"connection timed out\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_ADDRESS_IN_USE, std::string("\"Address already in use\"")));
    eMap.insert(IndexElement_t(NSAPI_ERROR_TIMEOUT, std::string("\"operation timed out\"")));    
    return eMap;
}

static ErrorCodesMap_t gs_ErrorCodesMap = make_error_codes_map();

std::string ToString(const nsapi_size_or_error_t & key)
{
    return (gs_ErrorCodesMap.at(key));
}

// Flag to be set when a message needs to be published, i.e. BUTTON is pushed. 
static volatile bool isPublish = false;
// Flag to be set when received a message from the server. 
static volatile bool isMessageArrived = false;
// Buffer size for a receiving message. 
const int MESSAGE_BUFFER_SIZE = 1024;
// Buffer for a receiving message.
char messageBuffer[MESSAGE_BUFFER_SIZE];

// Function prototypes
void handleMqttMessage(MQTT::MessageData& md);
void handleButtonRise();

int main(int argc, char* argv[])
{
    mbed_trace_init();
    
    const float version = 0.1;

    NetworkInterface* network = NULL;
    
    // The following values do target the NUCLEO_F767ZI board.
    // Reference : en.DM00244518.pdf
    DigitalOut led_green(LED1);
    DigitalOut led_blue(LED2);
    DigitalOut led_red(LED3);

    printf("Mbed to Watson IoT : version is %.2f\r\n", version);
    printf("\r\n");

    // Turns on green LED to indicate processing initialization process
    led_green = LED_ON;

    printf("Opening network interface...\r\n");
    {
        network = easy_connect(true);    // If true, prints out connection details.
        if (!network) 
	{
            printf("Unable to open network interface.\r\n");
            return -1;
        }
    }
    printf("Network interface opened successfully.\r\n");
    printf("\r\n");

    // sync the real time clock (RTC)
    NTPClient ntp(network);
    ntp.set_server("time.google.com", 123);
    time_t now = ntp.get_timestamp();
    set_time(now);
    printf("Time is now %s", ctime(&now));

    std::string hostname = std::string(ORG_ID) + ".messaging.internetofthings.ibmcloud.com";

    // Establish a network connection. 
    MQTTNetwork* mqttNetwork = NULL;
    printf("Connecting to host %s:%d ...\r\n", hostname.c_str(), MQTT_SERVER_PORT);
    {
        mqttNetwork = new MQTTNetwork(network);
        int rc = mqttNetwork->connect(hostname.c_str(), MQTT_SERVER_PORT, SSL_CA_PEM,
                SSL_CLIENT_CERT_PEM, SSL_CLIENT_PRIVATE_KEY_PEM);
        if (rc != MQTT::SUCCESS)
	{
            const int MAX_TLS_ERROR_CODE = -0x1000;
            
	    // Network error
            if ((MAX_TLS_ERROR_CODE < rc) && (rc < 0)) 
	    {
                printf("ERROR from MQTTNetwork connect is: [%d] -> %s\n", rc, ToString(rc).c_str());
            }
            
	    // TLS error - mbedTLS error codes starts from -0x1000 to -0x8000.
            if (rc <= MAX_TLS_ERROR_CODE) 
	    {
                const int buf_size = 256;
                char *buf = new char[buf_size];
                mbedtls_strerror(rc, buf, buf_size);
                printf("TLS ERROR (%d) : %s\r\n", rc, buf);
		delete[] buf;
            }
            return -1;
        }
    }
    printf("Connection established.\r\n");
    printf("\r\n");

    const char* username = "use-token-auth";
    std::string cid = std::string("d:") + ORG_ID + ":" + DEVICE_TYPE + ":" + DEVICE_ID;

    // Establish a MQTT connection. 
    MQTT::Client<MQTTNetwork, Countdown, MQTT_MAX_PACKET_SIZE, MQTT_MAX_CONNECTIONS>* mqttClient = NULL;
    printf("MQTT client is connecting to the service ...\r\n");
    {
        MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
        data.MQTTVersion = 4; // 3 = 3.1 4 = 3.1.1
        data.clientID.cstring = (char*)cid.c_str();
        data.username.cstring = (char*)username;
        data.password.cstring = (char *)TOKEN;

        mqttClient = new MQTT::Client<MQTTNetwork, Countdown, MQTT_MAX_PACKET_SIZE, MQTT_MAX_CONNECTIONS>(*mqttNetwork);
        int rc = mqttClient->connect(data);
        if (rc != MQTT::SUCCESS) 
	{
            printf("ERROR: rc from MQTT connect is %d\r\n", rc);
            return -1;
        }
    }
    printf("Client connected.\r\n");
    printf("\r\n");

    // Network initialization done. Turn off the green LED
    led_green = LED_OFF;

    const char* mqtt_topic_pub = "iot-2/evt/start/fmt/text";
    const char* mqtt_topic_sub = "iot-2/cmd/myevt/fmt/text";

    // Subscribe a topic.
    bool isSubscribed = false;
    printf("Client is trying to subscribe a topic \"%s\".\r\n", mqtt_topic_sub);
    {
        int rc = mqttClient->subscribe(mqtt_topic_sub, MQTT::QOS0, handleMqttMessage);
        if (rc != MQTT::SUCCESS) 
	{
            printf("ERROR: rc from MQTT subscribe is %d\r\n", rc);
            return -1;
        }
        isSubscribed = true;
    }
    printf("Client has subscribed a topic \"%s\".\r\n", mqtt_topic_sub);
    printf("\r\n");

    // Enable button 1 for publishing a message.
    InterruptIn btn1 = InterruptIn(BUTTON1);
    btn1.rise(handleButtonRise);
    
    printf("To send a packet, push the button 1 on your board.\r\n");

    // Main loop
    while (1) 
    {
        // Client is disconnected. 
        if (!mqttClient->isConnected())
	{        
            break;
        }
        
	// Waits a message and handles keepalive. 
        if (mqttClient->yield(100) != MQTT::SUCCESS) 
	{
            break;
        }
	
        // Received a message. 
        if (isMessageArrived) 
	{
            isMessageArrived = false;
            printf("\r\nMessage arrived:\r\n%s\r\n", messageBuffer);
        }
	
        // Button is pushed - publish a message. 
        if (isPublish) 
	{
            isPublish = false;
            static unsigned int id = 0;
            static unsigned int count = 0;

            // When sending a message, blue LED lights.
            led_blue = LED_ON;

            MQTT::Message message;
            message.retained = false;
            message.dup = false;

            const size_t len = 128;
            char buf[len];
            snprintf(buf, len, "Message #%d from %s.", count, DEVICE_ID);
            message.payload = (void*)buf;

            message.qos = MQTT::QOS0;
            message.id = id++;
            message.payloadlen = strlen(buf);
            
	    // Publish a message.
            printf("\r\nPublishing message to the topic %s:\r\n%s\r\n", mqtt_topic_pub, buf);
            int rc = mqttClient->publish(mqtt_topic_pub, message);
            if (rc != MQTT::SUCCESS) 
	    {
                printf("ERROR: rc from MQTT publish is %d\r\n", rc);
            }
            printf("Message published.\r\n");

            count++;

            led_blue = LED_OFF;
        }
    }

    printf("The client has disconnected.\r\n");

    if (mqttClient) 
    {
        if (isSubscribed) 
	{
            mqttClient->unsubscribe(mqtt_topic_sub);
            mqttClient->setMessageHandler(mqtt_topic_sub, 0);
        }
	
        if (mqttClient->isConnected())
	{ 
            mqttClient->disconnect();
	}
        delete mqttClient;
    }
    
    if (mqttNetwork) 
    {
        mqttNetwork->disconnect();
        delete mqttNetwork;
    }
    
    if (network) 
    {
        network->disconnect();
        // network is not created by new.
    }

    // Turn on the red LED when the program is done.
    led_red = LED_ON;
}

/*
 * Callback function called when a message arrived from server.
 */
void handleMqttMessage(MQTT::MessageData& md)
{
    // Copy payload to the buffer.
    MQTT::Message &message = md.message;
    memcpy(messageBuffer, message.payload, message.payloadlen);
    messageBuffer[message.payloadlen] = '\0';

    isMessageArrived = true;
}

/*
 * Callback function called when button is pushed.
 */
void handleButtonRise() 
{
    isPublish = true;
}
