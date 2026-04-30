using System;
using System.Threading.Tasks;
using System.Xml;
using MQTTnet;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using MQTTnet.Formatter;
using System.Collections.Concurrent;
using System.Linq;
using System.Threading;
using System.Text;

// Standalone AppConfig class that generates JSON configuration
public class AppConfig
{
    // Constant configuration parameters
    public string AppVersion = "1.0.0";
    public string AppName = "HealoSysApp";
    public string DeviceID = "HealoSysApp";
    public string Environment = "production";
    public int HeartbeatInterval = 60; // seconds
    public string FirmwareVersion = "1.0.0";
    
    // Get JSON representation of the configuration
    public string GetJson()
    {
        var configObject = new JObject
        {
            ["app_name"] = AppName,
            ["app_version"] = AppVersion,
            ["device_id"] = DeviceID,
            ["environment"] = Environment,
            ["heartbeat_interval"] = HeartbeatInterval,
            ["firmware_version"] = FirmwareVersion,
            ["timestamp"] = DateTimeOffset.UtcNow.ToUnixTimeSeconds()
        };
        
        return configObject.ToString(Newtonsoft.Json.Formatting.None);
    }
}

public static class MqttPublisher
{
    // Track subscribed response topics
    private static ConcurrentDictionary<string, bool> subscribedTopics = new ConcurrentDictionary<string, bool>();
    
    // Store pending responses with timeout
    private static ConcurrentDictionary<string, TaskCompletionSource<JObject>> pendingResponses = 
        new ConcurrentDictionary<string, TaskCompletionSource<JObject>>();
    
    public enum MqttPublishResult
    {
        Success,
        ClientNotConnected,
        InvalidPayload,
        PublishFailed,
        ResponseTimeout,
        ResponseError,
        Exception
    }

    // Subscribe to a response topic if not already subscribed
    private static async Task<bool> EnsureResponseTopicSubscribed(IMqttClient mqttClient, string deviceID)
    {
        string responseTopic = $"/v1/dev/response/{deviceID}";
        
        // Check if already subscribed
        if (subscribedTopics.TryGetValue(responseTopic, out bool _))
        {
            return true;
        }
        
        try
        {
            // Subscribe to the response topic
            var subscribeOptions = new MqttClientSubscribeOptionsBuilder()
                .WithTopicFilter(responseTopic)
                .Build();
                
            var subscribeResult = await mqttClient.SubscribeAsync(subscribeOptions);
            
            // Register application message handler if not already registered
            if (!subscribedTopics.ContainsKey("handler_registered"))
            {
                mqttClient.ApplicationMessageReceivedAsync += HandleResponseMessage;
                subscribedTopics["handler_registered"] = true;
            }
            
            // Mark as subscribed
            subscribedTopics[responseTopic] = true;
            return true;
        }
        catch
        {
            return false;
        }
    }

    // Publish a response message to the MQTT broker
    public static async Task<MqttPublishResult> PublishMqttResponse(
        IMqttClient mqttClient,
        string deviceID,
        UInt32 msg_idx,
        string mac,
        string request_type,
        string status,
        string data)
    {
        try
        {
            if (!mqttClient.IsConnected)
                return MqttPublishResult.ClientNotConnected;

            var responseObj = new JObject
            {
                ["msg_idx"] = msg_idx,
                ["mac"] = mac,
                ["response"] = new JObject
                {
                    ["status"] = status,
                    ["type"] = request_type,
                    ["ver"] = "v1.0",
                    ["data"] = data
                }
            };

            string payloadString = responseObj.ToString(Newtonsoft.Json.Formatting.None);
            string topic = $"/v1/dev/response/{deviceID}";

            var message = new MqttApplicationMessageBuilder()
                .WithTopic(topic)
                .WithPayload(payloadString)
                .WithQualityOfServiceLevel(MQTTnet.Protocol.MqttQualityOfServiceLevel.AtLeastOnce)
                .WithRetainFlag(false)
                .Build();

            var publishResult = await mqttClient.PublishAsync(message);

            return publishResult.IsSuccess
                ? MqttPublishResult.Success
                : MqttPublishResult.PublishFailed;
        }
        catch (Exception)
        {
            return MqttPublishResult.Exception;
        }
    }

    // Register a callback for a specific topic
    public static async Task ListenToTopicAsync(IMqttClient mqttClient, IEnumerable<string> topicsToListen, Func<string, Task> callback)
    {
        // Subscribe to each topic before registering the callback
        foreach (var topic in topicsToListen)
        {
            if (!subscribedTopics.ContainsKey(topic))
            {
                //await SubscribeToTopicAsync(mqttClient, topic);
            }
        }

        // Register the callback for each topic
        foreach (var topic in topicsToListen)
        {
            //topicCallbacks.AddOrUpdate(
            //    topic,
            //    _ => new List<Func<string, Task>> { callback },
            //    (_, list) => { list.Add(callback); return list; }
            //);
        }

        // Register the message handler only once
        if (!subscribedTopics.ContainsKey("generic_handler_registered"))
        {
            mqttClient.ApplicationMessageReceivedAsync += async e =>
            {
                //if (topicCallbacks.TryGetValue(e.ApplicationMessage.Topic, out var callbacks))
                //{
                //    string payload = Encoding.UTF8.GetString(e.ApplicationMessage.Payload);
                //    foreach (var cb in callbacks)
                //    {
                //        await cb(payload);
                //    }
                //}
                await Task.CompletedTask;
            };
            subscribedTopics["generic_handler_registered"] = true;
        }
    }   

    public static async Task<bool> MqttConnect(
        IMqttClient mqttClient,
        string brokerHost,
        int brokerPort)
    {
        try
        {
            var mqttClientOptions = new MqttClientOptionsBuilder()
                .WithTcpServer(brokerHost, brokerPort)
                .WithProtocolVersion(MqttProtocolVersion.V500)
                .Build();

            await mqttClient.ConnectAsync(mqttClientOptions, CancellationToken.None);
            return true;
        }
        catch (Exception)
        {
            return false;
        }
    }   
        

    // Handle received response messages
    private static async Task HandleResponseMessage(MqttApplicationMessageReceivedEventArgs e)
    {
        try
        {
            // Convert payload to string
            string payloadString = Encoding.UTF8.GetString(e.ApplicationMessage.Payload);
            
            // Parse JSON
            JObject responseObject = JObject.Parse(payloadString);
            
            // Extract msg_idx
            if (responseObject.TryGetValue("msg_idx", out JToken msgIdxToken))
            {
                UInt32 msgIdx = msgIdxToken.Value<UInt32>();
                string responseKey = msgIdx.ToString();
                
                // Complete the pending task if exists
                if (pendingResponses.TryGetValue(responseKey, out TaskCompletionSource<JObject> pendingTask))
                {
                    pendingTask.TrySetResult(responseObject);
                    pendingResponses.TryRemove(responseKey, out _);
                }
            }
            
            await Task.CompletedTask;
        }
        catch
        {
            // Ignore parsing errors
            await Task.CompletedTask;
        }
    }

    public static async Task<(MqttPublishResult Result, JObject Response)> PublishMqttRequest(
        IMqttClient mqttClient,
        string deviceID,
        UInt32 msg_idx,
        string request_type,
        string data,
        int timeoutMs = 5000)
    {
        try
        {
            if (!mqttClient.IsConnected)
                return (MqttPublishResult.ClientNotConnected, null);
                
            // Ensure response topic is subscribed
            bool subscribed = await EnsureResponseTopicSubscribed(mqttClient, deviceID);
            if (!subscribed)
            {
                return (MqttPublishResult.Exception, null);
            }

            var payloadObj = new JObject
            {
                ["msg_idx"] = msg_idx,
                ["request"] = new JObject
                {
                    ["type"] = request_type,
                    ["ver"] = "v1.0"
                },
                ["data"] = data
            };

            string payloadString = payloadObj.ToString(Newtonsoft.Json.Formatting.None);

            string topic = $"/v1/dev/request/{deviceID}";
            
            // Create a TaskCompletionSource for the response
            var responseTask = new TaskCompletionSource<JObject>();
            string responseKey = msg_idx.ToString();
            pendingResponses[responseKey] = responseTask;
            
            // Set up timeout
            var cancellationTokenSource = new CancellationTokenSource(timeoutMs);
            cancellationTokenSource.Token.Register(() => 
            {
                responseTask.TrySetCanceled();
                pendingResponses.TryRemove(responseKey, out _);
            });

            var message = new MqttApplicationMessageBuilder()
                .WithTopic(topic)
                .WithPayload(payloadString)
                .WithQualityOfServiceLevel(MQTTnet.Protocol.MqttQualityOfServiceLevel.AtLeastOnce)
                .WithRetainFlag(false)
                .Build();

            var publishResult = await mqttClient.PublishAsync(message);

            if (!publishResult.IsSuccess)
            {
                pendingResponses.TryRemove(responseKey, out _);
                return (MqttPublishResult.PublishFailed, null);
            }
            
            // Wait for response or timeout
            try
            {
                JObject response = await responseTask.Task;
                
                // Verify the response matches the request
                if (response.TryGetValue("response", out JToken responseToken) && 
                    responseToken.Type == JTokenType.Object &&
                    responseToken["type"]?.Value<string>() == request_type)
                {
                    // Check status
                    string status = responseToken["status"]?.Value<string>();
                    if (status == "success")
                    {
                        return (MqttPublishResult.Success, response);
                    }
                    else
                    {
                        return (MqttPublishResult.ResponseError, response);
                    }
                }
                else
                {
                    return (MqttPublishResult.ResponseError, response);
                }
            }
            catch (TaskCanceledException)
            {
                return (MqttPublishResult.ResponseTimeout, null);
            }
        }
        catch (Exception)
        {
            return (MqttPublishResult.Exception, null);
        }
    }

    public static async Task<MqttPublishResult> PublishStartupMessage(
        IMqttClient mqttClient, AppConfig appConfig )
    {
        try
        {
            if (!mqttClient.IsConnected)
                return MqttPublishResult.ClientNotConnected;

            // Get configuration JSON from AppConfig
            string configJson = appConfig.GetJson();

            // Fixed topic for startup messages
            string topic = "/v1/dev/startup";

            var message = new MqttApplicationMessageBuilder()
                .WithTopic(topic)
                .WithPayload(configJson)
                .WithQualityOfServiceLevel(MQTTnet.Protocol.MqttQualityOfServiceLevel.AtLeastOnce)
                .WithRetainFlag(false)
                .Build();

            var publishResult = await mqttClient.PublishAsync(message);

            return publishResult.IsSuccess
                ? MqttPublishResult.Success
                : MqttPublishResult.PublishFailed;
        }
        catch (Exception)
        {
            return MqttPublishResult.Exception;
        }
    }
}

