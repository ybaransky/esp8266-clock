i am building a clock-like device that displays on a 3 4-segement displays. it has advanced modes, each of which consists of a combination of base modes. it also has the concept of a single primary mode, which the device must always be in.

the base modes are as follows:
- COUTDOWN: this is a clock which counts down to a user specified target time. the format (days, hours, seconds, tenths, ...0) is defined in a format section. when it hits its target it just displays 0 time.
- COUNTUP: this is a clock that counts up starting from a user specified start time. it has a seperate format specification
- CLOCK: this is a regular clock with its own format
- MESSAGE: this displays a message. the message could be too long to fit in the existing 3 segements so then it becomes a series of panels, one after another with a N second delay between them. the format of these messages can have different segemetns blinking.

the advanced modes are a combination of these base modes
- splash: this displays a MESSAGE and after some seconds transitions to the primary mode
- message: this displays a MESSAGE for some amount of time (which could be forever). it can also blink.
- countdown: this displays the COUNTDOWN mode and when it counts down to 0, it then displays a blinking MESSAGE forever.
- countup: this is displays the COUNTUP mode
- clock: this is jsut in the CLOCK mode.
- demo: this is a special case of the countdown mode with the expiration time to be 5 seconds from now, then transitions to a blinking message for 5 seconds.
- friday: the base mode this is in depends on the day of week. between sunset saturday to the start of friday it is in clock mode, from the start of friday to sunset friday itnisin COUNTDOWN mode with target time the sunset time, then it resets the expiration time to saturday sunset, and then it starts again.


 
