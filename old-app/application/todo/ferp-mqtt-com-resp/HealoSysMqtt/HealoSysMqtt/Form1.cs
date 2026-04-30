namespace HealoSysMqtt
{
    using HealoSysMqtt.Libs;
    using HealoSysMqtt.Libs.Mqtt;

    using MQTTnet;
    using MQTTnet.Formatter;
    using System.Buffers.Text;
    using System.Buffers;
    using System.Diagnostics;


    public partial class Form1 : Form
    {
        private IMqttClient mqttClient = null!;
        //AppConfig appConfig = new AppConfig();

        public Form1()
        {
            InitializeComponent();
            InitializeMqttClient();
        }

        private void InitializeMqttClient()
        {
            var mqttFactory = new MqttClientFactory();
            mqttClient = mqttFactory.CreateMqttClient();

            mqttClient.ConnectedAsync += async e =>
            {
                SetStatus("Connected");
                await Task.CompletedTask;
            };

            mqttClient.DisconnectedAsync += async e =>
            {
                SetStatus("Disconnected");
                await Task.CompletedTask;
            };

            // Add global message handler for debugging - this should catch ALL messages
            mqttClient.ApplicationMessageReceivedAsync += async e =>
            {
                // debugLog($"[GLOBAL] === MESSAGE RECEIVED ===");
                // debugLog($"[GLOBAL] Topic: {e.ApplicationMessage.Topic}");
                // debugLog($"[GLOBAL] QoS: {e.ApplicationMessage.QualityOfServiceLevel}");
                // debugLog($"[GLOBAL] Retain: {e.ApplicationMessage.Retain}");
                // debugLog($"[GLOBAL] Payload length: {e.ApplicationMessage.Payload.Length}");

                // Convert payload to string for debugging
                try
                {
                    var payloadStr = e.ApplicationMessage.ConvertPayloadToString();
                    // debugLog($"[GLOBAL] Payload as string: {payloadStr}");
                }
                catch (Exception ex)
                {
                    // debugLog($"[GLOBAL] Failed to convert payload to string: {ex.Message}");
                }

                // debugLog($"[GLOBAL] === END MESSAGE ===");

                // Forward to all HsysMqtt instances - but only for response topics
                if (e.ApplicationMessage.Topic.Contains("sas/v1/dev/response"))
                {
                    //debugLog("[GLOBAL] This is a response topic, forwarding to HsysMqtt...");
                    try
                    {
                        await HsysMqtt.HandleMessageForAllInstances(e);
                    }
                    catch (Exception ex)
                    {
                        // debugLog($"[GLOBAL] Error forwarding to HsysMqtt: {ex.Message}");
                    }
                }
                else
                {
                    // debugLog("[GLOBAL] This is NOT a response topic, not forwarding to HsysMqtt");
                }

                await Task.CompletedTask;
            };
        }

        private async Task<bool> ConnectMQTT()
        {
            bool isConnectionSuccess = false;

            string ip = txtMqttUrl.Text.Trim();
            if (string.IsNullOrEmpty(ip))
            {
                MessageBox.Show("Invalid IP or URL");
                return false;
            }

            string portText = txtPort.Text.Trim();
            if (!int.TryParse(portText, out int port))
            {
                MessageBox.Show("Invalid port number");
                return false;
            }

            //var mqttFactory = new MqttClientFactory(); // Correct for MQTTnet v5
            //var mqttClient = mqttFactory.CreateMqttClient();

            var mqttClientOptions = new MqttClientOptionsBuilder()
                .WithTcpServer(ip, port)
                .WithProtocolVersion(MQTTnet.Formatter.MqttProtocolVersion.V500)
                .Build();

            try
            {
                await mqttClient.ConnectAsync(mqttClientOptions, CancellationToken.None);
                lblStatus.Text = "Connected.";
                isConnectionSuccess = true;

                // Test subscription to verify MQTT client can receive messages
                debugLog("=== Testing MQTT subscription ===");
                var testSubscribeOptions = new MqttClientSubscribeOptionsBuilder()
                    .WithTopicFilter("sas/v1/dev/response/+/+")  // Wildcard to catch any response
                    .Build();

                var testSubResult = await mqttClient.SubscribeAsync(testSubscribeOptions);
                debugLog($"Test subscription result: {testSubResult.Items.FirstOrDefault()?.ResultCode}");

                // Also subscribe to a broader pattern to catch any hsys messages
                var broadSubscribeOptions = new MqttClientSubscribeOptionsBuilder()
                    .WithTopicFilter("sas/+/+/+/+")
                    .Build();

                var broadSubResult = await mqttClient.SubscribeAsync(broadSubscribeOptions);
                debugLog($"Broad subscription result: {broadSubResult.Items.FirstOrDefault()?.ResultCode}");

                //MqttPublisher.PublishStartupMessage(mqttClient, appConfig);

            }
            catch (Exception ex)
            {
                lblStatus.Text = "Connection failed: " + ex.Message;
            }

            return isConnectionSuccess;
        }
        private async Task<bool> DisconnectMQTT()
        {
            bool isDisconnected = false;

            if (mqttClient == null || !mqttClient.IsConnected)
            {
                lblStatus.Text = "Client is not connected.";
                return false;
            }

            try
            {
                await mqttClient.DisconnectAsync();
                lblStatus.Text = "Disconnected.";
                isDisconnected = true;
            }
            catch (Exception ex)
            {
                lblStatus.Text = "Disconnection failed: " + ex.Message;
            }

            return isDisconnected;
        }

        private void changeForConnected()
        {
            txtMqttUrl.Enabled = true;
            txtPort.Enabled = true;
            BtnConnect.Text = "Connect";
        }

        private void changeForDisconnected()
        {
            txtMqttUrl.Enabled = false;
            txtPort.Enabled = false;
            BtnConnect.Text = "Disconnect";
        }

        private async void BtnConnect_Click(object sender, EventArgs e)
        {
            if (mqttClient != null && mqttClient.IsConnected)
            {
                bool disconnected = await DisconnectMQTT();
                changeForConnected();
            }
            else
            {
                bool connected = await ConnectMQTT();
                changeForDisconnected();
            }
        }

        private void SetStatus(string message)
        {
            if (InvokeRequired)
            {
                Invoke(new Action(() => lblStatus.Text = message));
            }
            else
            {
                lblStatus.Text = message;
            }
        }

        private void btnBrowse_Click(object sender, EventArgs e)
        {
            using (OpenFileDialog openFileDialog = new OpenFileDialog())
            {
                openFileDialog.Filter = "BIN files (*.bin)|*.bin";
                openFileDialog.Title = "Select Firmware File";

                if (openFileDialog.ShowDialog() == DialogResult.OK)
                {
                    string selectedFilePath = openFileDialog.FileName;
                    // Display it in a textbox or handle it
                    txtBrowse.Text = selectedFilePath;

                    // Optional: log or show message
                    lblStatus.Text = "Firmware file selected.";
                }
                else
                {
                    lblStatus.Text = "No file selected.";
                }
            }
        }

        public enum DownloadStatus
        {
            Starting,
            InProgress,
            Completed,
            Failed,
            Aborted,
            Canceled
        }

        public delegate void OnCommandResponseHandler(byte status, string note);
        public delegate void OnDownloadStateChangedHandler(DownloadStatus status, string note, double progress);
        UInt32 msgIdx = 0;

        private static string GenerateRandomString(int length)
        {
            const string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
            var random = new Random();
            return new string(Enumerable.Repeat(chars, length)
                .Select(s => s[random.Next(s.Length)]).ToArray());
        }

        public async Task DownloadFile(string filePath, OnDownloadStateChangedHandler callback)
        {
            if (string.IsNullOrEmpty(filePath) || !File.Exists(filePath))
            {
                callback?.Invoke(DownloadStatus.Failed, "Parameter Err", 0);
                return;
            }

            // Capture instance variables explicitly
            var capturedClient = mqttClient;
            //string capturedDeviceID = "48E7293310440000"; // cmbSelectDevice.Text;
            var capturedDeviceID = cmbSelectDevice.Text;
            var capturedMsgIdx = msgIdx++;

            var hsysMqtt = new HsysMqtt("sas/v1/dev/command", "sas/v1/dev/response", debugLog);

            await Task.Run(async () =>
            {
                FileStream? file = null;

                try
                {
                    callback?.Invoke(DownloadStatus.Starting, "Starting", 0);

                    file = new FileStream(filePath, FileMode.Open, FileAccess.Read);
                    UInt32 totalSize = (UInt32)file.Length;

                    string response_pipe = GenerateRandomString(6);

                    debugLog($"Starting OTA to device {capturedDeviceID}, size: {totalSize} bytes Response pipe: {response_pipe}");

                    var frame = new HsysCmdRespFrame(HsysCommandId.CMD_OTA_START, 0x00, 0x00, response_pipe, 1);
                    frame.Data = HsysCmdRespFrame.CreateOtaStartCommandData(HsysOtaId.OTAMAIN, totalSize);
                    byte[] frameBytes = frame.ToByteArray();
                    
                    debugLog("Sending : \r\n" + frame.toPrintableString());
                    debugLog("TX: " + BitConverter.ToString(frameBytes).Replace("-", " "));
                    String EncodedMessage = Base64Encoder.Encode(frameBytes);

                    byte[] frameBytesAfterBase64 = EncodedMessage.ToCharArray().Select(c => (byte)c).ToArray();
                    var (success, responseBuffer, responseLength) = await hsysMqtt.CommandResponseAsync(capturedClient, capturedDeviceID, frameBytesAfterBase64, frameBytesAfterBase64.Length, response_pipe, 5000);

                    if (success && responseBuffer != null)
                    {
                        debugLog($"Response received: {responseLength} bytes");
                        debugLog("RX: " + BitConverter.ToString(responseBuffer).Replace("-", " "));

                        // Decode the response if it's base64 encoded
                        try
                        {
                            string responseString = System.Text.Encoding.UTF8.GetString(responseBuffer);
                            byte[] decodedResponse = Convert.FromBase64String(responseString);

                            debugLog("RX: " + BitConverter.ToString(decodedResponse).Replace("-", " "));

                            var receivedFrame = HsysCmdRespFrame.FromByteArray(decodedResponse);
                            var cmdResponseCode = HsysCmdRespFrame.ParseResponseStatus(receivedFrame.Data);
                            debugLog($"OTA Start response - ID: {cmdResponseCode}");

                            if (cmdResponseCode != HsysCmdStatus.OK)
                            {
                                callback?.Invoke(DownloadStatus.Starting, "Started", 0);
                                return;
                            }
                        }
                        catch (Exception ex)
                        {
                            debugLog($"Failed to decode response: {ex.Message}");
                            callback?.Invoke(DownloadStatus.Failed, ex.Message, 0);
                            return;
                        }
                    }
                    else
                    {
                        debugLog("Failed to receive response or timeout occurred");
                        callback?.Invoke(DownloadStatus.Failed, "Resp Timedout", 0);
                        return;
                    }

                    int readBytes = 0;
                    long offset = 0;
                    const int chunkSize = 512+128;
                    byte[] buffer = new byte[chunkSize];
                    UInt16 retry_count = 0;
                    const UInt16 MAX_RETRY_COUNT = 10;

                    while ((readBytes = FirmwareFileReader.ReadChunk(file, buffer, offset, chunkSize)) > 0)
                    {
                        response_pipe = GenerateRandomString(6);
                        // debugLog($"Sending data to device {capturedDeviceID}, size: {readBytes} bytes Response pipe: {response_pipe}");

                        var frameData = new HsysCmdRespFrame(HsysCommandId.CMD_OTA_DATA, 0x00, 0x00, response_pipe, 1);
                        frameData.Data = HsysCmdRespFrame.CreateOtaDataCommandData((UInt32)offset, buffer, readBytes);
                        byte[] frameDataBytes = frameData.ToByteArray();
                        
                        debugLog("Sending : \r\n" + frameData.toPrintableString());

                        // debugLog("TX: " + BitConverter.ToString(frameDataBytes).Replace("-", " "));
                        EncodedMessage = Base64Encoder.Encode(frameDataBytes);

                        byte[] frameBytesDataAfterBase64 = EncodedMessage.ToCharArray().Select(c => (byte)c).ToArray();
                        var (success_data, responseDataBuffer, responseDataLength) = await hsysMqtt.CommandResponseAsync(capturedClient, capturedDeviceID, frameBytesDataAfterBase64, frameBytesDataAfterBase64.Length, response_pipe, 5000);

                        if (success_data && responseDataBuffer != null)
                        {
                            // debugLog($"Response received: {responseDataLength} bytes");
                            // debugLog("RX: " + BitConverter.ToString(responseDataBuffer).Replace("-", " "));

                            // Decode the response if it's base64 encoded
                            try
                            {
                                string responseString = System.Text.Encoding.UTF8.GetString(responseDataBuffer);
                                byte[] decodedResponse = Convert.FromBase64String(responseString);

                                // debugLog("RX: " + BitConverter.ToString(decodedResponse).Replace("-", " "));

                                var receivedFrame = HsysCmdRespFrame.FromByteArray(decodedResponse);
                                var (otaExpectedOffset, cmdResponseCode) = HsysCmdRespFrame.ParseOtaDataResponse(receivedFrame.Data);
                                // debugLog($"OTA Data response - ID: {cmdResponseCode} Offset: {otaExpectedOffset}");

                                if (cmdResponseCode != HsysCmdStatus.OK)
                                {
                                    retry_count++;
                                    if (retry_count < MAX_RETRY_COUNT)
                                    {
                                        offset = otaExpectedOffset;
                                        debugLog("Retry, ");
                                        continue;
                                    }

                                    callback?.Invoke(DownloadStatus.Failed, " Retry Count Exceeded", 0);
                                    return;
                                }

                                retry_count = 0;
                            }
                            catch (Exception ex)
                            {
                                retry_count++;
                                if (retry_count < MAX_RETRY_COUNT)
                                {
                                    debugLog("Retry");
                                    continue;
                                }

                                debugLog($"Failed to decode response: {ex.Message}");
                                callback?.Invoke(DownloadStatus.Failed, " Retry Count Exceeded", 0);
                                return;
                            }
                        }
                        else
                        {
                            retry_count++;
                            if (retry_count < MAX_RETRY_COUNT)
                            {
                                debugLog("Retry");
                                continue;
                            }

                            debugLog("Failed to receive response or timeout occurred");
                            callback?.Invoke(DownloadStatus.Failed, " Retry Count Exceeded", 0);
                            return;
                        }

                        retry_count = 0;
                        offset += readBytes;

                        double progress = (offset * 100.0) / totalSize;
                        callback?.Invoke(DownloadStatus.InProgress, "Downloading...", Math.Min(progress, 100.0));
                    }

                    response_pipe = GenerateRandomString(6);

                    debugLog($"Completing OTA to device {capturedDeviceID}, size: {totalSize} bytes Response pipe: {response_pipe}");

                    frame = new HsysCmdRespFrame(HsysCommandId.CMD_OTA_COMPLETE, 0x00, 0x00, response_pipe, 1);
                    frame.Data = HsysCmdRespFrame.CreateOtaStartCommandData(HsysOtaId.OTAMAIN, totalSize);
                    frameBytes = frame.ToByteArray();
                    
                    debugLog("Sending : \r\n" + frame.toPrintableString());

                    debugLog("TX: " + BitConverter.ToString(frameBytes).Replace("-", " "));
                    EncodedMessage = Base64Encoder.Encode(frameBytes);

                    frameBytesAfterBase64 = EncodedMessage.ToCharArray().Select(c => (byte)c).ToArray();
                    var (successComplete, responseBufferComplete, responseLengthComplete) = await hsysMqtt.CommandResponseAsync(capturedClient, capturedDeviceID, frameBytesAfterBase64, frameBytesAfterBase64.Length, response_pipe, 5000);

                    if (successComplete && responseBufferComplete != null)
                    {
                        debugLog($"Response received: {responseLengthComplete} bytes");
                        debugLog("RX: " + BitConverter.ToString(responseBufferComplete).Replace("-", " "));

                        // Decode the response if it's base64 encoded
                        try
                        {
                            string responseString = System.Text.Encoding.UTF8.GetString(responseBufferComplete);
                            byte[] decodedResponse = Convert.FromBase64String(responseString);

                            debugLog("RX: " + BitConverter.ToString(decodedResponse).Replace("-", " "));

                            var receivedFrame = HsysCmdRespFrame.FromByteArray(decodedResponse);
                            var cmdResponseCode = HsysCmdRespFrame.ParseResponseStatus(receivedFrame.Data);
                            debugLog($"OTA Complete response - ID: {cmdResponseCode}");

                            if (cmdResponseCode != HsysCmdStatus.OK)
                            {
                                callback?.Invoke(DownloadStatus.Failed, "Device Err", 0);
                                return;
                            }
                        }
                        catch (Exception ex)
                        {
                            debugLog($"Failed to decode response: {ex.Message}");
                            callback?.Invoke(DownloadStatus.Failed, "Decoding Failed", 0);
                            return;
                        }
                    }
                    else
                    {
                        debugLog("Failed to receive response or timeout occurred");
                        callback?.Invoke(DownloadStatus.Failed, " Retry Count Exceeded", 0);
                        return;
                    }

                    callback?.Invoke(DownloadStatus.Completed, "Completed", 100.0);
                }
                catch (Exception ex)
                {
                    debugLog($"OTA Error: {ex.Message}");
                    callback?.Invoke(DownloadStatus.Failed, ex.Message, 0);
                }
                finally
                {
                    file?.Dispose();
                }
            });
        }

        private void OnDownloadStateChanged(DownloadStatus status, string note, double progress)
        {
            Invoke(() =>
            {
                lblProgress.Text = $"{status} : {note} - {progress:F2}%";
                progressBarOta.Value = (int)progress;
                lblProgress.Left = progressBarOta.Left + (progressBarOta.Width - lblStatus.Width) / 2;
                //lblProgress.Top = progressBarOta.Top + (progressBarOta.Height - lblStatus.Height) / 2;
            });
        }

        private void OnCommandResponse(byte ResponseCode, String note)
        {

        }

        private async void btnOtaStart_Click(object sender, EventArgs e)
        {
            string filePath = txtBrowse.Text;
            await DownloadFile(filePath, OnDownloadStateChanged);
        }

        private async void resetDevice(String capturedDeviceID, OnCommandResponseHandler callback)
        {
            var capturedClient = mqttClient;
            var hsysMqtt = new HsysMqtt("sas/v1/dev/command", "sas/v1/dev/response", debugLog);

            string response_pipe = GenerateRandomString(6);

            debugLog($"Reset Device {capturedDeviceID}, bytes Response pipe: {response_pipe}");

            var frame = new HsysCmdRespFrame(HsysCommandId.CMD_DEVICE_RESET, 0x00, 0x00, response_pipe, 1);
            char[] pw = capturedDeviceID.ToCharArray();
            frame.Data = HsysCmdRespFrame.CreateDeviceResetCommandData(pw, pw.Length);
            byte[] frameBytes = frame.ToByteArray();

            debugLog("Sending : \r\n" + frame.toPrintableString());

            debugLog("TX: " + BitConverter.ToString(frameBytes).Replace("-", " "));
            String EncodedMessage = Base64Encoder.Encode(frameBytes);

            byte[] frameBytesAfterBase64 = EncodedMessage.ToCharArray().Select(c => (byte)c).ToArray();
            var (success, responseBuffer, responseLength) = await hsysMqtt.CommandResponseAsync(capturedClient, capturedDeviceID, frameBytesAfterBase64, frameBytesAfterBase64.Length, response_pipe, 5000);

            if (success && responseBuffer != null)
            {
                debugLog($"Response received: {responseLength} bytes");
                debugLog("RX: " + BitConverter.ToString(responseBuffer).Replace("-", " "));

                // Decode the response if it's base64 encoded
                try
                {
                    string responseString = System.Text.Encoding.UTF8.GetString(responseBuffer);
                    byte[] decodedResponse = Convert.FromBase64String(responseString);

                    debugLog("RX: " + BitConverter.ToString(decodedResponse).Replace("-", " "));

                    var receivedFrame = HsysCmdRespFrame.FromByteArray(decodedResponse);
                    var cmdResponseCode = HsysCmdRespFrame.ParseResponseStatus(receivedFrame.Data);
                    debugLog($"OTA Start response - ID: {cmdResponseCode}");

                    if (cmdResponseCode != HsysCmdStatus.OK)
                    {
                        callback?.Invoke((byte)cmdResponseCode, "Done");
                        return;
                    }
                }
                catch (Exception ex)
                {
                    debugLog($"Failed to decode response: {ex.Message}");
                    callback?.Invoke((byte)255, "Error");
                    return;
                }
            }
            else
            {
                debugLog("Failed to receive response or timeout occurred");
                callback?.Invoke((byte)255, "Timeout");
                return;
            }
        }

        private async void button1_Click(object sender, EventArgs e)
        {
            var capturedDeviceID = cmbSelectDevice.Text;
            resetDevice(capturedDeviceID, OnCommandResponse);
        }

        private void debugLog(string message)
        {
            if (rtb_DebugLogs.InvokeRequired)
            {
                rtb_DebugLogs.Invoke(new Action(() =>
                {
                    rtb_DebugLogs.AppendText($"{GetTimestamp()} {message}" + Environment.NewLine);
                }));
            }
            else
            {
                rtb_DebugLogs.AppendText($"{GetTimestamp()} {message}" + Environment.NewLine);
            }
        }
        
        private static string GetTimestamp()
        {
            return DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff");                
        }

    }
}
