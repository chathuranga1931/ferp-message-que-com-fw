
OTA Testing
    MQTT - OK
        Process
            Preperation
            - Should have latest bundles, odd and even version
            - Read current FW version, if that is not the ODD one provided, flash new one using OTA
            - Verify the firmware is test bin, odd version. This is where the starting point
            - Start test report file, add text idx, and start recode in each step in local structure. 
              Later can put it to the report file. 

            Test Cycle.
            - Check MD5 for ODD version, binary MD5 note the bundle MD5. 
            - Run Simulator
            - Delete location of the bin file that is stored currently. 
            - Run OTA with MQTT
            - Make sure it is completed, to 100%
            - Check the SPIFFS folder to check the bin MD5 is correct.  
            - Wait till the reboot for given delay (30 Secs)
            - Version validation can not be done, becaseu the Simulator will not be appling the binary. 
            - Run the cycle for both files and make sure both are working
            - Once run the both odd and even bundles, that is a one test cycle, you can pass number of 
              cycles from the test script. 
    WebClient - NOTREADY
    WebServer - MAC PC has issue connecting, device

Pumping
    Params 
        Pump Type - (Sanki6, Cen6, Cen7, HongYang, Wayne ... etc)
        TestTypes   - Normal Pumping (Based on total price, below 1K, below 10K, below 100K, below 1000K)
                    - Totalizer (0, random values in every 10s of values till, 100000000.000)
                    - Normal Pumping with middle gaps (Run all the tests with intermidiete pumping gaps)
    Process
        Preperation
            - Run the simulator
            - Set the pump type
            - Select which test is running
            - Based on that, nozzle Up, start pump, if has multiple stops and starts, apply that 
            - Once the value is completed, stop pumping and nozzle down
            - Verify the event, cubesphere value, or the message pumped, value and the value you expected 
              are correc.

Printing
    Normal Printing 
        - Run the pumping for all pump types, for different total values
        - Do the printing request, and check the printing message send to the http server, you can wait 
          for the message to the printer, and compare the values are machine 
    Totalizer Printing
        - Set the pump values to indicate the totalizer
        - Press the button for longer time, longer press
        - Check the http data to be sent to the printer, has the correct values. 


Architecture


Simulator (C++)
{
    Firmware Applicaiton layer
    Mac Abstraciton layer (Backdoor Socket)
}

Simulator UI (Python)
{
    Python Connection to Firmware Simulator Backdoor
    UI Impementation
}

Automated Test Unit
{
    Python Connection to Firmware Simulator Backdoor
      |
    Pumping Eumulation Unit
    Device Output Monitoring Unit
}

Automated Test Scripts
    - Ota Test
        Start Simulator
        Start Automated Test Unit
        Connect MQTT
        Send Bundle, similar to Device Tool we used to update firmware
        Check the status from Automated Test Unit backdoor interface messages
        Check the file details from the Simulator file path
        
