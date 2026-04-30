using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.IO;
using System.Drawing;
using System.Security.Cryptography;

namespace HealoSysMqtt.Libs.Mqtt
{
    public enum HsysCommandId : ushort
    {
        CMD_OTA_START = 0x0001,
        CMD_OTA_DATA = 0x0002,
        CMD_OTA_COMPLETE = 0x0003,
        CMD_OTA_GET_STATUS = 0x0004,
        CMD_GET_FW_VERSION = 0x0005,
        CMD_GET_FW_VERSION_SUB_1 = 0x0006,
        CMD_DEVICE_RESET = 0x0007,
    }

    public enum HsysOtaId : byte
    {
        OTAMAIN = 0x00,
        OTASUB_1 = 0x01 // Ex - ESP07 Firmware
    }

    public enum HsysCmdStatus : byte
    {
        OK = 0x00,
        ERROR = 0x01,
        INVALID_COMMAND = 0x02,
        INVALID_DATA = 0x03,
        CRC_ERROR = 0x04,
        OTA_ERROR = 0x05
    }

    public enum HsysOtaStatus : byte
    {
        OTAIDLE = 0x00,
        OTAINPROGRESS = 0x01,
        OTACOMPLETED = 0x02
    }

    public class HsysCmdRespFrame
    {
        /*
         * Consideration During designing the protocol 
         * 
            🔎⚡🔑Current Protocol Design                
            [STX - 4B][VER - 1B][CMDID - 2B][RSP-PIPE-ID - 6B][SEQ NU - 1B][SIZE - 4B][DATA - 1K][CRC32 - 4B]
            STX - Startup Code "HSYS"
            VER - Protocol Version "0x00" means 0.0
            CMDID - Command IDs                    
            CRC32 - is for full command                    
                    
            CMDID List
            ==========                
                CMD_OTA_START
                    - DATA (Command)
                    [OTAID - 1B][OTASIZE - 4B]
                        OTAID 
                            OTAMAIN - 0x00
                            OTASUB_1 - 0x01 Ex - ESP07 Firmware
                        OTASIZE 
                            Size of the ongoing OTA
                    - DATA(Response)
                    [CMDSTATUS - 1B]
                        CMDSTATUS - Error Code, 0x00 OK
                        
                CMD_OTA_DATA
                    - DATA (Command)
                    [OTAOFFSET  - 4B][OTADATABUFFER - 512]
                        OTAOFFSET - Offset of the data from the OTA bin file
                        OTADATABUFFER - Data buffer of the OTA Bin file   
                    - DATA(Response)
                    [CMDSTATUS - 1B]
                        CMDSTATUS - Error Code, 0x00 OK                             
                        
                CMD_OTA_COMPLETE
                    - DATA (Command)
                    [OTACRC32 - 4B]
                        OTACRC32 - Calculated CRC32 for the completed binary      
                    - DATA(Response)
                    [CMDSTATUS - 1B]
                        CMDSTATUS - Error Code, 0x00 OK
                        
                CMD_OTA_GET_STATUS
                    - DATA (Command)
                        None
                    - DATA(Response)
                        [CMDSTATUS - 1B][OTASTATUS - 1B]
                            - OTAIDLE - 0x00
                            - OTAINPROGRESS - 0x01
                            - OTACOMPLETED - 0x02                                
                        
                CMD_GET_FW_VERSION
                    - DATA (Command)
                        None
                    - DATA(Response)
                        [FWVERSION - 4B]
                        
                CMD_GET_FW_VERSION_SUB_1
        * 
        *   
        */

        // Protocol constants
        public const string STX = "HSYS";
        public const byte PROTOCOL_VERSION = 0x00;
        public const int MAX_DATA_SIZE = 2048;
        public const int OTA_BUFFER_SIZE = 1024;
        
        // Frame structure properties
        public string StartCode { get; set; } = STX;
        public byte Version { get; set; } = PROTOCOL_VERSION;
        public byte Flags { get; set; } = 0x00;
        public byte Type { get; set; } = 0x00;
        public HsysCommandId CommandId { get; set; }
        public string ResponsePipeId { get; set; } = "";
        public byte SequenceNumber { get; set; }
        public uint DataSize { get; set; }
        public byte[] Data { get; set; } = Array.Empty<byte>();
        public uint Crc32 { get; set; }

        public HsysCmdRespFrame()
        {
        }

        public HsysCmdRespFrame(HsysCommandId commandId, byte flags, byte type, string responsePipeId, byte sequenceNumber)
        {
            CommandId = commandId;
            Flags = flags;
            Type = type;
            ResponsePipeId = responsePipeId.PadRight(6).Substring(0, 6); // Ensure 6 bytes
            SequenceNumber = sequenceNumber;
        }

        public String toPrintableString()
        {
            String str = "";

            str += "Version : " + Version.ToString("X2") + "\r\n";
            str += "Flags : " + Flags.ToString("X2") + "\r\n";
            str += "Type : " + Type.ToString("X2") + "\r\n";
            str += "Command ID : " + ((ushort)CommandId).ToString("X4") + "\r\n";
            str += "Response Pipe ID : " + ResponsePipeId + "\r\n";
            str += "Sequence Number : " + SequenceNumber.ToString("X2") + "\r\n";
            str += "Data Size : " + DataSize.ToString() + "\r\n";
            str += "CRC32 : " + Crc32.ToString("X8") + "\r\n";

            return str;
        }

        // Serialize the frame to byte array
        public byte[] ToByteArray()
        {
            using var ms = new MemoryStream();
            using var writer = new BinaryWriter(ms);

            // STX - 4 bytes
            writer.Write(Encoding.ASCII.GetBytes(StartCode.PadRight(4).Substring(0, 4)));
            
            // VER - 1 byte
            writer.Write(Version);

            // FLAGS - 1 byte
            writer.Write(Flags);

            // TYPE - 1 byte
            writer.Write(Type);
            
            // CMDID - 2 bytes
            writer.Write((ushort)CommandId);
            
            // RSP-PIPE-ID - 6 bytes
            var pipeIdBytes = Encoding.ASCII.GetBytes(ResponsePipeId.PadRight(6).Substring(0, 6));
            writer.Write(pipeIdBytes);
            
            // SEQ NU - 1 byte
            writer.Write(SequenceNumber);
            
            // SIZE - 4 bytes
            DataSize = (uint)Data.Length;
            writer.Write(DataSize);
            
            // DATA - variable size
            writer.Write(Data);
            
            // Calculate CRC32 for the entire frame (excluding CRC32 field itself)
            byte[] frameWithoutCrc = ms.ToArray();
            Crc32 = CalculateCrc32(frameWithoutCrc);
            
            // CRC32 - 4 bytes
            writer.Write(Crc32);

            return ms.ToArray();
        }

        // Deserialize from byte array
        public static HsysCmdRespFrame FromByteArray(byte[] data)
        {
            if (data.Length < 24) // Minimum frame size without data (4+1+1+1+2+6+1+4+4)
                throw new ArgumentException("Invalid frame size");

            using var ms = new MemoryStream(data);
            using var reader = new BinaryReader(ms);

            var frame = new HsysCmdRespFrame();

            // STX - 4 bytes
            frame.StartCode = Encoding.ASCII.GetString(reader.ReadBytes(4)).TrimEnd('\0');
            
            // VER - 1 byte
            frame.Version = reader.ReadByte();

            // FLAGS - 1 byte
            frame.Flags = reader.ReadByte();

            // TYPE - 1 byte
            frame.Type = reader.ReadByte();
            
            // CMDID - 2 bytes
            frame.CommandId = (HsysCommandId)reader.ReadUInt16();
            
            // RSP-PIPE-ID - 6 bytes
            frame.ResponsePipeId = Encoding.ASCII.GetString(reader.ReadBytes(6)).TrimEnd('\0');
            
            // SEQ NU - 1 byte
            frame.SequenceNumber = reader.ReadByte();
            
            // SIZE - 4 bytes
            frame.DataSize = reader.ReadUInt32();
            
            // DATA - variable size
            if (frame.DataSize > 0)
                frame.Data = reader.ReadBytes((int)frame.DataSize);
            
            // CRC32 - 4 bytes
            frame.Crc32 = reader.ReadUInt32();

            // Verify CRC32
            byte[] frameForCrc = new byte[data.Length - 4];
            Array.Copy(data, 0, frameForCrc, 0, frameForCrc.Length);
            uint calculatedCrc = CalculateCrc32(frameForCrc);
            
            if (calculatedCrc != frame.Crc32)
                throw new InvalidDataException("CRC32 verification failed");

            return frame;
        }

        // Command-specific data builders

        // CMD_RESET Command data
        public static byte[] CreateDeviceResetCommandData(char [] pw, int pwLength)
        {
            using var ms = new MemoryStream();
            using var writer = new BinaryWriter(ms);

            writer.Write(pw, 0, pwLength);

            return ms.ToArray();
        }

        // CMD_RESET response data
        public static byte[] CreateDeviceResetResponseData(HsysCmdStatus status)
        {
            return new byte[] { (byte)status };
        }

        // CMD_OTA_START command data
        public static byte[] CreateOtaStartCommandData(HsysOtaId otaId, UInt32 otaSize)
        {
            using var ms = new MemoryStream();
            using var writer = new BinaryWriter(ms);
            
            writer.Write((byte)otaId);
            writer.Write(otaSize);
            
            return ms.ToArray();
        }

        // CMD_OTA_START response data
        public static byte[] CreateOtaStartResponseData(HsysCmdStatus status)
        {
            return new byte[] { (byte)status };
        }

        // CMD_OTA_DATA command data
        public static byte[] CreateOtaDataCommandData(UInt32 otaOffset, byte[] otaDataBuffer, int size)
        {
            if (size > OTA_BUFFER_SIZE)
                throw new ArgumentException($"OTA data buffer cannot exceed {OTA_BUFFER_SIZE} bytes");

            using var ms = new MemoryStream();
            using var writer = new BinaryWriter(ms);
            
            writer.Write((byte)(((otaOffset) >> 24) & 0xFF));
            writer.Write((byte)(((otaOffset) >> 16) & 0xFF));
            writer.Write((byte)(((otaOffset) >>  8) & 0xFF));
            writer.Write((byte)(((otaOffset) >>  0) & 0xFF));
            writer.Write(otaDataBuffer, 0, size);
            
            return ms.ToArray();
        }

        // CMD_OTA_DATA response data
        public static byte[] CreateOtaDataResponseData(HsysCmdStatus status)
        {
            return new byte[] { (byte)status };
        }

        // CMD_OTA_COMPLETE command data
        public static byte[] CreateOtaCompleteCommandData(uint otaCrc32)
        {
            using var ms = new MemoryStream();
            using var writer = new BinaryWriter(ms);
            
            writer.Write(otaCrc32);
            
            return ms.ToArray();
        }

        // CMD_OTA_COMPLETE response data
        public static byte[] CreateOtaCompleteResponseData(HsysCmdStatus status)
        {
            return new byte[] { (byte)status };
        }

        // CMD_OTA_GET_STATUS response data
        public static byte[] CreateOtaGetStatusResponseData(HsysCmdStatus cmdStatus, HsysOtaStatus otaStatus)
        {
            return new byte[] { (byte)cmdStatus, (byte)otaStatus };
        }

        // CMD_GET_FW_VERSION response data
        public static byte[] CreateGetFwVersionResponseData(uint fwVersion)
        {
            using var ms = new MemoryStream();
            using var writer = new BinaryWriter(ms);
            
            writer.Write(fwVersion);
            
            return ms.ToArray();
        }

        // Simple CRC32 implementation
        private static uint CalculateCrc32(byte[] data)
        {
            uint crc = 0xFFFFFFFF;
            uint[] table = GenerateCrc32Table();

            foreach (byte b in data)
            {
                crc = (crc >> 8) ^ table[(crc ^ b) & 0xFF];
            }

            return ~crc;
        }

        private static uint[] GenerateCrc32Table()
        {
            uint[] table = new uint[256];
            uint polynomial = 0xEDB88320;

            for (uint i = 0; i < 256; i++)
            {
                uint crc = i;
                for (int j = 0; j < 8; j++)
                {
                    if ((crc & 1) == 1)
                        crc = (crc >> 1) ^ polynomial;
                    else
                        crc >>= 1;
                }
                table[i] = crc;
            }

            return table;
        }

        // Helper methods to parse command-specific data

        // Parse OTA_START command data
        public static (HsysOtaId otaId, uint otaSize) ParseOtaStartCommandData(byte[] data)
        {
            if (data.Length < 5)
                throw new ArgumentException("Invalid OTA start command data size");

            using var ms = new MemoryStream(data);
            using var reader = new BinaryReader(ms);

            var otaId = (HsysOtaId)reader.ReadByte();
            var otaSize = reader.ReadUInt32();

            return (otaId, otaSize);
        }

        // Parse OTA_DATA command data
        public static (uint otaOffset, byte[] otaDataBuffer) ParseOtaDataCommandData(byte[] data)
        {
            if (data.Length < 4)
                throw new ArgumentException("Invalid OTA data command data size");

            using var ms = new MemoryStream(data);
            using var reader = new BinaryReader(ms);

            var otaOffset = reader.ReadUInt32();
            var remainingBytes = data.Length - 4;
            var otaDataBuffer = reader.ReadBytes(remainingBytes);

            return (otaOffset, otaDataBuffer);
        }

        // Parse response status
        public static HsysCmdStatus ParseResponseStatus(byte[] data)
        {
            if (data.Length < 1)
                throw new ArgumentException("Invalid response data size");

            return (HsysCmdStatus)data[0];
        }

        // Parse response status
        public static (UInt32 expected_offset, HsysCmdStatus otaStatus) ParseOtaDataResponse(byte[] data)
        {
            if (data.Length < 5)
                throw new ArgumentException("Invalid response data size");

            UInt32 exp_offset = (UInt32)((data[1] << 24))
                | (UInt32)((data[2] << 16))
                | (UInt32)((data[3] <<  8))
                | (UInt32)((data[4] <<  0));

            return ((UInt32)exp_offset, (HsysCmdStatus)data[0]);
        }

        // Parse OTA status response
        public static (HsysCmdStatus cmdStatus, HsysOtaStatus otaStatus) ParseOtaStatusResponse(byte[] data)
        {
            if (data.Length < 2)
                throw new ArgumentException("Invalid OTA status response data size");

            return ((HsysCmdStatus)data[0], (HsysOtaStatus)data[1]);
        }

        // Parse firmware version response
        public static uint ParseFwVersionResponse(byte[] data)
        {
            if (data.Length < 4)
                throw new ArgumentException("Invalid firmware version response data size");

            using var ms = new MemoryStream(data);
            using var reader = new BinaryReader(ms);

            return reader.ReadUInt32();
        }
    }
}
