using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Threading;
using MQTTnet;
using Newtonsoft.Json.Linq;
using MQTTnet.Protocol;
using System.Collections.Concurrent;
using System.Buffers;
using System.Diagnostics;

namespace HealoSysMqtt.Libs.Mqtt
{
    class HsysMqtt
    {
        private string _basePublishTopic;
        private string _baseResponseTopic;
        private ConcurrentDictionary<string, TaskCompletionSource<byte[]>> _pendingResponses;
        private bool _messageHandlerRegistered = false;

        // Static collection to track all instances
        private static readonly ConcurrentBag<HsysMqtt> _instances = new ConcurrentBag<HsysMqtt>();
        
        // Delegate for log messages
        public delegate void LogMessageDelegate(string msg);
        private LogMessageDelegate _logDebug;
        
        public HsysMqtt(string commandTopic, string responseTopic, LogMessageDelegate logDebug = null)
        {
            _basePublishTopic = commandTopic;
            _baseResponseTopic = responseTopic;
            _pendingResponses = new ConcurrentDictionary<string, TaskCompletionSource<byte[]>>();
            _logDebug = logDebug;

            // Add this instance to the static collection
            _instances.Add(this);
        }
        
        
        // Function to get current timestamp for logging
        private static string GetTimestamp()
        {
            return DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff");                
        }
        
        /* 
         * Function Name, CommandResponse
         * 
         * Parameters 
         *  IMqttClient mqttClient 
         *  Device ID, which is a UUID string  
         *  Payload byte array
         *  Payload Length
         *  Response Pipe
         *  uint32_t timeout ms
         *  ref resp_payload_buffer
         *  ref resp_payload_length
         *  
         *  Function will send the payload as hex to the basetopic_publish/uuid, base topics will be 
         *  created during the initialization of the class.
         *  
         *  The fuction should subscribed to the base topic and then should publish the payload to the
         *  publish topic including the uuid, then should wait until the response is received tot the
         *  topic is subscribed until the timeout, and the the funciton will return with the response
         *  buffer, 
         */

        public async Task<(bool Success, byte[]? ResponseBuffer, int ResponseLength)> CommandResponseAsync(
            IMqttClient mqttClient,
            string deviceUuid,
            byte[] payload,
            int payloadLength,
            string responsePipe,
            uint timeoutMs)
        {
            if (!mqttClient.IsConnected)
            {
                await mqttClient.ReconnectAsync();

                if (!mqttClient.IsConnected)
                {
                    return (false, null, 0);
                }
            }
                

            // Create the full topic paths
            string publishTopic = $"{_basePublishTopic}/{deviceUuid}";
            string responseTopic = $"{_baseResponseTopic}/{deviceUuid}/{responsePipe}";

            _logDebug($"Publishing to: {publishTopic}");
            _logDebug($"Waiting for response on: {responseTopic}");

            try
            {
                // DO NOT register message handler here - let the global one in Form1 handle it
                // We'll check for responses in a different way

                // Subscribe to response topic
                var subscribeOptions = new MqttClientSubscribeOptionsBuilder()
                    .WithTopicFilter(responseTopic)
                    .Build();

                var subscribeResult = await mqttClient.SubscribeAsync(subscribeOptions);
                // _logDebug($"Subscribed to {responseTopic}: {subscribeResult.Items.FirstOrDefault()?.ResultCode}");

                // Add a small delay to ensure subscription is processed
                await Task.Delay(100);

                // Create TaskCompletionSource for this specific response
                var responseTask = new TaskCompletionSource<byte[]>();
                _pendingResponses[responseTopic] = responseTask;
                // _logDebug($"Added pending response for topic: {responseTopic}");
                // _logDebug($"Total pending responses: {_pendingResponses.Count}");

                // Publish the command
                var message = new MqttApplicationMessageBuilder()
                    .WithTopic(publishTopic)
                    .WithPayload(payload.Take(payloadLength).ToArray())
                    .WithQualityOfServiceLevel(MqttQualityOfServiceLevel.AtLeastOnce)
                    .WithRetainFlag(false)
                    .Build();

                var publishResult = await mqttClient.PublishAsync(message);
                _logDebug($"Publish result: {publishResult.IsSuccess}");


                if (!publishResult.IsSuccess)
                {
                    _pendingResponses.TryRemove(responseTopic, out _);
                    return (false, null, 0);
                }

                // Wait for response with timeout
                using var cancellationTokenSource = new CancellationTokenSource((int)timeoutMs);

                try
                {
                    var responseData = await responseTask.Task.WaitAsync(cancellationTokenSource.Token);
                    _logDebug($"Received response: {responseData?.Length ?? 0} bytes");
                    return (true, responseData, responseData?.Length ?? 0);
                }
                catch (TimeoutException)
                {
                    _logDebug("Response timeout");
                    _pendingResponses.TryRemove(responseTopic, out _);
                    return (false, null, 0);
                }
                catch (TaskCanceledException)
                {
                    _logDebug("Response cancelled");
                    _pendingResponses.TryRemove(responseTopic, out _);
                    return (false, null, 0);
                }
            }
            catch (Exception ex)
            {
                _logDebug($"CommandResponseAsync error: {ex.Message}");
                _pendingResponses.TryRemove(responseTopic, out _);
                return (false, null, 0);
            }
        }
        
        // Non-async wrapper for backward compatibility
        public bool CommandResponse(
            IMqttClient mqttClient, 
            string deviceUuid, 
            byte[] payload, 
            int payloadLength, 
            string responsePipe, 
            uint timeoutMs, 
            ref byte[] respPayloadBuffer,
            ref int respPayloadLength)
        {
            // Call the async version and wait for its result
            var result = CommandResponseAsync(
                mqttClient, 
                deviceUuid, 
                payload, 
                payloadLength, 
                responsePipe, 
                timeoutMs).GetAwaiter().GetResult();
            
            if (result.Success && result.ResponseBuffer != null)
            {
                respPayloadBuffer = result.ResponseBuffer;
                respPayloadLength = result.ResponseLength;
            }
            
            return result.Success;
        }
        
        private Task HandleResponseMessage(MqttApplicationMessageReceivedEventArgs e)
        {
            try
            {
                string topic = e.ApplicationMessage.Topic.Trim(); // Trim whitespace
                // _logDebug($"[HsysMqtt] Received message on topic: '{topic}' (trimmed)");
                // _logDebug($"[HsysMqtt] Original topic: '{e.ApplicationMessage.Topic}' (length: {e.ApplicationMessage.Topic.Length})");
                // _logDebug($"[HsysMqtt] Current pending responses: {string.Join(", ", _pendingResponses.Keys)}");
                
                // Check exact match first
                bool hasExactMatch = _pendingResponses.ContainsKey(topic);
                // _logDebug($"[HsysMqtt] Exact topic match for '{topic}': {hasExactMatch}");
                
                // Check if we have a pending response for this topic
                if (_pendingResponses.TryRemove(topic, out var pendingTask))
                {
                    // _logDebug($"[HsysMqtt] SUCCESS: Found and removed pending task for topic: {topic}");
                    // _logDebug($"[HsysMqtt] Task status before completion: {pendingTask.Task.Status}");
                    
                    // Get payload - handle ReadOnlySequence<byte>
                    byte[] payload = Array.Empty<byte>();
                    
                    try
                    {
                        var payloadSequence = e.ApplicationMessage.Payload;
                        if (payloadSequence.Length > 0)
                        {
                            payload = payloadSequence.ToArray();
                        }
                    }
                    catch
                    {
                        try
                        {
                            // Fallback for older versions
                            var payloadString = e.ApplicationMessage.ConvertPayloadToString();
                            if (!string.IsNullOrEmpty(payloadString))
                            {
                                payload = System.Text.Encoding.UTF8.GetBytes(payloadString);
                            }
                        }
                        catch
                        {
                            payload = Array.Empty<byte>();
                        }
                    }
                    
                    // _logDebug($"[HsysMqtt] Payload length: {payload.Length}");
                    
                    // Complete the task with the payload
                    bool taskCompleted = pendingTask.TrySetResult(payload);
                    // _logDebug($"[HsysMqtt] Task completion result: {taskCompleted}");
                    // _logDebug($"[HsysMqtt] Task status after completion: {pendingTask.Task.Status}");
                }
                else
                {
                    // _logDebug($"[HsysMqtt] No pending task found for topic: {topic}");
                    // Debug: Show what topics we are waiting for
                    foreach (var pendingTopic in _pendingResponses.Keys)
                    {
                        // _logDebug($"[HsysMqtt] Waiting for: '{pendingTopic}' (length: {pendingTopic.Length})");
                        // _logDebug($"[HsysMqtt] Received: '{topic}' (length: {topic.Length})");
                        // _logDebug($"[HsysMqtt] Are they equal? {pendingTopic.Equals(topic, StringComparison.Ordinal)}");
                    }
                }
            }
            catch (Exception ex)
            {
                // _logDebug($"[HsysMqtt] HandleResponseMessage error: {ex.Message}");
                // _logDebug($"[HsysMqtt] Stack trace: {ex.StackTrace}");
            }
            
            return Task.CompletedTask;
        }
        
        // Static method to handle messages for all instances
        public static Task HandleMessageForAllInstances(MqttApplicationMessageReceivedEventArgs e)
        {
            foreach (var instance in _instances)
            {
                try
                {
                    _ = instance.HandleResponseMessage(e);
                }
                catch (Exception ex)
                {
                    // _logDebug($"[HsysMqtt] Error in instance message handling: {ex.Message}");
                }
            }
            return Task.CompletedTask;
        }
    }
}
