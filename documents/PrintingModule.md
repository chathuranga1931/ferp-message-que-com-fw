Now I want to add new module named, 
ModulePriting, 
What this should do is to subscribe to pumped events, And subscribed to print button events, both short and long
Once this received a pumped event, it should send a request to the url set itn the config, printer_url, #sym:CFG_KEY_PRINTER_URL 
And once the configs loaded and wifi is connected, then only this module should start to operate. 
So, once the wifi is connected, and config ready, fet the pritner url 
And once printer short button is pressed event received, and after n seconds which is defined #sym:CFG_KEY_PRINT_DELAY_MS  in milliseconds, then send a print request it is a post request to this URL, and just http not https, use http interface to send this request, 
It will have a respond, if the respond is not received for some time, need to retry 2 times, 
If still fails, publish pirnt failed message and success publish print ok message

The data should contains the pumped event ID, this is print count for the nozzle, this should be sent by the FUEL module when event is published, I am not sure this is implemented or not, if it is not will do it later. 

Check the printer client for more details of the old app. 

Nozzle Event ID is not implemented, we need to impement this, 
            if(_device_configs->enable_nid_print){
                measurements["NE_ID"] = get_unique_event_id(n_event->time_stamp, _device_configs->nozel_configs[nozzel_id].nozzel_id);
                measurements["ABS_ID"] = n_event->event_id;

        

