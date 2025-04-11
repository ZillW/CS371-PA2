For task1, since I wanted to make the test more effective, I added the usleep() function to artificially increase the server delay.
So, to test task1, you can use ./compiledFileName client 127.0.0.1 12345 100 1000, and please wait at least 30sec then you will see the lost.

For task2, it's ok to test ./compiledFileName client 127.0.0.1 12345 100 10000, the server will respond quickly

Thank you!
