
- This is a application layer module, means applicaiton dependent like the main logic
- There should be a table, constant table, which has columns
    incomming msg Id, incomming source, outgoingmsg_id, outgoing_destination_id, msg_translator, delayed republish, delay.. etc
- The module should have its own thread, at start if no need we can share it later. 
- When initializing, it should subscribed to all the incomming messages. 
- When message received, it will go through the table and find what is the msg translator and call that function. 
- the message translator funciton should follow the same prototype, which has, incoming src idm, incomming msg, outgoing msg id, outgoing dest, and pointer to translated msg. 
- The function will be implemented in msg translater cpp, and will set during the compilation to the table
- the function will execute and returns the message, the msg received function will publish it back to the  msg queue. 
- then if there is another entry for the same message, the message received will execute that too and publish that translated message as well. once all the translated entry is done, it will exits
- you can use the msg received function to do all, no need to pass it to queue and operate later. 
