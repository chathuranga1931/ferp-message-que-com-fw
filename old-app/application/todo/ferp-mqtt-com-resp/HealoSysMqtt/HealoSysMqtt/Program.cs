namespace HealoSysMqtt
{
    internal static class Program
    {
        /// <summary>
        ///  The main entry point for the application.
        /// </summary>

        // [System.Runtime.InteropServices.DllImport("kernel32.dll")]
        // private static extern bool AllocConsole();

        [STAThread]
        static void Main()
        {
            // To customize application configuration such as set high DPI settings or default font,
            // see https://aka.ms/applicationconfiguration.

            // AllocConsole();
            // Console.WriteLine("Now I have a console!");
            ApplicationConfiguration.Initialize();
            Application.Run(new Form1());
        }
    }
}