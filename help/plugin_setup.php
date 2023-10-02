The triksc Plugin can be use to respond to Serial events by invoking FPP Commands.
<p>
For each Event added, the following fields need to be configured:
<p>
<ol>
<li>Description - this is a short description of what the event does.  This is ignored by FPP, but can be used to help you organized the events.</li>
<li>Condition - these are conditions to filter in/out events based on the bytes in the message sent from the MIDI device.  For example, you could apply a condition to only respond to button down states instead of up and down.</li>
<li>Command - the FPP Command to execute.
<p>
If the parameter starts with a single equal sign, it will be evaluated as a simple mathamatical formula.  For example, you can create a red color that is scaled from the velocity of the key press (usually byte 3, values 0-127) by using a formula like "=rgb(b3*2,0,0)".  You can also use variable names for the various parts of: "note" for the note (same as b2), "velocity" (same as b3), "channel" (lower 4 bits of b1), and pitch (b3 and b2, range -8192 to 8191).  For example, the formula above can be "=rgb(velocity*2,0,0)".
<p>
<p>
The "Last Messages" section in the upper right displays the last 25 messages that FPPD has received.  Clicking Refresh will refresh the list.  These can be used to help identify which parameters are being used to help define conditions.
