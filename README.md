# gbf.h

A minimal single-header [Gap Buffer](https://en.wikipedia.org/wiki/Gap_buffer). Designed for efficient insertions and deletions at a moving cursor.

## Demo
![](./demo.gif)

This demo visualizes the gap buffer, the original visualization idea is taken from [this youtube video](https://youtu.be/NH7PapZINtc?si=iZomXg5TAUPiP1IA) however the author did not share the implementation, so I tried to do something similar and share it.

Remove -DGAP_DEBUG to use it as a simple readline-like line editor!

 ## Quick Start
 ```sh
# run with --help for motions
$ make && ./demo
 ```
