
using System;
using System.Threading;
using System.Threading.Tasks;
using MQTTnet;
using MQTTnet.Formatter;

namespace MqttOtaReceiverTest
{
    class Program
    {
        // Table to hold topic and corresponding callback list
        //private static ConcurrentDictionary<string, List<Func<string, Task>>> topicCallbacks = new ConcurrentDictionary<string, List<Func<string, Task>>>();
        private static IMqttClient mqttClient;

        static async Task Main(string[] args)
        {
            Console.WriteLine("MQTT OTA Receiver Test Application");
            Console.WriteLine("----------------------------------");

            var mqttFactory = new MqttClientFactory();
            mqttClient = mqttFactory.CreateMqttClient();

            bool isConnected = await MqttPublisher.MqttConnect(
                mqttClient,
                "144.24.156.245",
                1883
            );

            if (!isConnected)
            {
                Console.WriteLine("Failed to connect to MQTT broker.");
                return;
            }

            Console.WriteLine("Connected to MQTT broker.");

            // public static async Task<(MqttPublishResult Result, JObject Response)> PublishMqttRequest(
            //     IMqttClient mqttClient,
            //     string deviceID,
            //     UInt32 msg_idx,
            //     string request_type,
            //     string data,
            //     int timeoutMs = 5000)

            AppConfig appConfig = new AppConfig();
            appConfig.AppName = "HsysMqttTest";

            await MqttPublisher.PublishStartupMessage(mqttClient, appConfig);

            // public static async Task<MqttPublishResult> PublishMqttResponse(
            //     IMqttClient mqttClient,
            //     string deviceID,
            //     UInt32 msg_idx,
            //     string mac,
            //     string request_type,
            //     string status,
            //     JObject data)

            await MqttPublisher.PublishMqttResponse(
                mqttClient,
                deviceID: "device123",
                msg_idx: 1,
                mac: "00:11:22:33:44:55",
                request_type: "OTA",
                status: "OK",
                data: "1.0.0"
            );            

            // Keep the application running
            Console.WriteLine("Press Ctrl+C to exit.");
            await Task.Delay(Timeout.Infinite);
        }

        // Example callback implementation
        public static async Task OnRequestReceived(string payload)
        {
            Console.WriteLine($"Received on /v1/dev/request/#: {payload}");
            await Task.CompletedTask;
        }

    }
}


