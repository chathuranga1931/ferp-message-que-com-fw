using System;

public static class Base64Encoder
{
    /// <summary>
    /// Encodes a byte array to a Base64 string.
    /// </summary>
    /// <param name="data">Byte array input</param>
    /// <returns>Base64 encoded string</returns>
    public static string Encode(byte[] data)
    {
        if (data == null || data.Length == 0)
        {
            throw new ArgumentException("Data to encode cannot be null or empty.");
        }

        return Convert.ToBase64String(data);
    }
}
