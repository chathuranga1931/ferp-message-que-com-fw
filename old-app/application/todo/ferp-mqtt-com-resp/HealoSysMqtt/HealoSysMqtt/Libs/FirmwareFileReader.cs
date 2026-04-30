using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System;
using System.IO;

namespace HealoSysMqtt.Libs
{
    public static class FirmwareFileReader
    {
        /// <summary>
        /// Opens a binary file and returns a FileStream pointer.
        /// </summary>
        public static FileStream OpenFile(string filePath)
        {
            if (string.IsNullOrEmpty(filePath) || !File.Exists(filePath))
                throw new FileNotFoundException("Invalid file path.");

            return new FileStream(filePath, FileMode.Open, FileAccess.Read);
        }

        /// <summary>
        /// Reads a chunk of data from the binary file starting at the specified offset.
        /// </summary>
        /// <param name="file">Open FileStream (from OpenFile)</param>
        /// <param name="chunkBuffer">Pre-allocated byte array to hold data</param>
        /// <param name="offset">Byte offset in the file</param>
        /// <param name="chunkSize">Number of bytes to read</param>
        /// <returns>Number of bytes actually read</returns>
        public static int ReadChunk(FileStream file, byte[] chunkBuffer, long offset, int chunkSize)
        {
            if (file == null || chunkBuffer == null)
                throw new ArgumentNullException("file or chunkBuffer is null");

            if (!file.CanRead)
                throw new IOException("File is not readable.");

            if (offset >= file.Length)
                return 0; // End of file

            file.Seek(offset, SeekOrigin.Begin);

            int bytesToRead = Math.Min(chunkSize, (int)(file.Length - offset));
            return file.Read(chunkBuffer, 0, bytesToRead);
        }
    }
}
