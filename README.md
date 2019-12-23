Author: Ryan Watkins

Email: rdwatkin@ucsc.edu

Build:

"make"

Run:

"./httpserver hostname -a mappingFile

Options:


[portname] - Define the port to be used at runtime, default is 80


[-N threadNum] - Define the number of threads to be created at runtime, default is 4


[-l logFileName] - Specify actions should be logged to logFileName


Supported Commands:
PATCH
GET
PUT


Tested with curl and netcat to send HTTP requests

Note: Directories not supported and filenames must be 27 characters long
